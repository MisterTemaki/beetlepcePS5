// Beetle PCE PS5 frontend: the game library -- every ROM under the ROM folders, with its system and official name.
//
// The system comes from the file extension (inside a .zip or .7z, from the ROM in it). A game is then named the way
// libretro names it:
//   1. its file name, when it is already a No-Intro / Redump name ("Bonk's Adventure (USA).pce");
//   2. otherwise the CRC32 of the HuCard (the whole file, then without a 512-byte copier header, then -- for a
//      TurboGrafx-16 dump with its data lines reversed -- of the ROM put right), looked up in the table built into
//      the app (data/nointro.tsv: No-Intro for the HuCards, Redump names for the CDs; tools/make_gamedb.py makes it);
//   3. otherwise the file name, loosely: "bonks adventure.pce" -> the best entry with that title (USA first,
//      then World, Europe, Japan).
// HuCards and SuperGrafx cards are searched together (a SuperGrafx game saved as .pce is found, and then started
// as one); the table that knows the game decides its system, and so where its cover comes from. CD games
// (.cue, .chd, .ccd, .toc, .m3u) are named by their file name.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fe
{
enum class System : uint8_t
{
	Pce, // PC Engine / TurboGrafx-16 / CoreGrafx HuCards
	Sgx, // SuperGrafx
	Cd, // CD-ROM², Super CD-ROM², Arcade Card, TurboGrafx-CD
	Count
};

// The shelf's tabs: systems grouped the way players think of them.
enum class Family : uint8_t
{
	All,
	Pce,
	Sgx,
	Cd,
	Count
};

struct SystemInfo
{
	const char* id; // "pce": the table's tag
	const char* name; // "PC Engine"
	const char* thumbs; // the libretro-thumbnails repository
	Family family;
	const char* folder; // "PCEngine": its folder in roms/ and in covers/
};
const SystemInfo& Info(System s);
const char* FamilyName(Family f); // "All games", "PC Engine", "SuperGrafx", "PC Engine CD"

// Creates roms/<folder> and covers/<folder> for every system (PCEngine, SuperGrafx, PCEngineCD).
void PrepareFolders();

// ".pce" -> System::Pce; false for anything that isn't a game we list.
bool SystemForExt(const std::string& ext, System* out);

struct GameInfo
{
	std::string path; // full path
	std::string file_base; // file name without extension
	std::string ext; // ".pce", ".zip", ".7z"... lower case
	System system = System::Pce;
	std::string nointro; // official name, "" until known (or unknown)
	std::string title; // what the shelf shows
	std::string region; // "USA", "Europe"... from the name's tags
	bool on_usb = false;
	bool on_network = false; // on a share (/smb/..., /nfs/...): named without reading the ROM (netfs)
	bool name_by_crc = false; // the name came from the CRC (not the file name)
	bool crc_pending = false; // on a share, named from its file name only so far: NetNaming reads its CRC
};

// The table built into the app.
namespace gamedb
{
size_t Count();
// "" when unknown. *found_in: the system whose table knew it (one of `s`'s family).
std::string ByCrc(System s, uint32_t crc, System* found_in);
// Exact No-Intro name ("Bonk's Adventure (USA)") -> itself, or "".
std::string Exact(System s, const std::string& name, System* found_in);
// Loose: title without tags, any case ("bonks adventure") -> best regional entry, or "".
std::string Loose(System s, const std::string& file_base, System* found_in);
// "Bonk's Adventure (USA)" -> "Bonk's Adventure"; "Kung Fu, The (...)" -> "The Kung Fu"
std::string Title(const std::string& nointro);
std::string Region(const std::string& nointro);
} // namespace gamedb

// The ROM's CRC32s: of the whole file, and of the playable ROM -- without its copier header and, for a HuCard dumped
// with its data lines reversed, put right (= whole when neither applies). For a .zip or .7z, of the ROM inside (the
// CRC its directory keeps when no header has to come off);
// *inner_ext gets that ROM's extension. False if it can't be read (a disc image: named by file name).
// cancel (when given): stops the read when it turns true (false is returned). certain (when given): false returned for
// a reason of the file's own, read without an error (a .7z with no HuCard, encrypted, too big...): it may be remembered.
bool RomCrc32(const std::string& path, uint32_t* whole, uint32_t* body, std::string* inner_ext = nullptr,
	const std::atomic<bool>* cancel = nullptr, bool* certain = nullptr);

// The folders to look for games in: OrbisRomRoots() plus the network shares that are connected (in memory, no I/O).
std::vector<std::string> RomRoots();

// Names the games on shares by their ROM's CRC, in the background (Snes9x PS5's design, by thatboialex). A scan does
// not read network ROMs (it would make a big library crawl); this does, one at a time, only for the games with
// crc_pending (a zip: its directory, which also gives its system), and remembers each CRC in crc-cache.txt, so the
// next ScanGames names them. Add() starts or extends the pass; it stops by itself when a game starts
// (netfs_realtime) and when Stop() or the destructor is called (leaving the shelf).
class NetNaming
{
public:
	NetNaming();
	~NetNaming();
	void Add(const std::vector<GameInfo>& games);
	void Stop();
	int Resolved() const; // ROMs read so far (named or not)
	bool Busy() const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_i;
};

// Where a CD game is read first, for netfs_prefetch (a disc on a network share): the start of every file its sheet
// names (a .cue's tracks, a .toc's files, the first disc of an .m3u, a .ccd's .img and .sub, a .chd) and of every track
// inside them (a .cue's INDEX 01, a .ccd's INDEX 1), a megabyte each, in the sheet's order.
struct DiscSpan
{
	std::string path;
	uint64_t offset = 0;
	uint64_t length = 0;
};
std::vector<DiscSpan> DiscSpans(const std::string& sheet);

// HuCards: a TurboGrafx-16 card dumped with its data lines reversed (bit 0 <-> bit 7 in every byte) doesn't start
// as it is. Reports whether `rom` (the copier header already off) is one: its reset vector only points into the
// first bank once reversed. FixReversedHuCard reverses it in place.
bool IsReversedHuCard(const uint8_t* rom, size_t size);
void FixReversedHuCard(uint8_t* rom, size_t size);
// The copier header in front of a HuCard (512 bytes), or 0: Mednafen's rule, the size's 512 bit.
size_t HuCardHeader(size_t size);

// Archives of HuCards: .zip and .7z (fe_7z.h). A disc image inside an archive is not a game (it would have to be
// unpacked whole): an archive with no HuCard in it is not listed.
bool IsArchiveExt(const std::string& ext);
// The system of a .zip or .7z: the extension of the first HuCard inside, in the archive's order. False if none.
// cancel: stops the read (a .7z's); certain: as for RomCrc32.
bool ArchiveSystem(const std::string& path, System* out, const std::atomic<bool>* cancel = nullptr, bool* certain = nullptr);
// The first HuCard inside a .zip or .7z (its name in the archive) and its bytes (16 MiB at most). False if none can be
// read; *why (when given) then says why in a sentence for the player (English: T() it), for a .7z that is damaged,
// encrypted, packed with a method the app can't unpack, or too big. Left alone when there is simply no HuCard inside.
bool ReadFromArchive(const std::string& path, std::string* inner, std::vector<uint8_t>* data, std::string* why = nullptr,
	const std::atomic<bool>* cancel = nullptr, bool* certain = nullptr);

// Every ROM under the ROM folders (sub-folders included, 4 levels), sorted by title. Names that need a
// CRC are resolved here too, with a cache (/data/pce/covers/crc-cache.txt) so each ROM is read once.
// progress (when given) is told how far the scan is: stage 0 while the folders are listed (done = games found
// so far, total = 0), stage 1 while each game is identified (done of total). It is called often: it decides itself
// how often to draw.
using ScanProgress = std::function<void(int stage, size_t done, size_t total)>;
std::vector<GameInfo> ScanGames(const ScanProgress& progress = nullptr);
} // namespace fe
