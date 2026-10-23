// Beetle PCE PS5 frontend: the game library (fe_games.h).
// SPDX-License-Identifier: MIT

#include "fe_games.h"
#include "fe_7z.h"

#include "OrbisPaths.h"
#include "ProsperoThread.h"
#include "netfs.h"

#include "third_party/unzip.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>
#include <unordered_map>

#define PCEPS5_INCBIN(sym, path)                                                                                \
	__asm__(".section .rodata\n"                                                                               \
			".balign 16\n"                                                                                     \
			".global " #sym "_begin\n" #sym "_begin:\n"                                                        \
			".incbin \"" path "\"\n"                                                                           \
			".global " #sym "_end\n" #sym "_end:\n"                                                            \
			".previous\n");                                                                                    \
	extern "C" const char sym##_begin[];                                                                       \
	extern "C" const char sym##_end[];

PCEPS5_INCBIN(pceps5_gamedb, GAMEDB_TSV)

namespace fe
{
namespace
{
const SystemInfo kSystems[size_t(System::Count)] = {
	{"pce", "PC Engine", "NEC_-_PC_Engine_-_TurboGrafx_16", Family::Pce, "PCEngine"},
	{"sgx", "SuperGrafx", "NEC_-_PC_Engine_SuperGrafx", Family::Sgx, "SuperGrafx"},
	{"pcecd", "PC Engine CD", "NEC_-_PC_Engine_CD_-_TurboGrafx-CD", Family::Cd, "PCEngineCD"},
};

struct ExtSys
{
	const char* ext;
	System sys;
};
// What the shelf lists (Beetle PCE's extensions): HuCards .pce (TurboGrafx-16 cards too), SuperGrafx cards .sgx,
// CDs as .cue (with their .bin/.iso/.wav/.ogg tracks), .chd, .ccd (CloneCD), .toc (cdrdao) and .m3u (several
// discs).
const ExtSys kExts[] = {
	{".pce", System::Pce}, {".sgx", System::Sgx},
	{".cue", System::Cd}, {".chd", System::Cd}, {".ccd", System::Cd}, {".toc", System::Cd}, {".m3u", System::Cd},
};

uint32_t Crc(const uint8_t* p, size_t n)
{
	return uint32_t(crc32(0L, p, uInt(n)));
}

std::string Lower(std::string s)
{
	for (char& c : s)
		c = char(tolower(uint8_t(c)));
	return s;
}

std::string ExtOf(const std::string& name)
{
	const size_t dot = name.find_last_of('.');
	const size_t slash = name.find_last_of("/\\");
	if (dot == std::string::npos || dot == 0 || (slash != std::string::npos && dot < slash))
		return "";
	return Lower(name.substr(dot));
}

// The systems searched together for a game of system s: a SuperGrafx game is often kept as .pce.
std::vector<System> Group(System s)
{
	switch (s)
	{
		case System::Pce: return {System::Pce, System::Sgx};
		case System::Sgx: return {System::Sgx, System::Pce};
		default: return {s};
	}
}

// "Super Mario World (USA) (Rev 1)" -> "super mario world"; also drops [tags] and trims.
std::string Key(const std::string& name)
{
	std::string out;
	int depth = 0;
	for (char c : name)
	{
		if (c == '(' || c == '[')
			depth++;
		else if ((c == ')' || c == ']') && depth > 0)
			depth--;
		else if (depth == 0)
			out += char(tolower(uint8_t(c)));
	}
	// collapse spaces, drop punctuation that file names often lose
	std::string k;
	bool space = false;
	for (char c : out)
	{
		if (c == '_' || c == ' ' || c == '.')
			space = !k.empty();
		else if (isalnum(uint8_t(c)) || c == '&' || c == '\'' || c == '-' || c == ',' || c == '!')
		{
			if (space)
				k += ' ';
			space = false;
			k += c;
		}
	}
	return k;
}

int RegionRank(const std::string& name)
{
	// prefer clean dumps: no Beta/Proto/Pirate/Virtual Console/etc.
	int penalty = 0;
	static const char* const bad[] = {"(Beta", "(Proto", "(Pirate", "(Virtual Console", "(Sample", "(Demo", "(Alt",
		"(Unl", "(Aftermarket", "(Arcade", "(Kiosk", "(Program", "(Hack", "(Switch Online", "(Collection"};
	for (const char* b : bad)
		if (name.find(b) != std::string::npos)
			penalty += 100;
	if (name.find("(Rev") != std::string::npos)
		penalty += 1;
	if (name.find("USA") != std::string::npos)
		return penalty + 0;
	if (name.find("World") != std::string::npos)
		return penalty + 2;
	if (name.find("Europe") != std::string::npos)
		return penalty + 4;
	if (name.find("Japan") != std::string::npos)
		return penalty + 6;
	return penalty + 8;
}

uint64_t CrcKey(System s, uint32_t crc)
{
	return (uint64_t(s) << 32) | crc;
}

std::string NameKey(System s, const std::string& k)
{
	return std::string(1, char('A' + int(s))) + k;
}

struct Db
{
	size_t entries = 0;
	std::unordered_map<uint64_t, std::string> by_crc;
	std::unordered_map<std::string, std::string> exact; // sys + lower-case name -> name
	std::unordered_map<std::string, std::string> loose; // sys + Key -> best name
	Db()
	{
		const char* p = pceps5_gamedb_begin;
		const char* end = pceps5_gamedb_end;
		while (p < end)
		{
			const char* nl = static_cast<const char*>(memchr(p, '\n', size_t(end - p)));
			if (!nl)
				nl = end;
			Line(std::string(p, size_t(nl - p)));
			p = nl + 1;
		}
	}
	void Line(const std::string& line)
	{
		if (line.empty() || line[0] == '#')
			return;
		const size_t t1 = line.find('\t');
		const size_t t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
		if (t2 == std::string::npos)
			return;
		const std::string id = line.substr(0, t1);
		const std::string crc = line.substr(t1 + 1, t2 - t1 - 1);
		const std::string name = line.substr(t2 + 1);
		System sys = System::Count;
		for (int i = 0; i < int(System::Count); i++)
			if (id == kSystems[i].id)
				sys = System(i);
		if (sys == System::Count || name.empty())
			return;
		entries++;
		if (crc != "-")
			by_crc.emplace(CrcKey(sys, uint32_t(strtoul(crc.c_str(), nullptr, 16))), name);
		exact.emplace(NameKey(sys, Lower(name)), name);
		const std::string k = NameKey(sys, Key(name));
		auto it = loose.find(k);
		if (it == loose.end() || RegionRank(name) < RegionRank(it->second))
			loose[k] = name;
	}
};

const Db& D()
{
	static const Db db;
	return db;
}

// ---- CRC cache: path \t size \t mtime \t whole \t body ----
// One per process: the scan and the pass that names network ROMs (NetNaming) share it, each under the lock.
struct CrcCache
{
	std::mutex lock;
	std::unordered_map<std::string, std::string> lines; // path -> "size\tmtime\twhole\tbody"
	std::unordered_map<std::string, bool> seen; // the paths this scan looked up
	bool dirty = false;
	bool loaded = false;
	std::string file;
	// A new scan: reads the file the first time, forgets which paths the last scan saw.
	void Begin()
	{
		std::lock_guard<std::mutex> g(lock);
		seen.clear();
		if (loaded)
			return;
		loaded = true;
		file = OrbisDir("covers") + "/crc-cache.txt";
		FILE* f = fopen(file.c_str(), "r");
		if (!f)
			return;
		char line[2048];
		while (fgets(line, sizeof(line), f))
		{
			line[strcspn(line, "\r\n")] = 0;
			char* tab = strchr(line, '\t');
			if (tab)
			{
				*tab = 0;
				lines[line] = tab + 1;
			}
		}
		fclose(f);
	}
	// prune: the scan's end drops the entries of files that are gone; the naming pass (mid-scan, maybe) keeps all.
	void Save(bool prune)
	{
		std::lock_guard<std::mutex> g(lock);
		// entries of files that are gone are dropped (a USB drive that isn't plugged in, or a share that is not
		// connected, keeps its own)
		for (auto it = lines.begin(); prune && it != lines.end();)
		{
			const std::string& key = it->first;
			const bool network = netfs_is_network_path(key.c_str()) ||
				((key.rfind("sys:", 0) == 0 || key.rfind("cue:", 0) == 0) && netfs_is_network_path(key.c_str() + 4));
			if (!seen.count(key) && key.rfind("/mnt/", 0) != 0 && !network)
			{
				it = lines.erase(it);
				dirty = true;
			}
			else
				++it;
		}
		if (!dirty)
			return;
		const std::string tmp = file + ".part";
		FILE* f = fopen(tmp.c_str(), "w");
		if (!f)
			return;
		for (const auto& kv : lines)
			fprintf(f, "%s\t%s\n", kv.first.c_str(), kv.second.c_str());
		bool ok = ferror(f) == 0;
		ok = fclose(f) == 0 && ok;
		if (!ok || rename(tmp.c_str(), file.c_str()) != 0)
		{
			remove(tmp.c_str());
			return;
		}
		dirty = false;
	}
	void Put(const std::string& path, const std::string& val)
	{
		std::lock_guard<std::mutex> g(lock);
		lines[path] = val;
		dirty = true;
	}
};

CrcCache& SharedCrcCache()
{
	static CrcCache* c = new CrcCache; // never freed: the naming pass may still run at exit
	return *c;
}

// Whether a file that could not be used (no ROM in a zip, a CRC that could not be taken) may be remembered as such.
// A local file: yes. A file on a share: only when it reads whole, without an error, from a share that is still
// connected. A read error (the share dropped, the watchdog cut a slow request, Settings > Network shares connected
// the shares again) is not the file's fault, and a remembered failure would hide the game, or keep it unnamed, until
// the file changes; it is tried again at the next visit of the shelf instead.
bool FailureIsTheFiles(const std::string& path, const struct stat& st, const std::atomic<bool>* cancel = nullptr)
{
	if (!netfs_is_network_path(path.c_str()))
		return true;
	if (st.st_size > 64LL * 1024 * 1024)
		return false; // too big to read through each time it fails
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::vector<char> buf(1 << 20);
	long long total = 0;
	bool ok = true;
	for (;;)
	{
		if (cancel && cancel->load())
		{
			ok = false;
			break;
		}
		const size_t got = fread(buf.data(), 1, buf.size(), f);
		total += (long long)got;
		if (got < buf.size())
			break;
	}
	ok = ok && ferror(f) == 0 && total == (long long)st.st_size;
	fclose(f);
	if (!ok)
		return false;
	for (const std::string& m : netfs_mount_points())
		if (path.size() > m.size() && path.compare(0, m.size(), m) == 0 && path[m.size()] == '/')
			return true;
	return false;
}

// read: false for a ROM on a share, where only a CRC already in the cache is used (reading every ROM over the
// network is what makes a scan slow). known (if given): the cache holds an answer for this file as it is now,
// "could not be read" included.
bool CachedCrc(CrcCache& cache, const std::string& path, uint32_t* whole, uint32_t* body, bool read = true, bool* known = nullptr)
{
	if (known)
		*known = false;
	{
		std::lock_guard<std::mutex> g(cache.lock);
		cache.seen[path] = true;
	}
	struct stat st = {};
	if (stat(path.c_str(), &st) != 0)
		return false;
	char key[64];
	snprintf(key, sizeof(key), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
	{
		std::lock_guard<std::mutex> g(cache.lock);
		auto it = cache.lines.find(path);
		if (it != cache.lines.end() && it->second.compare(0, strlen(key), key) == 0)
		{
			if (known)
				*known = true;
			if (it->second.compare(strlen(key), std::string::npos, "fail") == 0)
				return false; // couldn't be read last time, and hasn't changed since
			unsigned a = 0, b = 0;
			if (sscanf(it->second.c_str() + strlen(key), "%x\t%x", &a, &b) == 2)
			{
				*whole = a;
				*body = b;
				return true;
			}
		}
	}
	if (!read)
		return false;
	char val[96];
	if (!RomCrc32(path, whole, body))
	{
		// unreadable (or an empty zip): remembered, so the next scans don't read it again until it changes
		if (FailureIsTheFiles(path, st))
		{
			snprintf(val, sizeof(val), "%sfail", key);
			cache.Put(path, val);
		}
		return false;
	}
	snprintf(val, sizeof(val), "%s%08X\t%08X", key, *whole, *body);
	cache.Put(path, val);
	return true;
}

// The files a disc sheet of this folder names: the tracks of a .cue (FILE "Game (Track 01).bin" BINARY) or a
// .toc (FILE "track01.bin" 0 / DATAFILE "Game.bin"), the .cue/.ccd/.chd of an .m3u, the .img/.sub of a .ccd (same
// name). They belong to a disc, not to the shelf as games of their own (a .cue listed by an .m3u, or a .bin track).
// Each sheet's list is cached while the sheet hasn't changed, so a scan again (each share that connects, each batch of
// the network naming pass) doesn't read every sheet of a share once more: "cue:<path>" -> "size\tmtime\t<file>\t<file>...".
// A sheet that could not be read whole is not cached.
std::vector<std::string> SheetFiles(const std::string& dir, CrcCache& cache)
{
	std::vector<std::string> files;
	DIR* d = opendir(dir.c_str());
	if (!d)
		return files;
	auto keep = [&](std::string file) {
		const size_t slash = file.find_last_of("/\\");
		if (slash != std::string::npos)
			file = file.substr(slash + 1);
		if (!file.empty())
			files.push_back(Lower(file));
	};
	while (dirent* e = readdir(d))
	{
		const std::string ext = ExtOf(e->d_name);
		const std::string name = e->d_name;
		if (ext == ".ccd")
		{
			const std::string base = name.substr(0, name.size() - 4);
			keep(base + ".img");
			keep(base + ".sub");
			continue;
		}
		if (ext != ".cue" && ext != ".toc" && ext != ".m3u")
			continue;
		const std::string path = dir + "/" + name;
		struct stat st = {};
		if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		const std::string key = "cue:" + path;
		char stamp[64];
		snprintf(stamp, sizeof(stamp), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
		bool cached = false;
		{
			std::lock_guard<std::mutex> g(cache.lock);
			cache.seen[key] = true;
			auto it = cache.lines.find(key);
			if (it != cache.lines.end() && it->second.compare(0, strlen(stamp), stamp) == 0)
			{
				cached = true;
				const std::string& v = it->second;
				for (size_t at = strlen(stamp); at < v.size();)
				{
					size_t tab = v.find('\t', at);
					if (tab == std::string::npos)
						tab = v.size();
					if (tab > at)
						files.push_back(v.substr(at, tab - at));
					at = tab + 1;
				}
			}
		}
		if (cached)
			continue;
		FILE* f = fopen(path.c_str(), "r");
		if (!f)
			continue;
		const size_t first = files.size();
		char line[1024];
		while (fgets(line, sizeof(line), f))
		{
			const char* p = line;
			while (*p == ' ' || *p == '\t')
				p++;
			if (ext == ".m3u")
			{
				// one disc per line; '#' starts a comment
				std::string file = p;
				while (!file.empty() && (file.back() == '\n' || file.back() == '\r' || file.back() == ' '))
					file.pop_back();
				if (!file.empty() && file[0] != '#')
					keep(file);
				continue;
			}
			if (strncasecmp(p, "FILE", 4) == 0)
				p += 4;
			else if (strncasecmp(p, "DATAFILE", 8) == 0)
				p += 8;
			else
				continue;
			// FILE "Game (Track 01).bin" BINARY, or FILE track01.bin BINARY; a path keeps only its file name
			while (*p == ' ' || *p == '\t')
				p++;
			std::string file;
			if (*p == '"')
			{
				const char* q2 = strchr(p + 1, '"');
				if (q2)
					file.assign(p + 1, q2);
			}
			else
			{
				const char* end = p;
				while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n')
					end++;
				file.assign(p, end);
			}
			keep(file);
		}
		const bool read_ok = ferror(f) == 0;
		fclose(f);
		if (read_ok)
		{
			std::string val = stamp;
			for (size_t i = first; i < files.size(); i++)
				val += (i == first ? "" : "\t") + files[i];
			cache.Put(key, val);
		}
	}
	closedir(d);
	return files;
}

// A zip's system (the ROM inside it), from the cache while the zip hasn't changed: opening every zip at every
// start is slow on a USB drive with a big library. "sys:<path>" -> "size\tmtime\t<system>".
// read: false for a zip on a share (*known then says whether the cache had it; NetNaming opens it later).
bool CachedArchiveSystem(CrcCache& cache, const std::string& path, const struct stat& st, System* out, bool read = true,
	bool* known = nullptr, const std::atomic<bool>* cancel = nullptr)
{
	const std::string key = "sys:" + path;
	char stamp[64];
	snprintf(stamp, sizeof(stamp), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
	if (known)
		*known = true;
	{
		std::lock_guard<std::mutex> g(cache.lock);
		cache.seen[key] = true;
		auto it = cache.lines.find(key);
		if (it != cache.lines.end() && it->second.compare(0, strlen(stamp), stamp) == 0)
		{
			const std::string v = it->second.substr(strlen(stamp));
			if (v == "none")
				return false;
			const int n = atoi(v.c_str());
			if (n >= 0 && n < int(System::Count) && System(n) != System::Cd)
			{
				*out = System(n);
				return true;
			}
		}
	}
	if (!read)
	{
		if (known)
			*known = false;
		return false;
	}
	bool certain = false;
	const bool ok = ArchiveSystem(path, out, cancel, &certain);
	// "none" hides the zip from the shelf until it changes: only when it is really the archive's (a 7z's directory says
	// so, or FailureIsTheFiles)
	if (ok || certain || FailureIsTheFiles(path, st, cancel))
		cache.Put(key, std::string(stamp) + (ok ? std::to_string(int(*out)) : std::string("none")));
	return ok;
}

// The system a zip on a share most likely holds before NetNaming has opened it: its folder's name (roms/SuperGrafx/,
// "SuperGrafx", "sgx"...), else the PC Engine.
System GuessSystemFromFolder(const std::string& path)
{
	std::string dir = path.substr(0, path.find_last_of('/'));
	for (int depth = 0; depth < 3 && !dir.empty(); depth++)
	{
		std::string name = dir.substr(dir.find_last_of('/') + 1), key;
		for (char c : name)
			if (isalnum(uint8_t(c)))
				key += char(tolower(uint8_t(c)));
		if (key.find("supergrafx") != std::string::npos || key == "sgx")
			return System::Sgx;
		if (key.find("pcengine") != std::string::npos || key.find("turbografx") != std::string::npos || key == "pce")
			return System::Pce;
		dir = dir.substr(0, dir.find_last_of('/'));
	}
	return System::Pce;
}

void Walk(const std::string& dir, int depth, bool usb, bool net, std::vector<GameInfo>& out, CrcCache& cache,
	const ScanProgress& progress)
{
	const std::vector<std::string> tracks = SheetFiles(dir, cache);
	DIR* d = opendir(dir.c_str());
	if (!d)
		return;
	while (dirent* e = readdir(d))
	{
		if (e->d_name[0] == '.')
			continue;
		const std::string path = dir + "/" + e->d_name;
		struct stat st = {};
		if (stat(path.c_str(), &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
		{
			if (depth < 4)
				Walk(path, depth + 1, usb, net, out, cache, progress);
			continue;
		}
		if (!S_ISREG(st.st_mode))
			continue; // a FIFO or a device would block the CRC read (and the shelf) forever
		const std::string name = e->d_name;
		const std::string ext = ExtOf(name);
		if (ext.empty())
			continue;
		if (std::find(tracks.begin(), tracks.end(), Lower(name)) != tracks.end())
			continue; // a disc's track
		GameInfo g;
		if (IsArchiveExt(ext))
		{
			// a zip (or a 7z) on a share is not opened by the scan: the cache knows its system, or its folder's name says it
			// until NetNaming has looked inside
			bool known = true;
			if (!CachedArchiveSystem(cache, path, st, &g.system, !net, &known))
			{
				if (known)
					continue;
				g.system = GuessSystemFromFolder(path);
				g.crc_pending = true;
			}
		}
		else if (!SystemForExt(ext, &g.system))
			continue;
		g.path = path;
		g.file_base = name.substr(0, name.size() - ext.size());
		g.ext = ext;
		g.on_usb = usb;
		g.on_network = net;
		out.push_back(std::move(g));
		if (progress)
			progress(0, out.size(), 0);
	}
	closedir(d);
}

template <typename F>
std::string Search(System s, System* found_in, F&& find)
{
	for (System g : Group(s))
	{
		std::string n = find(g);
		if (!n.empty())
		{
			if (found_in)
				*found_in = g;
			return n;
		}
	}
	return "";
}
} // namespace

const SystemInfo& Info(System s)
{
	return kSystems[size_t(s) < size_t(System::Count) ? size_t(s) : 0];
}

const char* FamilyName(Family f)
{
	switch (f)
	{
		case Family::All: return "All games";
		case Family::Pce: return "PC Engine";
		case Family::Sgx: return "SuperGrafx";
		case Family::Cd: return "PC Engine CD";
		default: return "?";
	}
}

bool SystemForExt(const std::string& ext, System* out)
{
	const std::string e = Lower(ext);
	for (const ExtSys& x : kExts)
		if (e == x.ext)
		{
			*out = x.sys;
			return true;
		}
	return false;
}

namespace
{
// Calls fn(name) for each file of the zip until it returns true; the zip is then positioned on that file.
template <typename F>
bool FindInZip(unzFile z, F&& fn)
{
	for (int r = unzGoToFirstFile(z); r == UNZ_OK; r = unzGoToNextFile(z))
	{
		unz_file_info info;
		char name[512];
		// minizip ends the name with '\0' only when it fits: keep the last byte for it, skip names that don't fit
		name[sizeof(name) - 1] = '\0';
		if (unzGetCurrentFileInfo(z, &info, name, sizeof(name) - 1, nullptr, 0, nullptr, 0) != UNZ_OK ||
			info.size_filename >= sizeof(name) - 1)
			continue;
		if (fn(std::string(name)))
			return true;
	}
	return false;
}
} // namespace

bool IsArchiveExt(const std::string& ext)
{
	const std::string e = Lower(ext);
	return e == ".zip" || e == ".7z";
}

namespace
{
// The biggest HuCard accepted from an archive (the biggest HuCard mapper takes 8.5 MiB)
constexpr uint64_t kMaxArchivedRom = 16u * 1024 * 1024;

// A file of an archive that is a game we list (a HuCard: a disc image inside an archive is not, it would have to be
// unpacked whole).
bool IsArchivedRom(const std::string& name, System* sys = nullptr)
{
	System s;
	if (!SystemForExt(ExtOf(name), &s) || s == System::Cd)
		return false;
	if (sys)
		*sys = s;
	return true;
}

// A .7z's first HuCard, in the archive's order (a zip's rule): its index, or -1.
int First7zRom(const sevenzip::Archive& a)
{
	const std::vector<sevenzip::Entry>& entries = a.Entries();
	for (size_t i = 0; i < entries.size(); i++)
		if (!entries[i].is_dir && IsArchivedRom(entries[i].name))
			return int(i);
	return -1;
}

// What went wrong with a .7z, for the player (English; T() translates it).
const char* SevenZipWhy(sevenzip::Status st)
{
	switch (st)
	{
		case sevenzip::Status::Unsupported:
			return "It is packed with a method Beetle PCE PS5 can't unpack (LZMA, LZMA2 and PPMd work), or its names are encrypted.";
		case sevenzip::Status::Encrypted: return "The archive is encrypted.";
		case sevenzip::Status::TooBig:
			return "The game, or the solid block it is packed in, is too big (16 MiB for a game, 64 MiB for a block).";
		case sevenzip::Status::ReadError: return "The file could not be read.";
		case sevenzip::Status::NoMemory: return "There is not enough memory to unpack it.";
		default: return "The archive is damaged, or it is not a 7z archive.";
	}
}
} // namespace

namespace
{
// A .7z that can't give a game for a reason of its own, read without an error: no HuCard in its directory, encrypted
// names, a method the decoder doesn't have, too big. Such an answer may be remembered without reading the whole file
// again (FailureIsTheFiles); a read error, a cancel or a damaged-looking archive (which a cut read can make) may not.
bool CertainFailure(sevenzip::Status st)
{
	return st == sevenzip::Status::Encrypted || st == sevenzip::Status::Unsupported || st == sevenzip::Status::TooBig;
}
} // namespace

bool ArchiveSystem(const std::string& path, System* out, const std::atomic<bool>* cancel, bool* certain)
{
	if (certain)
		*certain = false;
	if (ExtOf(path) == ".7z")
	{
		sevenzip::Archive a;
		const sevenzip::Status st = a.Open(path, cancel);
		if (st != sevenzip::Status::Ok)
		{
			if (certain)
				*certain = CertainFailure(st);
			return false;
		}
		const int i = First7zRom(a);
		if (i < 0 && certain)
			*certain = true; // its directory was read whole: no HuCard in it
		return i >= 0 && IsArchivedRom(a.Entries()[size_t(i)].name, out);
	}
	unzFile z = unzOpen(path.c_str());
	if (!z)
		return false;
	const bool ok = FindInZip(z, [&](const std::string& name) { return IsArchivedRom(name, out); });
	unzClose(z);
	return ok;
}

// The CRC an archive stores for its ROM (a zip's and a 7z's directory both keep it), when the ROM has no copier header
// to skip (HuCardHeader): true and the name inside; false when the data has to be read (a header). RomCrc32 uses it
// only when the game table knows it: a CRC it doesn't know may be a TurboGrafx-16 dump with its data lines reversed,
// which is read once (and its CRCs cached) to be named as the same dump outside an archive is.
bool StoredArchiveCrc(const std::string& path, std::string* inner, uint32_t* crc, const std::atomic<bool>* cancel = nullptr)
{
	if (ExtOf(path) == ".7z")
	{
		sevenzip::Archive a;
		if (a.Open(path, cancel) != sevenzip::Status::Ok)
			return false;
		const int i = First7zRom(a);
		if (i < 0)
			return false;
		const sevenzip::Entry& e = a.Entries()[size_t(i)];
		if (!e.has_crc || e.size == 0 || e.size > kMaxArchivedRom || HuCardHeader(size_t(e.size)) != 0)
			return false;
		*inner = e.name;
		*crc = e.crc;
		return true;
	}
	unzFile z = unzOpen(path.c_str());
	if (!z)
		return false;
	bool ok = FindInZip(z, [&](const std::string& name) {
		if (!IsArchivedRom(name))
			return false;
		*inner = name;
		return true;
	});
	if (ok)
	{
		unz_file_info info;
		ok = unzGetCurrentFileInfo(z, &info, nullptr, 0, nullptr, 0, nullptr, 0) == UNZ_OK &&
			 info.uncompressed_size > 0 && info.uncompressed_size <= kMaxArchivedRom &&
			 HuCardHeader(info.uncompressed_size) == 0;
		if (ok)
			*crc = uint32_t(info.crc);
	}
	unzClose(z);
	return ok;
}

bool ReadFromArchive(const std::string& path, std::string* inner, std::vector<uint8_t>* data, std::string* why,
	const std::atomic<bool>* cancel, bool* certain)
{
	if (certain)
		*certain = false;
	if (ExtOf(path) == ".7z")
	{
		sevenzip::Archive a;
		sevenzip::Status st = a.Open(path, cancel);
		int i = -1;
		if (st == sevenzip::Status::Ok)
		{
			i = First7zRom(a);
			if (i < 0)
			{
				if (certain)
					*certain = true;
				return false; // no HuCard inside (a disc image is not unpacked): the caller says so
			}
			*inner = a.Entries()[size_t(i)].name;
			st = a.Extract(size_t(i), data, kMaxArchivedRom);
			if (st == sevenzip::Status::Ok && data->empty())
				st = sevenzip::Status::Broken;
		}
		if (st != sevenzip::Status::Ok)
		{
			OrbisLog("[games] %s%s%s: %s", path.c_str(), i >= 0 ? " -> " : "", i >= 0 ? inner->c_str() : "",
				sevenzip::StatusName(st));
			if (why)
				*why = SevenZipWhy(st);
			if (certain)
				*certain = CertainFailure(st);
			data->clear();
			return false;
		}
		return true;
	}
	unzFile z = unzOpen(path.c_str());
	if (!z)
	{
		if (why)
			*why = "The file could not be read."; // a share that dropped, or not a zip at all
		return false;
	}
	bool ok = FindInZip(z, [&](const std::string& name) {
		if (!IsArchivedRom(name))
			return false;
		*inner = name;
		return true;
	});
	if (ok)
	{
		unz_file_info info;
		ok = unzGetCurrentFileInfo(z, &info, nullptr, 0, nullptr, 0, nullptr, 0) == UNZ_OK &&
			 info.uncompressed_size > 0 && info.uncompressed_size <= kMaxArchivedRom && unzOpenCurrentFile(z) == UNZ_OK;
		if (ok)
		{
			data->resize(info.uncompressed_size);
			ok = unzReadCurrentFile(z, data->data(), unsigned(data->size())) == int(data->size());
			unzCloseCurrentFile(z);
		}
		if (!ok)
		{
			data->clear();
			if (why)
				*why = "The file could not be read."; // the HuCard is there, its data didn't come (or is damaged)
		}
	}
	unzClose(z);
	return ok;
}

namespace gamedb
{
size_t Count()
{
	return D().entries;
}

std::string ByCrc(System s, uint32_t crc, System* found_in)
{
	return Search(s, found_in, [&](System g) {
		auto it = D().by_crc.find(CrcKey(g, crc));
		return it == D().by_crc.end() ? std::string() : it->second;
	});
}

std::string Exact(System s, const std::string& name, System* found_in)
{
	return Search(s, found_in, [&](System g) {
		auto it = D().exact.find(NameKey(g, Lower(name)));
		return it == D().exact.end() ? std::string() : it->second;
	});
}

std::string Loose(System s, const std::string& file_base, System* found_in)
{
	return Search(s, found_in, [&](System g) {
		auto it = D().loose.find(NameKey(g, Key(file_base)));
		return it == D().loose.end() ? std::string() : it->second;
	});
}

std::string Title(const std::string& nointro)
{
	std::string t = nointro.substr(0, nointro.find(" ("));
	// "Legend of Zelda, The - A Link to the Past" -> "The Legend of Zelda - A Link to the Past"
	static const char* const arts[] = {", The", ", A", ", An"};
	for (const char* a : arts)
	{
		const size_t pos = t.find(a);
		if (pos != std::string::npos)
		{
			const size_t end = pos + strlen(a);
			if (end == t.size() || t.compare(end, 3, " - ") == 0 || t[end] == ':')
			{
				const std::string art = std::string(a + 2);
				t = art + " " + t.substr(0, pos) + t.substr(end);
				break;
			}
		}
	}
	return t;
}

std::string Region(const std::string& nointro)
{
	const size_t a = nointro.find(" (");
	if (a == std::string::npos)
		return "";
	const size_t b = nointro.find(')', a);
	return b == std::string::npos ? "" : nointro.substr(a + 2, b - a - 2);
}
} // namespace gamedb

size_t HuCardHeader(size_t size)
{
	return (size & 512) ? 512 : 0;
}

namespace
{
uint8_t Reverse8(uint8_t b)
{
	b = uint8_t((b & 0xF0) >> 4 | (b & 0x0F) << 4);
	b = uint8_t((b & 0xCC) >> 2 | (b & 0x33) << 2);
	return uint8_t((b & 0xAA) >> 1 | (b & 0x55) << 1);
}
} // namespace

bool IsReversedHuCard(const uint8_t* rom, size_t size)
{
	// The HuC6280 starts with the card's first 8 KiB bank at 0xE000 and jumps through the vector at 0xFFFE: a
	// card that starts has its high byte (offset 0x1FFF) in 0xE0..0xFF; reversed, it only does once put right.
	if (size < 0x2000)
		return false;
	return rom[0x1FFF] < 0xE0 && Reverse8(rom[0x1FFF]) >= 0xE0;
}

void FixReversedHuCard(uint8_t* rom, size_t size)
{
	for (size_t i = 0; i < size; i++)
		rom[i] = Reverse8(rom[i]);
}

bool RomCrc32(const std::string& path, uint32_t* whole, uint32_t* body, std::string* inner_ext, const std::atomic<bool>* cancel,
	bool* certain)
{
	if (certain)
		*certain = false;
	auto stopped = [cancel] { return cancel && cancel->load(); };
	std::string ext = ExtOf(path);
	std::vector<uint8_t> data;
	System sys;
	if (IsArchiveExt(ext))
	{
		// the zip (or 7z) keeps its ROM's CRC: the archive is read only when a 512-byte copier header has to come off for
		// the body's, or when the table doesn't know that CRC (a reversed dump is put right first, as outside an archive)
		std::string inner;
		uint32_t stored = 0;
		if (StoredArchiveCrc(path, &inner, &stored, cancel) && !gamedb::ByCrc(System::Pce, stored, nullptr).empty())
		{
			if (inner_ext)
				*inner_ext = ExtOf(inner);
			*whole = *body = stored;
			return true;
		}
		if (!ReadFromArchive(path, &inner, &data, nullptr, cancel, certain))
			return false;
		ext = ExtOf(inner);
	}
	else if (!SystemForExt(ext, &sys) || sys == System::Cd)
		return false; // a disc: named by its file name only
	else
	{
		FILE* f = fopen(path.c_str(), "rb");
		if (!f)
			return false;
		fseek(f, 0, SEEK_END);
		const long size = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (size <= 0 || size > 64L * 1024 * 1024)
		{
			fclose(f);
			return false;
		}
		data.resize(size_t(size));
		// in pieces, so a cancel (NetNaming::Stop) does not wait for a whole ROM to come over the network
		size_t n = 0;
		while (n < data.size() && !stopped())
		{
			const size_t got = fread(data.data() + n, 1, std::min<size_t>(1 << 20, data.size() - n), f);
			if (got == 0)
				break;
			n += got;
		}
		fclose(f);
		if (n != data.size())
			return false;
	}
	if (inner_ext)
		*inner_ext = ext;
	*whole = Crc(data.data(), data.size());
	const size_t skip = HuCardHeader(data.size());
	*body = skip > 0 && skip < data.size() ? Crc(data.data() + skip, data.size() - skip) : *whole;
	if (skip < data.size() && IsReversedHuCard(data.data() + skip, data.size() - skip))
	{
		FixReversedHuCard(data.data() + skip, data.size() - skip);
		*body = Crc(data.data() + skip, data.size() - skip);
	}
	return true;
}

namespace
{
// "mm:ss:ff" -> frames (75 a second); -1 if it isn't one
long Msf(const char* p)
{
	int m = 0, sec = 0, f = 0;
	return sscanf(p, "%d:%d:%d", &m, &sec, &f) == 3 ? long(m) * 4500 + sec * 75 + f : -1;
}

std::string SheetName(const char*& p)
{
	while (*p == ' ' || *p == '\t')
		p++;
	std::string file;
	if (*p == '"')
	{
		const char* q = strchr(p + 1, '"');
		if (q)
		{
			file.assign(p + 1, q);
			p = q + 1;
		}
	}
	else
	{
		const char* e = p;
		while (*e && *e != ' ' && *e != '\t' && *e != '\r' && *e != '\n')
			e++;
		file.assign(p, e);
		p = e;
	}
	for (char& c : file)
		if (c == '\\')
			c = '/';
	return file;
}

void DiscSpansOf(const std::string& sheet, std::vector<DiscSpan>& out, int depth)
{
	constexpr uint64_t kHead = 1 << 20; // two of netfs's blocks: a few seconds of a track, the reader ahead by then
	const std::string dir = sheet.substr(0, sheet.find_last_of('/'));
	const std::string ext = ExtOf(sheet);
	auto add = [&](const std::string& file, uint64_t at) {
		if (!file.empty())
			out.push_back({file[0] == '/' ? file : dir + "/" + file, at, kHead});
	};
	if (ext == ".chd")
	{
		add(sheet.substr(sheet.find_last_of('/') + 1), 0);
		return;
	}
	if (ext == ".ccd")
	{
		const std::string base = sheet.substr(0, sheet.size() - 4);
		const std::string img = base.substr(base.find_last_of('/') + 1) + ".img";
		add(img, 0);
		add(base.substr(base.find_last_of('/') + 1) + ".sub", 0);
		// each track's start: [TRACK n] ... INDEX 1=<lba>
		if (FILE* f = fopen(sheet.c_str(), "r"))
		{
			char line[512];
			while (fgets(line, sizeof line, f))
			{
				long lba = 0;
				if (strncasecmp(line, "INDEX 1=", 8) == 0 && (lba = atol(line + 8)) > 0)
					add(img, uint64_t(lba) * 2352);
			}
			fclose(f);
		}
		return;
	}
	if (ext != ".cue" && ext != ".toc" && ext != ".m3u")
		return;
	FILE* f = fopen(sheet.c_str(), "r");
	if (!f)
		return;
	char line[1024];
	std::string file;
	unsigned sector = 2352;
	// Where a track starts in its file: an INDEX's time counts sectors from the start of the file, and each stretch
	// between two INDEX lines has the sector size of the track the first one is in (one .bin can mix MODE1/2048 and
	// 2352-byte tracks). Only a BINARY (or MOTOROLA) file is laid out that way: in a WAVE/OGG/MP3/FLAC... file the
	// offset of a time isn't known, so only its start is fetched.
	bool raw = false, have_point = false;
	long point_at = 0;
	uint64_t point_bytes = 0;
	unsigned point_sector = 2352;
	while (fgets(line, sizeof line, f))
	{
		const char* p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (ext == ".m3u")
		{
			// the first disc is the one in the drive
			std::string disc = p;
			while (!disc.empty() && (disc.back() == '\n' || disc.back() == '\r' || disc.back() == ' '))
				disc.pop_back();
			if (disc.empty() || disc[0] == '#')
				continue;
			if (depth < 2)
				DiscSpansOf(disc[0] == '/' ? disc : dir + "/" + disc, out, depth + 1);
			break;
		}
		if (strncasecmp(p, "FILE", 4) == 0 || strncasecmp(p, "DATAFILE", 8) == 0)
		{
			p += toupper(uint8_t(p[0])) == 'F' ? 4 : 8;
			file = SheetName(p);
			add(file, 0);
			while (*p == ' ' || *p == '\t')
				p++;
			const std::string fext = ExtOf(file);
			raw = strncasecmp(p, "BINARY", 6) == 0 || strncasecmp(p, "MOTOROLA", 8) == 0 ||
				  (!isalpha(uint8_t(*p)) && fext != ".wav" && fext != ".ogg" && fext != ".mp3" && fext != ".flac" &&
					  fext != ".aiff" && fext != ".aif" && fext != ".opus");
			have_point = false;
		}
		else if (strncasecmp(p, "TRACK", 5) == 0)
		{
			// TRACK 02 AUDIO, TRACK 01 MODE1/2048: the size of the file's sectors
			sector = strstr(p, "/2048") ? 2048 : strstr(p, "/2336") ? 2336 : 2352;
		}
		else if (strncasecmp(p, "INDEX", 5) == 0 && (p[5] == ' ' || p[5] == '\t') && !file.empty() && raw)
		{
			// INDEX nn mm:ss:ff (00: a pregap inside the file, 01: where the track starts)
			const char* q = p + 5 + strspn(p + 5, " \t");
			const int index = atoi(q);
			q += strspn(q, "0123456789");
			const long at = Msf(q + strspn(q, " \t"));
			if (at < 0 || (have_point && at < point_at))
				continue;
			const uint64_t bytes = have_point ? point_bytes + uint64_t(at - point_at) * point_sector : uint64_t(at) * sector;
			have_point = true;
			point_at = at;
			point_bytes = bytes;
			point_sector = sector;
			if (index == 1 && bytes > 0)
				add(file, bytes);
		}
	}
	fclose(f);
}
} // namespace

std::vector<DiscSpan> DiscSpans(const std::string& sheet)
{
	std::vector<DiscSpan> out;
	DiscSpansOf(sheet, out, 0);
	return out;
}

void PrepareFolders()
{
	const std::string roms = OrbisDir("roms"), covers = OrbisDir("covers");
	for (const SystemInfo& s : kSystems)
	{
		OrbisMkdirs(roms + "/" + s.folder);
		OrbisMkdirs(covers + "/" + s.folder);
	}
}

// ---- the naming pass for ROMs on shares ----

struct NetNaming::Impl
{
	std::mutex lock;
	std::deque<std::string> todo;
	std::set<std::string> tried; // every path queued: a ROM is read once per run (one whose share went away is let go)
	std::atomic<bool> stop{false};
	std::atomic<int> resolved{0};
	std::atomic<bool> busy{false};
	ps5::BigThread thread;

	void Run()
	{
		CrcCache& cache = SharedCrcCache();
		int named = 0, since_save = 0;
		for (;;)
		{
			std::string path;
			{
				std::unique_lock<std::mutex> g(lock);
				if (stop.load() || todo.empty() || netfs_realtime())
				{
					busy = false;
					break;
				}
				path = todo.front();
				todo.pop_front();
			}
			struct stat st = {};
			if (stat(path.c_str(), &st) != 0)
			{
				// the share went away: the next scan lists it again once it is back, and Add queues it then
				std::lock_guard<std::mutex> g(lock);
				tried.erase(path);
				continue;
			}
			char key[64];
			snprintf(key, sizeof(key), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
			if (IsArchiveExt(ExtOf(path)))
			{
				System sys;
				// the system of the ROM inside, for the next scan
				if (!CachedArchiveSystem(cache, path, st, &sys, true, nullptr, &stop) && !stop.load())
				{
					// an archive known to hold no HuCard (a disc image, nothing, encrypted names): no CRC to take, and
					// the whole file is not read again to prove it
					bool none = false;
					{
						std::lock_guard<std::mutex> g(cache.lock);
						auto it = cache.lines.find("sys:" + path);
						none = it != cache.lines.end() && it->second == std::string(key) + "none";
					}
					if (none)
					{
						cache.Put(path, std::string(key) + "fail");
						named++;
						resolved++;
						continue;
					}
				}
			}
			uint32_t whole = 0, body = 0;
			bool certain = false;
			const bool ok = RomCrc32(path, &whole, &body, nullptr, &stop, &certain);
			if (stop.load())
			{
				// cancelled half way: nothing recorded, and it is the first to be read when the pass starts again
				std::lock_guard<std::mutex> g(lock);
				todo.push_front(path);
				break;
			}
			char val[96];
			if (ok)
				snprintf(val, sizeof(val), "%s%08X\t%08X", key, whole, body);
			else
				snprintf(val, sizeof(val), "%sfail", key);
			// a read error is not remembered as "fail" (FailureIsTheFiles): the next visit of the shelf tries it again
			// (not this one: it stays in `tried`, or every scan the pass's results bring would read it once more)
			if (ok || certain || FailureIsTheFiles(path, st, &stop))
				cache.Put(path, val);
			else if (stop.load())
			{
				std::lock_guard<std::mutex> g(lock);
				todo.push_front(path); // cancelled while it was being checked: the same as above
				break;
			}
			named++;
			resolved++;
			if (++since_save >= 20)
			{
				cache.Save(false);
				since_save = 0;
			}
		}
		cache.Save(false);
		busy = false;
		OrbisLog("[games] network CRC pass: %d ROM(s) read%s", named, stop.load() ? " (stopped)" : "");
	}
};

NetNaming::NetNaming() : m_i(new Impl) {}

NetNaming::~NetNaming()
{
	Stop();
}

void NetNaming::Add(const std::vector<GameInfo>& games)
{
	size_t added = 0, queued = 0;
	{
		std::lock_guard<std::mutex> g(m_i->lock);
		for (const GameInfo& game : games)
			if (game.crc_pending && m_i->tried.insert(game.path).second)
			{
				m_i->todo.push_back(game.path);
				added++;
			}
		queued = m_i->todo.size();
	}
	if (added)
		OrbisLog("[games] naming %zu network ROM(s) by CRC in the background", added);
	// nothing new may still leave work: what a Stop() cut short, or what was queued when a game started (netfs_realtime)
	if (!queued)
		return;
	if (m_i->busy.load())
		return; // the running pass takes the new ones too
	m_i->thread.join();
	m_i->stop = false;
	m_i->busy = true;
	m_i->thread = ps5::BigThread([this] { m_i->Run(); }, 256 * 1024);
}

void NetNaming::Stop()
{
	m_i->stop = true;
	m_i->thread.join();
	m_i->busy = false;
}

int NetNaming::Resolved() const
{
	return m_i->resolved.load();
}

bool NetNaming::Busy() const
{
	return m_i->busy.load();
}

std::vector<std::string> RomRoots()
{
	std::vector<std::string> roots = OrbisRomRoots();
	for (const std::string& r : netfs_mount_points())
		roots.push_back(r);
	return roots;
}

std::vector<GameInfo> ScanGames(const ScanProgress& progress)
{
	PrepareFolders();
	std::vector<GameInfo> games;
	CrcCache& cache = SharedCrcCache();
	cache.Begin();
	for (const std::string& root : RomRoots())
		Walk(root, 0, root.rfind("/mnt/", 0) == 0, netfs_is_network_path(root.c_str()), games, cache, progress);

	size_t identified = 0;
	for (GameInfo& g : games)
	{
		if (progress)
			progress(1, identified++, games.size());
		System found = g.system;
		g.nointro = gamedb::Exact(g.system, g.file_base, &found);
		if (g.nointro.empty())
		{
			uint32_t whole = 0, body = 0;
			bool known = false;
			const bool have = CachedCrc(cache, g.path, &whole, &body, !g.on_network, &known);
			// a cartridge on a share is named from its file name for now: NetNaming reads its CRC (a disc has none)
			if (g.on_network && !known && g.system != System::Cd)
				g.crc_pending = true;
			if (have)
			{
				g.nointro = gamedb::ByCrc(g.system, whole, &found);
				if (g.nointro.empty() && body != whole)
					g.nointro = gamedb::ByCrc(g.system, body, &found);
				g.name_by_crc = !g.nointro.empty();
			}
		}
		if (g.nointro.empty())
			g.nointro = gamedb::Loose(g.system, g.file_base, &found);
		if (!g.nointro.empty())
			g.system = found;
		g.title = g.nointro.empty() ? g.file_base : gamedb::Title(g.nointro);
		g.region = gamedb::Region(g.nointro);
	}
	cache.Save(true);

	std::sort(games.begin(), games.end(), [](const GameInfo& a, const GameInfo& b) {
		const int c = strcasecmp(a.title.c_str(), b.title.c_str());
		if (c != 0)
			return c < 0;
		if (a.system != b.system)
			return a.system < b.system;
		return a.path < b.path;
	});
	OrbisLog("[games] %zu ROM(s); table of %zu names", games.size(), gamedb::Count());
	// each game once per scan is a lot of boot.log for a big library: the first 200
	constexpr size_t kLogged = 200;
	for (size_t i = 0; i < games.size() && i < kLogged; i++)
	{
		const GameInfo& g = games[i];
		OrbisLog("[games]   %s [%s] -> \"%s\"%s", g.path.c_str(), Info(g.system).id, g.nointro.c_str(),
			g.name_by_crc ? " (by CRC)" : "");
	}
	if (games.size() > kLogged)
		OrbisLog("[games]   ... and %zu more", games.size() - kLogged);
	return games;
}
} // namespace fe
