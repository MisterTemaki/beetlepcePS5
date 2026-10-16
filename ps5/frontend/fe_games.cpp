// Beetle PCE PS5 frontend: the game library (fe_games.h).
// SPDX-License-Identifier: MIT

#include "fe_games.h"

#include "OrbisPaths.h"

#include "third_party/unzip.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
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
struct CrcCache
{
	std::unordered_map<std::string, std::string> lines; // path -> "size\tmtime\twhole\tbody"
	std::unordered_map<std::string, bool> seen; // the paths this scan looked up
	bool dirty = false;
	std::string file;
	void Load()
	{
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
	void Save()
	{
		// entries of files that are gone are dropped (a USB drive that isn't plugged in keeps its own)
		for (auto it = lines.begin(); it != lines.end();)
		{
			if (!seen.count(it->first) && it->first.rfind("/mnt/", 0) != 0)
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
};

bool CachedCrc(CrcCache& cache, const std::string& path, uint32_t* whole, uint32_t* body)
{
	cache.seen[path] = true;
	struct stat st = {};
	if (stat(path.c_str(), &st) != 0)
		return false;
	char key[64];
	snprintf(key, sizeof(key), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
	auto it = cache.lines.find(path);
	if (it != cache.lines.end() && it->second.compare(0, strlen(key), key) == 0)
	{
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
	char val[96];
	if (!RomCrc32(path, whole, body))
	{
		// unreadable (or an empty zip): remembered, so the next scans don't read it again until it changes
		snprintf(val, sizeof(val), "%sfail", key);
		cache.lines[path] = val;
		cache.dirty = true;
		return false;
	}
	snprintf(val, sizeof(val), "%s%08X\t%08X", key, *whole, *body);
	cache.lines[path] = val;
	cache.dirty = true;
	return true;
}

// The files a disc sheet of this folder names: the tracks of a .cue (FILE "Game (Track 01).bin" BINARY) or a
// .toc (FILE "track01.bin" 0 / DATAFILE "Game.bin"), the .cue/.ccd/.chd of an .m3u, the .img/.sub of a .ccd (same
// name). They belong to a disc, not to the shelf as games of their own (a .cue listed by an .m3u, or a .bin track).
std::vector<std::string> SheetFiles(const std::string& dir)
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
		FILE* f = fopen((dir + "/" + name).c_str(), "r");
		if (!f)
			continue;
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
		fclose(f);
	}
	closedir(d);
	return files;
}

// A zip's system (the ROM inside it), from the cache while the zip hasn't changed: opening every zip at every
// start is slow on a USB drive with a big library. "sys:<path>" -> "size\tmtime\t<system>".
bool CachedArchiveSystem(CrcCache& cache, const std::string& path, const struct stat& st, System* out)
{
	const std::string key = "sys:" + path;
	cache.seen[key] = true;
	char stamp[64];
	snprintf(stamp, sizeof(stamp), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
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
	const bool ok = ArchiveSystem(path, out);
	cache.lines[key] = std::string(stamp) + (ok ? std::to_string(int(*out)) : std::string("none"));
	cache.dirty = true;
	return ok;
}

void Walk(const std::string& dir, int depth, bool usb, std::vector<GameInfo>& out, CrcCache& cache,
	const ScanProgress& progress)
{
	const std::vector<std::string> tracks = SheetFiles(dir);
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
				Walk(path, depth + 1, usb, out, cache, progress);
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
		if (ext == ".zip")
		{
			if (!CachedArchiveSystem(cache, path, st, &g.system))
				continue;
		}
		else if (!SystemForExt(ext, &g.system))
			continue;
		g.path = path;
		g.file_base = name.substr(0, name.size() - ext.size());
		g.ext = ext;
		g.on_usb = usb;
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

bool ArchiveSystem(const std::string& path, System* out)
{
	unzFile z = unzOpen(path.c_str());
	if (!z)
		return false;
	const bool ok = FindInZip(z, [&](const std::string& name) {
		System s;
		if (!SystemForExt(ExtOf(name), &s) || s == System::Cd)
			return false;
		*out = s;
		return true;
	});
	unzClose(z);
	return ok;
}

// The CRC a zip stores for its ROM, when the ROM has no copier header to skip (its size has no 512 bit): true and
// the name inside; false when the data has to be read (a header, or a TurboGrafx-16 dump that may be reversed:
// those are read once and their CRCs cached).
bool StoredZipCrc(const std::string& path, std::string* inner, uint32_t* crc)
{
	unzFile z = unzOpen(path.c_str());
	if (!z)
		return false;
	bool ok = FindInZip(z, [&](const std::string& name) {
		System s;
		if (!SystemForExt(ExtOf(name), &s) || s == System::Cd)
			return false;
		*inner = name;
		return true;
	});
	if (ok)
	{
		unz_file_info info;
		ok = unzGetCurrentFileInfo(z, &info, nullptr, 0, nullptr, 0, nullptr, 0) == UNZ_OK &&
			 info.uncompressed_size > 0 && info.uncompressed_size <= 16u * 1024 * 1024 &&
			 (info.uncompressed_size & 0x3ff) != 0x200;
		if (ok)
			*crc = uint32_t(info.crc);
	}
	unzClose(z);
	return ok;
}

bool ReadFromZip(const std::string& path, std::string* inner, std::vector<uint8_t>* data)
{
	unzFile z = unzOpen(path.c_str());
	if (!z)
		return false;
	bool ok = FindInZip(z, [&](const std::string& name) {
		System s;
		if (!SystemForExt(ExtOf(name), &s) || s == System::Cd)
			return false;
		*inner = name;
		return true;
	});
	if (ok)
	{
		unz_file_info info;
		ok = unzGetCurrentFileInfo(z, &info, nullptr, 0, nullptr, 0, nullptr, 0) == UNZ_OK &&
			 info.uncompressed_size > 0 && info.uncompressed_size <= 16u * 1024 * 1024 /* the biggest HuCard mapper takes 8.5 MiB */ &&
				 unzOpenCurrentFile(z) == UNZ_OK;
		if (ok)
		{
			data->resize(info.uncompressed_size);
			ok = unzReadCurrentFile(z, data->data(), unsigned(data->size())) == int(data->size());
			unzCloseCurrentFile(z);
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

bool RomCrc32(const std::string& path, uint32_t* whole, uint32_t* body, std::string* inner_ext)
{
	std::string ext = ExtOf(path);
	std::vector<uint8_t> data;
	System sys;
	if (ext == ".zip")
	{
		// the zip keeps its ROM's CRC: read only when a 512-byte copier header has to come off for the body's
		std::string inner;
		uint32_t stored = 0;
		if (StoredZipCrc(path, &inner, &stored))
		{
			if (inner_ext)
				*inner_ext = ExtOf(inner);
			*whole = *body = stored;
			return true;
		}
		if (!ReadFromZip(path, &inner, &data))
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
		const size_t n = fread(data.data(), 1, data.size(), f);
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

void PrepareFolders()
{
	const std::string roms = OrbisDir("roms"), covers = OrbisDir("covers");
	for (const SystemInfo& s : kSystems)
	{
		OrbisMkdirs(roms + "/" + s.folder);
		OrbisMkdirs(covers + "/" + s.folder);
	}
}

std::vector<GameInfo> ScanGames(const ScanProgress& progress)
{
	PrepareFolders();
	std::vector<GameInfo> games;
	CrcCache cache;
	cache.Load();
	for (const std::string& root : OrbisRomRoots())
		Walk(root, 0, root.rfind("/mnt/", 0) == 0, games, cache, progress);

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
			if (CachedCrc(cache, g.path, &whole, &body))
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
	cache.Save();

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
