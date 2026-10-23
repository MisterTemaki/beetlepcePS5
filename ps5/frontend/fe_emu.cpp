// Beetle PCE PS5 frontend: the core and its libretro callbacks (fe_emu.h).
//
// Frame pacing, as in Snes9x PS5 and Genesis Plus GX PS5:
//   - The PC Engine runs at 59.83 Hz (7.16 MHz / 455 / 263): Present waits for the flip, so the console's vsync
//     paces the emulation, and the resampler (44.1 -> 48 kHz) stretches the sound by up to 0.5% to keep about
//     60 ms queued.
//   - If more than 120 ms is queued (vsync not blocking for some reason), wait on audio.
//   - Fast forward (hold R2): several frames per flip, no sound.
//
// SPDX-License-Identifier: MIT

#include "fe_emu.h"

#include "ProsperoNotify.h"
#include "fe_games.h"
#include "fe_i18n.h"
#include "fe_settings.h"
#include "fe_text.h"

#include "OrbisPaths.h"
#include "ProsperoAudio.h"
#include "ProsperoCrash.h"
#include "ProsperoInput.h"
#include "ProsperoSce.h"
#include "ProsperoVideo.h"

#include "libretro.h"
#include "zlib.h"

#include "netfs.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <vector>

namespace emu
{
const Choice kAspects[] = {
	{"TV (the core's)", "auto"},
	{"Square pixels", "uncorrected"},
	{"4:3", "4:3"},
	{"6:5", "6:5"},
	{"16:9 (stretched)", "auto"},
};
const int kAspectCount = int(sizeof(kAspects) / sizeof(kAspects[0]));

const Choice kPalettes[] = {
	{"RGB", "RGB"},
	{"Composite (TV colours)", "Composite"},
};
const int kPaletteCount = int(sizeof(kPalettes) / sizeof(kPalettes[0]));

const Choice kOverscans[] = {
	{"Auto (crops the black)", "auto"},
	{"Hidden", "disabled"},
	{"Shown", "enabled"},
};
const int kOverscanCount = int(sizeof(kOverscans) / sizeof(kOverscans[0]));

const LinesChoice kLines[] = {
	{"All 240", "3", "242"},
	{"232 (Mednafen's)", "4", "235"},
	{"224", "8", "231"},
};
const int kLinesCount = int(sizeof(kLines) / sizeof(kLines[0]));

const Choice kPsgRevisions[] = {
	{"Auto (the console's)", "auto"},
	{"HuC6280 (PC Engine)", "HuC6280"},
	{"HuC6280A (later models)", "HuC6280A"},
};
const int kPsgRevisionCount = int(sizeof(kPsgRevisions) / sizeof(kPsgRevisions[0]));

const Choice kOverclocks[] = {
	{"Off", "1"},
	{"2x", "2"},
	{"3x", "3"},
	{"4x", "4"},
};
const int kOverclockCount = int(sizeof(kOverclocks) / sizeof(kOverclocks[0]));

const Choice kCdSpeeds[] = {
	{"1x (the real drive)", "1"},
	{"2x", "2"},
	{"4x", "4"},
	{"8x", "8"},
};
const int kCdSpeedCount = int(sizeof(kCdSpeeds) / sizeof(kCdSpeeds[0]));

// value = the file in bios/ ("" = the best one there)
const Choice kCdBioses[] = {
	{"Auto (the best one in bios/)", ""},
	{"System Card 3.0", "syscard3.pce"},
	{"System Card 3.0 (US)", "syscard3u.pce"},
	{"System Card 2.0", "syscard2.pce"},
	{"System Card 2.0 (US)", "syscard2u.pce"},
	{"System Card 1.0", "syscard1.pce"},
};
const int kCdBiosCount = int(sizeof(kCdBioses) / sizeof(kCdBioses[0]));

const Choice kPadTypes[] = {
	{"2 buttons", "2 Buttons"},
	{"6 buttons (Avenue Pad 6)", "6 Buttons"},
};
const int kPadTypeCount = int(sizeof(kPadTypes) / sizeof(kPadTypes[0]));

const Choice kTurboModes[] = {
	{"Off", "disabled"},
	{"III / IV switch I / II turbo", "toggle"},
	{"III / IV are turbo I / II", "always"},
};
const int kTurboModeCount = int(sizeof(kTurboModes) / sizeof(kTurboModes[0]));

const Choice kTurboSpeeds[] = {
	{"Fast", "Fast"},
	{"Medium", "Medium"},
	{"Slow", "Slow"},
};
const int kTurboSpeedCount = int(sizeof(kTurboSpeeds) / sizeof(kTurboSpeeds[0]));

const int kVolumeSteps[] = {0, 25, 50, 75, 100, 125, 150, 175, 200};
const int kVolumeStepCount = int(sizeof(kVolumeSteps) / sizeof(kVolumeSteps[0]));

const PadButton kPs5Buttons[] = {
	{"Cross", SCE_PAD_BUTTON_CROSS},
	{"Circle", SCE_PAD_BUTTON_CIRCLE},
	{"Square", SCE_PAD_BUTTON_SQUARE},
	{"Triangle", SCE_PAD_BUTTON_TRIANGLE},
	{"L1", SCE_PAD_BUTTON_L1},
	{"R1", SCE_PAD_BUTTON_R1},
	{"OPTIONS", SCE_PAD_BUTTON_OPTIONS},
	{"Touchpad", SCE_PAD_BUTTON_TOUCH_PAD},
	{"None", 0},
};
const int kPs5ButtonCount = int(sizeof(kPs5Buttons) / sizeof(kPs5Buttons[0]));

// Beetle PCE's RetroPad layout (libretro.cpp, update_input): A = I, B = II, Y = III, X = IV, L = V, R = VI,
// Select, Start = Run. By position on the PS5 pad, as RetroArch maps it: II on Cross, I on Circle (the PC Engine
// pad has II on the left, I on the right), III / IV on Square / Triangle, V / VI on L1 / R1.
const ConsoleButton kConsoleButtons[kConsoleButtonCount] = {
	{"I", "btn_1", RETRO_DEVICE_ID_JOYPAD_A, 1},
	{"II", "btn_2", RETRO_DEVICE_ID_JOYPAD_B, 0},
	{"III  (6-button pad)", "btn_3", RETRO_DEVICE_ID_JOYPAD_Y, 2},
	{"IV  (6-button pad)", "btn_4", RETRO_DEVICE_ID_JOYPAD_X, 3},
	{"V  (6-button pad)", "btn_5", RETRO_DEVICE_ID_JOYPAD_L, 4},
	{"VI  (6-button pad)", "btn_6", RETRO_DEVICE_ID_JOYPAD_R, 5},
	{"Select", "btn_select", RETRO_DEVICE_ID_JOYPAD_SELECT, 7},
	{"Run", "btn_run", RETRO_DEVICE_ID_JOYPAD_START, 6},
};
} // namespace emu

namespace
{
using fe::T;

// printf into a std::string (the translated formats)
std::string Fmt(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string Fmt(const char* fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return buf;
}
constexpr int kTargetQueued = ps5audio::kRate * 60 / 1000; // 60 ms of sound
constexpr int kRewindEvery = 3; // frames between rewind snapshots
constexpr size_t kRewindBytes = 192u * 1024 * 1024;

double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

std::string Lower(std::string s)
{
	for (char& c : s)
		c = char(tolower(uint8_t(c)));
	return s;
}

std::string ExtOf(const std::string& p)
{
	const size_t dot = p.find_last_of('.');
	const size_t slash = p.find_last_of('/');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "";
	return Lower(p.substr(dot));
}

bool FileExists(const std::string& p)
{
	struct stat st = {};
	return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	out.clear();
	uint8_t buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		out.insert(out.end(), buf, buf + n);
	fclose(f);
	return true;
}

// data -> path.part, flushed to the disk (fsync) and closed without error, then renamed over path: the old file or
// the whole new one, never a cut one (a full disk or an I/O error shows up at fflush / fsync / fclose).
bool WriteFileAtomic(const std::string& path, const void* data, size_t size)
{
	const std::string tmp = path + ".part";
	FILE* f = fopen(tmp.c_str(), "wb");
	if (!f)
		return false;
	bool ok = fwrite(data, 1, size, f) == size;
	ok = fflush(f) == 0 && ok;
	ok = fsync(fileno(f)) == 0 && ok;
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp.c_str(), path.c_str()) != 0)
	{
		remove(tmp.c_str());
		return false;
	}
	return true;
}

uint64_t Fnv(const uint8_t* p, size_t n)
{
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 1099511628211ull;
	return h;
}

struct State
{
	bool inited = false;
	bool loaded = false;
	std::string path, name, dir, ext; // the game; ext without the dot
	fe::System system = fe::System::Pce;
	std::vector<uint8_t> rom; // a HuCard, read here (from its file, a .zip or a .7z) and put right when reversed
	retro_game_info_ext info_ext = {};
	std::string sys_dir, save_dir;

	// the core's options, as libretro variables
	std::map<std::string, std::string> opts;
	bool opts_changed = false;

	// video
	unsigned pixel_format = RETRO_PIXEL_FORMAT_0RGB1555; // Beetle PCE asks for XRGB8888 (WANT_32BPP)
	std::vector<uint32_t> frame;
	int fw = 0, fh = 0;
	bool new_frame = false;
	double fps = 60.0, core_aspect = 4.0 / 3.0;
	std::string bios; // the System Card a CD game started with
	double sample_rate = 44100.0;

	// sound: the core's samples of this frame, and the resampler's state
	std::vector<int16_t> in;
	bool mute = false; // fast forward, rewind: drop the sound
	double frac = 0;
	int16_t prev_l = 0, prev_r = 0;
	std::vector<int16_t> out;
	std::vector<int16_t> silence; // the zeros that refill a dry ring

	// pads
	ps5input::PadState pads[ps5input::kMaxPads];
	uint32_t prev_p1 = 0;
	bool wait_release = true;

	// fast forward, rewind
	bool turbo = false, rewinding = false;
	std::deque<std::vector<uint8_t>> rewind;
	size_t rewind_bytes = 0;
	size_t state_size = 0; // retro_serialize_size() of the running game, 0 = not asked yet
	int rewind_tick = 0;

	// saves
	uint64_t sram_hash = 0;
	size_t sram_size = 0; // the backup RAM's size (2 KiB; 32 KiB for Populous's card)
	double next_sram_check = 0;

	// messages and the FPS counter
	std::string osd;
	double osd_until = 0;
	bool overlay_was_drawn = false;
	int fps_frames = 0;
	double fps_since = 0, fps_shown = 0;
	uint64_t frames = 0;
	double next_stats = 0;
	int stats_reports = 0;
};
State g;

// ---- libretro callbacks -------------------------------------------------------------------------------------
void CoreLog(enum retro_log_level level, const char* fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	size_t n = strlen(buf);
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
		buf[--n] = 0;
	static const char* const kLevel[] = {"debug", "info", "warn", "error"};
	if (level == RETRO_LOG_DEBUG)
		return;
	OrbisLog("[core] %s%s%s", level == RETRO_LOG_INFO ? "" : kLevel[level < 4 ? level : 3],
		level == RETRO_LOG_INFO ? "" : ": ", buf);
}

bool Environment(unsigned cmd, void* data)
{
	switch (cmd)
	{
		case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
			*static_cast<const char**>(data) = g.sys_dir.c_str();
			return true;
		case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
			*static_cast<const char**>(data) = g.save_dir.c_str();
			return true;
		case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		{
			const unsigned f = *static_cast<const unsigned*>(data);
			if (f != RETRO_PIXEL_FORMAT_RGB565 && f != RETRO_PIXEL_FORMAT_0RGB1555 && f != RETRO_PIXEL_FORMAT_XRGB8888)
				return false;
			g.pixel_format = f;
			return true;
		}
		case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
			static_cast<retro_log_callback*>(data)->log = CoreLog;
			return true;
		case RETRO_ENVIRONMENT_GET_VARIABLE:
		{
			retro_variable* v = static_cast<retro_variable*>(data);
			auto it = v->key ? g.opts.find(v->key) : g.opts.end();
			if (it == g.opts.end())
			{
				v->value = nullptr;
				return false;
			}
			v->value = it->second.c_str();
			return true;
		}
		case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
			*static_cast<bool*>(data) = g.opts_changed;
			g.opts_changed = false;
			return true;
		case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
			*static_cast<unsigned*>(data) = 0; // the core then uses SET_VARIABLES (accepted below)
			return true;
		case RETRO_ENVIRONMENT_GET_CAN_DUPE:
			*static_cast<bool*>(data) = true;
			return true;
		case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
			return true;
		case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
			if (!g.loaded && g.path.empty())
				return false;
			*static_cast<const retro_game_info_ext**>(data) = &g.info_ext;
			return true;
		case RETRO_ENVIRONMENT_SET_GEOMETRY:
		{
			const retro_game_geometry* geo = static_cast<const retro_game_geometry*>(data);
			if (geo->aspect_ratio > 0.1f)
				g.core_aspect = geo->aspect_ratio;
			return true;
		}
		case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
		{
			const retro_system_av_info* av = static_cast<const retro_system_av_info*>(data);
			if (av->geometry.aspect_ratio > 0.1f)
				g.core_aspect = av->geometry.aspect_ratio;
			if (av->timing.fps > 1)
				g.fps = av->timing.fps;
			if (av->timing.sample_rate > 1000)
				g.sample_rate = av->timing.sample_rate;
			OrbisLog("[emu] av info: %.3f fps, %.0f Hz", g.fps, g.sample_rate);
			return true;
		}
		case RETRO_ENVIRONMENT_SET_MESSAGE:
		{
			const retro_message* m = static_cast<const retro_message*>(data);
			if (m && m->msg)
				emu::Osd(m->msg);
			return true;
		}
		// accepted and ignored
		case RETRO_ENVIRONMENT_SET_VARIABLES:
		case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
		case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
		case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
		case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
		case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
		case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
		case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
		case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
		case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
		case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
			return true;
		default:
			return false;
	}
}

inline uint32_t Expand565(uint16_t c)
{
	const uint32_t r = (c >> 11) & 31, gg = (c >> 5) & 63, b = c & 31;
	return 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((gg << 2) | (gg >> 4)) << 8) | ((b << 3) | (b >> 2));
}

inline uint32_t Expand555(uint16_t c)
{
	const uint32_t r = (c >> 10) & 31, gg = (c >> 5) & 31, b = c & 31;
	return 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((gg << 3) | (gg >> 2)) << 8) | ((b << 3) | (b >> 2));
}

void Video(const void* data, unsigned w, unsigned h, size_t pitch)
{
	if (!data || w == 0 || h == 0)
		return; // a duplicated frame: keep the last one
	g.fw = int(w);
	g.fh = int(h);
	g.frame.resize(size_t(w) * h);
	const uint8_t* src = static_cast<const uint8_t*>(data);
	for (unsigned y = 0; y < h; y++)
	{
		uint32_t* out = g.frame.data() + size_t(y) * w;
		if (g.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888)
		{
			const uint32_t* in = reinterpret_cast<const uint32_t*>(src + y * pitch);
			for (unsigned x = 0; x < w; x++)
				out[x] = in[x] | 0xff000000u;
		}
		else
		{
			const uint16_t* in = reinterpret_cast<const uint16_t*>(src + y * pitch);
			if (g.pixel_format == RETRO_PIXEL_FORMAT_RGB565)
				for (unsigned x = 0; x < w; x++)
					out[x] = Expand565(in[x]);
			else
				for (unsigned x = 0; x < w; x++)
					out[x] = Expand555(in[x]);
		}
	}
	g.new_frame = true;
}

void AudioSample(int16_t l, int16_t r)
{
	if (g.mute)
		return;
	g.in.push_back(l);
	g.in.push_back(r);
}

size_t AudioBatch(const int16_t* data, size_t frames)
{
	if (!g.mute)
		g.in.insert(g.in.end(), data, data + frames * 2);
	return frames;
}

void InputPoll()
{
	ps5input::Poll();
	for (int p = 0; p < ps5input::kMaxPads; p++)
		g.pads[p] = ps5input::Snapshot(p);
}

// The RetroPad Beetle PCE reads, from the player's button layout (Settings, CONTROLS; by position by default:
// Circle/Cross = I/II, Square/Triangle = III/IV, L1/R1 = V/VI, touchpad = Select, OPTIONS = Run). L2 (the core's
// 2/6-button switch) is never passed: it is the hot-key modifier, and the pad type is a setting.
uint16_t RetroPadBits(const ps5input::PadState& p, bool dpad_off)
{
	if (!p.connected)
		return 0;
	const fe::Settings& cfg = fe::Config();
	uint16_t bits = 0;
	for (int i = 0; i < emu::kConsoleButtonCount; i++)
	{
		const int b = cfg.buttons[i];
		const unsigned bit = b >= 0 && b < emu::kPs5ButtonCount ? emu::kPs5Buttons[b].bit : 0;
		if (bit && (p.raw_buttons & bit))
			bits |= uint16_t(1u << emu::kConsoleButtons[i].retro_id);
	}
	struct Dir
	{
		unsigned id;
		uint32_t bit;
	};
	static const Dir kDirs[] = {
		{RETRO_DEVICE_ID_JOYPAD_UP, SCE_PAD_BUTTON_UP},
		{RETRO_DEVICE_ID_JOYPAD_DOWN, SCE_PAD_BUTTON_DOWN},
		{RETRO_DEVICE_ID_JOYPAD_LEFT, SCE_PAD_BUTTON_LEFT},
		{RETRO_DEVICE_ID_JOYPAD_RIGHT, SCE_PAD_BUTTON_RIGHT},
	};
	if (!dpad_off)
		for (const Dir& d : kDirs)
			if (p.buttons & d.bit) // the D-pad includes the left stick
				bits |= uint16_t(1u << d.id);
	return bits;
}

int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
	if (port >= unsigned(ps5input::kMaxPads) || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD || index != 0)
		return 0;
	// L2 is the hot-key modifier for player 1: the D-pad stays with the hot keys while it is held.
	// While the buttons that closed a menu (Cross, Circle) or opened it (L3 + R3) are still held, player 1's pad
	// is not passed on: Resume with Cross must not press B in the game.
	if (port == 0 && g.wait_release)
		return 0;
	const bool l2 = port == 0 && (g.pads[0].raw_buttons & SCE_PAD_BUTTON_L2) != 0;
	const uint16_t bits = RetroPadBits(g.pads[port], l2);
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
		return int16_t(bits);
	return id < 16 ? int16_t((bits >> id) & 1) : 0;
}

// ---- sound: the core's 44.1 kHz -> 48 kHz, the rate steered by the ring's fill ---------------------------------
void FlushAudio()
{
	const size_t frames = g.in.size() / 2;
	if (frames == 0)
		return;
	const fe::Settings& cfg = fe::Config();
	if (!cfg.audio || g.mute || !ps5audio::Available())
	{
		g.in.clear();
		return;
	}
	int queued = ps5audio::Queued();
	if (queued < kTargetQueued / 4)
	{
		// the ring ran (nearly) dry: a game just started, or came back from a menu, or the frame was late. Fill
		// it with silence up to the target at once; the rate control alone (0.5%) would take ~10 s to get there,
		// and play crackling meanwhile.
		g.silence.assign(size_t(kTargetQueued - queued) * 2, 0);
		ps5audio::Push(g.silence.data(), kTargetQueued - queued);
		queued = kTargetQueued;
	}
	double adj = double(kTargetQueued - queued) / kTargetQueued;
	adj = 1.0 + std::max(-1.0, std::min(1.0, adj)) * 0.005;
	const double step = g.sample_rate / (ps5audio::kRate * adj); // input frames per output frame
	const int vol = cfg.volume * 256 / 100;
	g.out.clear();
	double pos = g.frac; // position in [prev, in...]: 0 = prev, 1 = in[0]
	while (pos < double(frames))
	{
		const int i = int(pos);
		const int t = int((pos - i) * 256);
		const int16_t l0 = i == 0 ? g.prev_l : g.in[size_t(i - 1) * 2];
		const int16_t r0 = i == 0 ? g.prev_r : g.in[size_t(i - 1) * 2 + 1];
		const int16_t l1 = g.in[size_t(i) * 2];
		const int16_t r1 = g.in[size_t(i) * 2 + 1];
		const int l = (l0 * (256 - t) + l1 * t) >> 8;
		const int r = (r0 * (256 - t) + r1 * t) >> 8;
		g.out.push_back(int16_t(std::max(-32768, std::min(32767, l * vol >> 8))));
		g.out.push_back(int16_t(std::max(-32768, std::min(32767, r * vol >> 8))));
		pos += step;
	}
	g.frac = pos - double(frames);
	g.prev_l = g.in[(frames - 1) * 2];
	g.prev_r = g.in[(frames - 1) * 2 + 1];
	g.in.clear();
	ps5audio::Push(g.out.data(), int(g.out.size() / 2));
}

// ---- saves ------------------------------------------------------------------------------------------------
// Saves are kept per system (saves/PCEngine/<game>.srm, states/PCEngineCD/<game>.state3...), so a HuCard and a CD
// of the same name never share a backup RAM or a state.
std::string SaveDir(const char* kind)
{
	return OrbisDir(kind) + "/" + fe::Info(g.system).folder;
}

std::string SramPath()
{
	return SaveDir("saves") + "/" + g.name + ".srm";
}

std::string StatePath(int slot)
{
	return SaveDir("states") + "/" + g.name + ".state" + std::to_string(slot);
}

void LoadSram()
{
	// The backup RAM (BRAM): the CD system's 2 KiB (the System Card's, as on a PC Engine Duo), which HuCards reach
	// through a Tennokoe Bank or a CD unit too -- or the 32 KiB on Populous's card.
	uint8_t* mem = static_cast<uint8_t*>(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
	const size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	g.sram_size = mem ? size : 0;
	g.sram_hash = mem && size ? Fnv(mem, size) : 0;
	if (!mem || !size)
		return;
	std::vector<uint8_t> data;
	if (ReadFile(SramPath(), data) && !data.empty())
	{
		memcpy(mem, data.data(), std::min(size, data.size()));
		g.sram_hash = Fnv(mem, size);
		OrbisLog("[emu] battery save loaded: %s (%zu bytes)", SramPath().c_str(), data.size());
	}
}

// Writes the battery save when it changed (or when `force`).
void SaveSram(bool force)
{
	uint8_t* mem = static_cast<uint8_t*>(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
	const size_t size = g.sram_size;
	if (!mem || !size)
		return;
	const uint64_t h = Fnv(mem, size);
	if (h == g.sram_hash && !force)
		return;
	if (h == g.sram_hash && !FileExists(SramPath()))
	{
		// nothing written by the game yet: don't create a file of zeros
		bool blank = true;
		for (size_t i = 0; i < size && blank; i++)
			blank = mem[i] == 0x00 || mem[i] == 0xff;
		if (blank)
			return;
	}
	const bool ok = WriteFileAtomic(SramPath(), mem, size);
	if (ok)
		g.sram_hash = h; // a failed write is tried again 3 s later and when the game closes
	OrbisLog("[emu] battery save -> %s (%s)", SramPath().c_str(), ok ? "ok" : "failed");
}

// The System Card for a CD game: the one chosen in the settings, or the best one in bios/ -- 3.0 runs CD-ROM²,
// Super CD-ROM² and (with the Arcade Card the core emulates) Arcade CD games; 2.0 and 1.0 only CD-ROM² ones. ""
// when there is none. Games Express discs use gexpress.pce, which the core picks itself.
std::string CdBios()
{
	const fe::Settings& cfg = fe::Config();
	const char* chosen = emu::kCdBioses[cfg.cd_bios % emu::kCdBiosCount].value;
	if (*chosen)
		return FileExists(g.sys_dir + "/" + chosen) ? chosen : "";
	for (int i = 1; i < emu::kCdBiosCount; i++)
		if (FileExists(g.sys_dir + "/" + emu::kCdBioses[i].value))
			return emu::kCdBioses[i].value;
	return "";
}

const char* CdBiosOption(const std::string& file)
{
	static const char* const kNames[][2] = {{"syscard3.pce", "System Card 3"}, {"syscard3u.pce", "System Card 3 US"},
		{"syscard2.pce", "System Card 2"}, {"syscard2u.pce", "System Card 2 US"}, {"syscard1.pce", "System Card 1"}};
	for (const auto& n : kNames)
		if (file == n[0])
			return n[1];
	return "System Card 3";
}

void ShowOverlays(ps5video::Rect* damage)
{
	const fe::Settings& cfg = fe::Config();
	const double now = Now();
	bool drawn = false;
	if (cfg.show_fps)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%.1f fps", g.fps_shown);
		const int w = fe::TextWidth(buf, 3);
		ps5video::FillRect(ps5video::kWidth - w - 60, 20, w + 40, 50, ps5video::Rgb(0, 0, 0));
		fe::DrawText(ps5video::kWidth - w - 40, 28, buf, 3, ps5video::Rgb(255, 255, 255));
		drawn = true;
	}
	if (!g.osd.empty() && now < g.osd_until)
	{
		const std::string text = fe::FitText(g.osd, 3, ps5video::kWidth - 160);
		const int w = fe::TextWidth(text.c_str(), 3);
		ps5video::DarkenRect(40, ps5video::kHeight - 100, w + 40, 60);
		fe::DrawText(60, ps5video::kHeight - 90, text.c_str(), 3, ps5video::Rgb(255, 255, 255));
		drawn = true;
	}
	if (drawn || g.overlay_was_drawn)
		*damage = ps5video::Rect{0, 0, ps5video::kWidth, ps5video::kHeight};
	if (!drawn && g.overlay_was_drawn)
		ps5video::InvalidateFrame(); // clear what the overlay left outside the picture
	g.overlay_was_drawn = drawn;
}

// The core works the aspect out from the game's dot clock (pce_aspect_ratio): only 16:9 is the frontend's.
double FrameAspect()
{
	const fe::Settings& cfg = fe::Config();
	return cfg.aspect % emu::kAspectCount == 4 ? 16.0 / 9.0 : g.core_aspect;
}

ps5video::Rect DrawLast()
{
	if (g.frame.empty())
		return ps5video::Rect{0, 0, 0, 0};
	const fe::Settings& cfg = fe::Config();
	return ps5video::DrawFrame(g.frame.data(), g.fw, g.fh, g.fh, FrameAspect(), ps5video::Scale(cfg.scale), cfg.smooth,
		cfg.scanlines, cfg.shader);
}

// A snapshot every few frames. Mednafen measures a state by writing one (retro_serialize_size), so the size is
// taken once per game and asked again only if a snapshot doesn't fit; the oldest snapshot's buffer is reused once
// the budget is full, so a long session doesn't allocate at every snapshot.
void RewindPush()
{
	if (g.state_size == 0)
		g.state_size = retro_serialize_size();
	if (g.state_size == 0)
		return;
	std::vector<uint8_t> s;
	if (g.rewind_bytes + g.state_size > kRewindBytes && !g.rewind.empty())
	{
		g.rewind_bytes -= g.rewind.front().size();
		s = std::move(g.rewind.front());
		g.rewind.pop_front();
	}
	s.resize(g.state_size);
	if (!retro_serialize(s.data(), s.size()))
	{
		g.state_size = retro_serialize_size(); // grew: once more at the new size
		s.resize(g.state_size);
		if (g.state_size == 0 || !retro_serialize(s.data(), s.size()))
			return;
	}
	g.rewind_bytes += s.size();
	g.rewind.push_back(std::move(s));
	while (g.rewind_bytes > kRewindBytes && !g.rewind.empty())
	{
		g.rewind_bytes -= g.rewind.front().size();
		g.rewind.pop_front();
	}
}

void RewindClear()
{
	g.rewind.clear();
	g.rewind_bytes = 0;
	g.rewind_tick = 0;
}
} // namespace

namespace emu
{
bool InitCore()
{
	if (g.inited)
		return true;
	PCEPS5_STAGE(Emu, "init");
	g.sys_dir = OrbisDir("bios");
	g.save_dir = OrbisDir("saves");
	ApplySettings();
	retro_set_environment(Environment);
	retro_set_video_refresh(Video);
	retro_set_audio_sample(AudioSample);
	retro_set_audio_sample_batch(AudioBatch);
	retro_set_input_poll(InputPoll);
	retro_set_input_state(InputState);
	retro_init();
	retro_system_info info = {};
	retro_get_system_info(&info);
	OrbisLog("[emu] %s %s ready (libretro API %u; BIOS folder %s)", info.library_name, info.library_version,
		retro_api_version(), g.sys_dir.c_str());
	g.inited = true;
	return true;
}

void DeinitCore()
{
	if (!g.inited)
		return;
	CloseGame();
	retro_deinit();
	g.inited = false;
}

// The pads: a joypad on each of the core's five ports (the multitap's; without one only the first is read). The
// core reads nothing from a port no device was set on.
void ApplyControllers()
{
	for (unsigned port = 0; port < 5; port++)
		retro_set_controller_port_device(port, RETRO_DEVICE_JOYPAD);
}

void ApplySettings()
{
	const fe::Settings& s = fe::Config();
	auto set = [](const char* key, const std::string& value) {
		auto it = g.opts.find(key);
		if (it == g.opts.end() || it->second != value)
		{
			g.opts[key] = value;
			g.opts_changed = true;
		}
	};
	auto pct = [](int v) { return std::to_string(std::max(0, std::min(200, v))); };
	// video
	set("pce_aspect_ratio", kAspects[s.aspect % kAspectCount].value);
	set("pce_palette", kPalettes[s.palette % kPaletteCount].value);
	set("pce_h_overscan", kOverscans[s.overscan % kOverscanCount].value);
	set("pce_initial_scanline", kLines[s.lines % kLinesCount].first);
	set("pce_last_scanline", kLines[s.lines % kLinesCount].last);
	set("pce_scaling", "auto");
	set("pce_hires_blend", "disabled");
	// audio
	set("pce_psgrevision", kPsgRevisions[s.psg_revision % kPsgRevisionCount].value);
	set("pce_cddavolume", pct(s.cdda_volume));
	set("pce_adpcmvolume", pct(s.adpcm_volume));
	set("pce_cdpsgvolume", pct(s.cdpsg_volume));
	set("pce_adpcmextraprec", "12-bit");
	set("pce_resamp_quality", "3");
	// emulation
	set("pce_nospritelimit", s.no_sprite_limit ? "enabled" : "disabled");
	set("pce_ocmultiplier", kOverclocks[s.overclock % kOverclockCount].value);
	set("pce_cdspeed", kCdSpeeds[s.cd_speed % kCdSpeedCount].value);
	set("pce_arcadecard", s.arcade_card ? "enabled" : "disabled");
	set("pce_cdimagecache", "disabled");
	set("pce_disable_softreset", s.no_soft_reset ? "enabled" : "disabled");
	if (!g.bios.empty())
		set("pce_cdbios", CdBiosOption(g.bios));
	// controls
	for (int p = 1; p <= 5; p++)
		set(("pce_default_joypad_type_p" + std::to_string(p)).c_str(), kPadTypes[s.pad_type % kPadTypeCount].value);
	set("pce_multitap", s.multitap ? "enabled" : "disabled");
	set("pce_Turbo_Toggling", kTurboModes[s.turbo_mode % kTurboModeCount].value);
	set("pce_turbo_toggle_hotkey", "disabled");
	set("pce_Turbo_Delay", kTurboSpeeds[s.turbo_speed % kTurboSpeedCount].value);
	set("pce_up_down_allowed", "disabled");
	set("pce_show_advanced_input_settings", "disabled");
	set("pce_mouse_sensitivity", "1.25");
	for (int p = 0; p < 5; p++)
	{
		set(("pce_p" + std::to_string(p) + "_turbo_I_enable").c_str(), "disabled");
		set(("pce_p" + std::to_string(p) + "_turbo_II_enable").c_str(), "disabled");
	}
	if (!s.rewind)
		RewindClear();
}

// The HuCard, read here rather than by the core: from its file or from a .zip or .7z, and a TurboGrafx-16 dump with its
// data lines reversed put right (the core would start it as garbage).
bool ReadHuCard(const std::string& path, std::string* inner, std::string* error)
{
	const std::string file = path.substr(path.find_last_of('/') + 1);
	if (fe::IsArchiveExt(ExtOf(path)))
	{
		std::string why;
		if (!fe::ReadFromArchive(path, inner, &g.rom, &why))
		{
			OrbisLog("[emu] could not start %s: %s", path.c_str(), why.empty() ? "no game inside the archive" : why.c_str());
			if (error)
				*error = why.empty() ? Fmt(T("There is no game Beetle PCE PS5 knows inside %s."), file.c_str())
									 : Fmt(T("Beetle PCE PS5 could not unpack %s."), file.c_str()) + " " + T(why.c_str());
			return false;
		}
	}
	else if (!ReadFile(path, g.rom) || g.rom.empty() || g.rom.size() > 16u * 1024 * 1024)
	{
		OrbisLog("[emu] could not read %s (%zu bytes)", path.c_str(), g.rom.size());
		if (error)
			*error = Fmt(T("Beetle PCE PS5 could not read %s."), file.c_str());
		return false;
	}
	const size_t skip = fe::HuCardHeader(g.rom.size());
	if (g.rom.size() > skip && fe::IsReversedHuCard(g.rom.data() + skip, g.rom.size() - skip))
	{
		fe::FixReversedHuCard(g.rom.data() + skip, g.rom.size() - skip);
		OrbisLog("[emu] %s: a TurboGrafx-16 dump with its data lines reversed: put right", file.c_str());
	}
	return true;
}

bool LoadGame(const std::string& path, std::string* error)
{
	if (!g.inited)
		return false;
	PCEPS5_STAGE(Emu, "load game");
	CloseGame();
	const std::string file = path.substr(path.find_last_of('/') + 1);
	if (path.size() >= 1024)
	{
		// the console's PATH_MAX; the CD readers build the tracks' paths from the sheet's
		OrbisLog("[emu] could not start: the path is %zu characters long (%s)", path.size(), path.c_str()); // the length first: the log cuts long lines
		if (error)
			*error = Fmt(T("The path of %s is too long (over 1023 characters): move it to a shorter folder."), file.c_str());
		return false;
	}
	std::string ext = ExtOf(path);
	g.path = path;
	g.dir = path.substr(0, path.find_last_of('/'));
	g.name = file.substr(0, file.size() - ext.size());
	g.rom.clear();
	g.bios.clear();
	g.info_ext = retro_game_info_ext();

	// a game on a share: its folder listed first (nothing for any other path); let go again if the game doesn't start
	netfs_prepare_game(path);
	std::string inner;
	if (fe::IsArchiveExt(ext) || ext == ".pce" || ext == ".sgx")
	{
		if (!ReadHuCard(path, &inner, error))
		{
			netfs_end_game();
			g.path.clear();
			g.rom.clear();
			return false;
		}
		if (!inner.empty())
			ext = ExtOf(inner);
	}
	if (!fe::SystemForExt(ext, &g.system))
		g.system = fe::System::Pce;
	// a SuperGrafx game kept as .pce (the game table knows it): started as one, as the core does for a .sgx file
	if (g.system == fe::System::Pce && !g.rom.empty())
	{
		const size_t skip = fe::HuCardHeader(g.rom.size());
		const uint32_t crc = uint32_t(crc32(0L, g.rom.data() + skip, uInt(g.rom.size() - skip)));
		fe::System found = fe::System::Pce;
		if (!fe::gamedb::ByCrc(fe::System::Pce, crc, &found).empty() && found == fe::System::Sgx)
		{
			g.system = fe::System::Sgx;
			ext = ".sgx";
			OrbisLog("[emu] %s is a SuperGrafx game: started as one", file.c_str());
		}
	}
	g.ext = ext.empty() ? "" : ext.substr(1);

	if (g.system == fe::System::Cd)
	{
		g.bios = CdBios();
		if (g.bios.empty())
		{
			const char* chosen = kCdBioses[fe::Config().cd_bios % kCdBiosCount].value;
			OrbisLog("[emu] missing firmware: no System Card (checked before loading)");
			OrbisLog("[emu] could not start %s: no System Card in %s", path.c_str(), g.sys_dir.c_str());
			if (error)
				*error = *chosen ? Fmt(T("The System Card chosen in the settings (%s) isn't in %s/. Copy it there, or choose "
										   "Auto, and try again."),
									   chosen, g.sys_dir.c_str())
								 : Fmt(T("PC Engine CD games need a System Card: syscard3.pce (Super CD-ROM² System Card 3.0, "
										   "the one most games need). Copy it to %s/ and try again."),
									   g.sys_dir.c_str());
			netfs_end_game();
			g.path.clear();
			return false;
		}
	}

	g.info_ext.full_path = g.rom.empty() ? g.path.c_str() : nullptr;
	g.info_ext.archive_path = inner.empty() ? nullptr : g.path.c_str();
	g.info_ext.dir = g.dir.c_str();
	g.info_ext.name = g.name.c_str();
	g.info_ext.ext = g.ext.c_str();
	g.info_ext.data = g.rom.empty() ? nullptr : g.rom.data();
	g.info_ext.size = g.rom.size();
	g.info_ext.file_in_archive = !inner.empty();
	g.info_ext.persistent_data = false;

	retro_game_info gi = {};
	gi.path = g.path.c_str();
	gi.data = g.rom.empty() ? nullptr : g.rom.data();
	gi.size = g.rom.size();

	OrbisLog("[emu] loading %s%s%s%s%s", path.c_str(), inner.empty() ? "" : " -> ", inner.c_str(),
		g.bios.empty() ? "" : ", System Card ", g.bios.c_str());
	ApplySettings();
	g.opts_changed = true;
	g.frame.clear();
	g.fw = g.fh = 0;
	g.in.clear();
	g.mute = false;
	if (!retro_load_game(&gi))
	{
		netfs_end_game();
		OrbisLog("[emu] could not start %s", path.c_str());
		if (error)
		{
			if (g.system == fe::System::Cd)
				*error = Fmt(T("Beetle PCE could not start %s with %s. Check that the disc's files are complete (a .cue needs "
							   "its tracks next to it); Games Express discs need gexpress.pce in %s."),
					file.c_str(), g.bios.c_str(), g.sys_dir.c_str());
			else
				*error = Fmt(T("Beetle PCE could not load %s."), file.c_str());
		}
		g.path.clear();
		g.rom.clear();
		return false;
	}
	ApplyControllers();
	retro_system_av_info av = {};
	retro_get_system_av_info(&av);
	g.fps = av.timing.fps > 1 ? av.timing.fps : 60.0;
	g.sample_rate = av.timing.sample_rate > 1000 ? av.timing.sample_rate : 44100.0;
	g.core_aspect = av.geometry.aspect_ratio > 0.1f ? av.geometry.aspect_ratio : 4.0 / 3.0;
	g.loaded = true;
	g.wait_release = true;
	g.prev_p1 = 0;
	g.turbo = g.rewinding = false;
	g.frac = 0;
	g.prev_l = g.prev_r = 0;
	g.frames = 0;
	g.fps_frames = 0;
	g.fps_since = Now();
	g.next_stats = Now() + 1.0;
	g.stats_reports = 0;
	g.next_sram_check = Now() + 3.0;
	g.overlay_was_drawn = false;
	g.state_size = 0;
	RewindClear();
	ps5video::InvalidateFrame();
	OrbisMkdirs(SaveDir("saves"));
	OrbisMkdirs(SaveDir("states"));
	LoadSram();
	// from here nothing on this thread waits for a share: a CD game on one streams through netfs's cache (blocks read
	// ahead of the core), a HuCard was read whole above
	netfs_set_realtime(true);
	OrbisLog("[emu] running \"%s\" (%s), %.3f fps, %.0f Hz, aspect %.4f, state size %zu", g.name.c_str(),
		SystemName().c_str(), g.fps, g.sample_rate, g.core_aspect, retro_serialize_size());
	return true;
}

void CloseGame()
{
	if (!g.loaded)
		return;
	PCEPS5_STAGE(Emu, "close game");
	SaveSram(false);
	retro_unload_game();
	g.loaded = false;
	g.frame.clear();
	g.rom.clear();
	RewindClear();
	netfs_set_realtime(false);
	netfs_end_game();
	OrbisLog("[emu] game closed");
}

bool GameLoaded()
{
	return g.loaded;
}

std::string GameName()
{
	return g.name;
}

std::string SystemName()
{
	return g.loaded || !g.path.empty() ? fe::Info(g.system).name : "";
}

void Pause() {}

void Resume()
{
	ApplySettings();
	g.wait_release = true;
	ps5video::InvalidateFrame();
	g.new_frame = true;
}

void Osd(const std::string& text)
{
	g.osd = T(text.c_str());
	g.osd_until = Now() + 2.5;
}

bool SaveState(int slot)
{
	if (!g.loaded)
		return false;
	const size_t size = retro_serialize_size();
	std::vector<uint8_t> data(size);
	bool ok = size > 0 && retro_serialize(data.data(), size) && WriteFileAtomic(StatePath(slot), data.data(), size);
	OrbisLog("[emu] save state %d: %s (%zu bytes)", slot, ok ? "ok" : "failed", size);
	if (ok)
		Osd(Fmt(T("State saved to slot %d"), slot));
	else
		Osd("Could not save the state");
	return ok;
}

bool LoadState(int slot)
{
	if (!g.loaded)
		return false;
	std::vector<uint8_t> data;
	if (!ReadFile(StatePath(slot), data) || data.empty())
	{
		Osd(Fmt(T("Slot %d is empty"), slot));
		return false;
	}
	const bool ok = retro_unserialize(data.data(), data.size());
	OrbisLog("[emu] load state %d: %s", slot, ok ? "ok" : "failed");
	if (ok)
		Osd(Fmt(T("State loaded from slot %d"), slot));
	else
		Osd("Could not load the state");
	if (ok)
		RewindClear();
	return ok;
}

bool StateExists(int slot)
{
	return g.loaded && FileExists(StatePath(slot));
}

void Reset()
{
	if (!g.loaded)
		return;
	retro_reset();
	RewindClear();
	OrbisLog("[emu] reset");
}

void PowerCycle()
{
	if (!g.loaded)
		return;
	const std::string path = g.path;
	CloseGame();
	std::string error;
	if (!LoadGame(path, &error))
	{
		OrbisLog("[emu] power cycle: %s", error.c_str());
		ProsperoNotify(T("Beetle PCE PS5: the game could not be started again. %s"), error.c_str());
	}
}

void RedrawLastFrame()
{
	DrawLast();
}

FrameResult RunFrame()
{
	if (!g.loaded)
		return FrameResult::Stopped;
	fe::Settings& cfg = fe::Config();

	// -- hot keys (player 1)
	ps5input::Poll();
	const uint32_t raw = ps5input::Pad(0).raw_buttons;
	if (g.wait_release)
	{
		g.wait_release = (raw & (SCE_PAD_BUTTON_L3 | SCE_PAD_BUTTON_R3 | SCE_PAD_BUTTON_CROSS | SCE_PAD_BUTTON_CIRCLE)) != 0;
		g.prev_p1 = raw;
	}
	const uint32_t pressed = raw & ~g.prev_p1;
	g.prev_p1 = raw;
	const uint32_t menu_combo = SCE_PAD_BUTTON_L3 | SCE_PAD_BUTTON_R3;
	if ((raw & menu_combo) == menu_combo && (pressed & menu_combo))
		return FrameResult::OpenMenu;

	const bool l2 = (raw & SCE_PAD_BUTTON_L2) != 0;
	const bool r2 = (raw & SCE_PAD_BUTTON_R2) != 0;
	if (l2 && !r2)
	{
		if (pressed & SCE_PAD_BUTTON_UP)
			SaveState(cfg.state_slot);
		else if (pressed & SCE_PAD_BUTTON_DOWN)
			LoadState(cfg.state_slot);
		else if (pressed & (SCE_PAD_BUTTON_LEFT | SCE_PAD_BUTTON_RIGHT))
		{
			cfg.state_slot = (pressed & SCE_PAD_BUTTON_RIGHT) ? cfg.state_slot % 10 + 1 : (cfg.state_slot + 8) % 10 + 1;
			Osd(Fmt(T(StateExists(cfg.state_slot) ? "State slot %d (used)" : "State slot %d (empty)"), cfg.state_slot));
			OrbisLog("[emu] state slot %d", cfg.state_slot);
			cfg.Save();
		}
	}
	const bool want_rewind = cfg.rewind && l2 && r2;
	if (want_rewind != g.rewinding)
	{
		g.rewinding = want_rewind;
		OrbisLog("[emu] rewind %s (%zu snapshots, %zu MB)", want_rewind ? "on" : "off", g.rewind.size(),
			g.rewind_bytes >> 20);
	}
	const bool want_turbo = r2 && !l2;
	if (want_turbo != g.turbo)
	{
		g.turbo = want_turbo;
		OrbisLog("[emu] fast forward %s", want_turbo ? "on" : "off");
	}

	// -- emulate
	PCEPS5_STAGE(Emu, "retro_run");
	if (g.rewinding)
	{
		if (!g.rewind.empty())
		{
			const std::vector<uint8_t>& s = g.rewind.back();
			retro_unserialize(s.data(), s.size());
			g.rewind_bytes -= s.size();
			g.rewind.pop_back();
		}
		g.mute = true;
		retro_run();
		g.mute = false;
		g.in.clear();
	}
	else
	{
		const int runs = g.turbo ? (cfg.ff_speed == 0 ? 8 : std::max(2, (cfg.ff_speed + 50) / 100)) : 1;
		for (int i = 0; i < runs; i++)
		{
			g.mute = g.turbo;
			retro_run();
			g.frames++;
			g.fps_frames++;
			if (cfg.rewind && ++g.rewind_tick >= kRewindEvery)
			{
				g.rewind_tick = 0;
				RewindPush();
			}
		}
		g.mute = false;
	}
	FlushAudio();

	// -- picture
	const double now = Now();
	if (now - g.fps_since >= 1.0)
	{
		g.fps_shown = g.fps_frames / (now - g.fps_since);
		g.fps_frames = 0;
		g.fps_since = now;
	}
	const bool pal = g.fps < 55.0;
	if (g.new_frame || (!g.osd.empty() && now < g.osd_until + 0.1) || cfg.show_fps)
	{
		g.new_frame = false;
		ps5video::Rect r = DrawLast();
		ShowOverlays(&r);
		// 60 Hz games wait for the flip: the display paces them
		ps5video::Present(r.x, r.y, r.w, r.h, !pal && !g.turbo);
	}

	// -- pacing on the sound: 50 Hz games, or a ring filling up
	if (!g.turbo && !g.rewinding)
	{
		if (cfg.audio && ps5audio::Available())
		{
			// 50 Hz: the sound is the clock; 60 Hz: vsync is, and this only catches a display that doesn't block
			const int limit = pal ? kTargetQueued : kTargetQueued * 2;
			const double give_up = Now() + 0.1;
			while (ps5audio::Queued() > limit && Now() < give_up)
				usleep(1000);
		}
		else if (pal)
		{
			static double next = 0;
			if (next < now - 0.1)
				next = now;
			next += 1.0 / g.fps;
			if (next > Now())
				usleep(useconds_t((next - Now()) * 1e6));
		}
	}

	// -- backup RAM, every 3 s when it changed; the log, now and then
	if (now >= g.next_sram_check)
	{
		g.next_sram_check = now + 3.0;
		SaveSram(false);
	}
	if (now >= g.next_stats)
	{
		g.stats_reports++;
		g.next_stats = now + (g.stats_reports < 3 ? 5.0 : 60.0);
		const double shader_ms = ps5video::TakeShaderMs();
		OrbisLog("[emu] frame %llu, %.1f fps, audio queued %d (%.0f ms), underruns %llu, shader %.1f ms",
			(unsigned long long)g.frames, g.fps_shown, ps5audio::Queued(), ps5audio::Queued() * 1000.0 / ps5audio::kRate,
			(unsigned long long)ps5audio::Underruns(), shader_ms);
	}
	return FrameResult::Continue;
}
} // namespace emu
