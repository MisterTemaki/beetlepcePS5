// Beetle PCE PS5 frontend: the menus.
//
// Everything is drawn on the CPU into the 1920x1080 surface (fe_text: Roboto + PromptFont) and shown with
// ps5video::Present, which waits for vsync, so the menus run at 60 Hz.
//
// SPDX-License-Identifier: MIT

#include "fe_menu.h"

#include "fe_emu.h"
#include "fe_settings.h"
#include "fe_text.h"

#include "OrbisPaths.h"
#include "ProsperoJailbreak.h"
#include "ProsperoUpdate.h"
#include "ProsperoCrt.h"
#include "ProsperoInput.h"
#include "ProsperoSce.h"
#include "ProsperoVideo.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <vector>

namespace fe
{
namespace
{
using ps5video::Rgb;
constexpr int W = ps5video::kWidth;
constexpr int H = ps5video::kHeight;

const uint32_t kBg = Rgb(14, 14, 28);
const uint32_t kPanel = Rgb(26, 24, 48);
const uint32_t kAccent = Rgb(150, 125, 255);
const uint32_t kSel = Rgb(78, 62, 170);
const uint32_t kText = Rgb(235, 235, 245);
const uint32_t kDim = Rgb(150, 150, 175);

double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

// ---- input with key repeat ---------------------------------------------------------------------------
struct Nav
{
	bool up = false, down = false, left = false, right = false;
	bool ok = false, back = false, options = false, menu = false;
};

class NavReader
{
public:
	NavReader()
	{
		ps5input::Poll();
		m_prev = ps5input::Pad(0).buttons; // buttons still held from before don't count
	}
	Nav Read()
	{
		ps5input::Poll();
		const uint32_t cur = ps5input::Pad(0).buttons;
		const uint32_t down = cur & ~m_prev;
		m_prev = cur;
		const double now = Now();
		Nav n;
		n.up = Repeat(cur, down, SCE_PAD_BUTTON_UP, now, 0);
		n.down = Repeat(cur, down, SCE_PAD_BUTTON_DOWN, now, 1);
		n.left = Repeat(cur, down, SCE_PAD_BUTTON_LEFT, now, 2);
		n.right = Repeat(cur, down, SCE_PAD_BUTTON_RIGHT, now, 3);
		n.ok = down & SCE_PAD_BUTTON_CROSS;
		n.back = down & SCE_PAD_BUTTON_CIRCLE;
		n.options = down & SCE_PAD_BUTTON_OPTIONS;
		const uint32_t combo = SCE_PAD_BUTTON_L3 | SCE_PAD_BUTTON_R3;
		n.menu = (cur & combo) == combo && (down & combo);
		return n;
	}

private:
	bool Repeat(uint32_t cur, uint32_t down, uint32_t bit, double now, int i)
	{
		if (down & bit)
		{
			m_next[i] = now + 0.40;
			return true;
		}
		if ((cur & bit) && now >= m_next[i])
		{
			m_next[i] = now + 0.07;
			return true;
		}
		return false;
	}
	uint32_t m_prev = 0;
	double m_next[4] = {};
};

void Present()
{
	ps5video::Present(0, 0, 0, 0, true);
}

void Header(const char* subtitle)
{
	ps5video::FillRect(0, 0, W, H, kBg);
	ps5video::FillRect(0, 0, W, 150, kPanel);
	ps5video::FillRect(0, 150, W, 4, kAccent);
	DrawText(80, 34, "Beetle PCE PS5", 6, kAccent);
	const char* ver = "Beetle PCE " PCEPS5_CORE_VERSION " - PS5 " PCEPS5_VERSION;
	DrawText(W - 80 - TextWidth(ver, 2), 40, ver, 2, kDim);
	if (subtitle && *subtitle)
		DrawText(84, 104, FitText(subtitle, 3, W - 168).c_str(), 3, kDim);
}

void Footer(const std::string& help)
{
	ps5video::FillRect(0, H - 80, W, 80, kPanel);
	DrawText(80, H - 60, help.c_str(), 3, kDim);
}

// A box of label/value rows (pause menu, settings); a row with header=true is a section title.
struct Row
{
	std::string label;
	std::string value; // "" = an action
	bool enabled = true;
	bool header = false;
};

void DrawOptionBox(const std::string& title, const std::vector<Row>& rows, int sel, bool over_game)
{
	const int scale = 3;
	const int row_h = 46;
	const int visible = std::min(int(rows.size()), 16);
	const int bw = 1180;
	const int bh = 120 + visible * row_h + 30;
	const int bx = (W - bw) / 2;
	const int by = std::max(20, (H - bh) / 2);
	int top = 0;
	if (int(rows.size()) > visible)
		top = std::max(0, std::min(int(rows.size()) - visible, sel - visible / 2));
	if (!over_game)
		ps5video::FillRect(bx - 4, by - 4, bw + 8, bh + 8, kAccent);
	ps5video::FillRect(bx, by, bw, bh, kPanel);
	ps5video::FillRect(bx, by + 86, bw, 3, kAccent);
	DrawText(bx + 40, by + 24, FitText(title, 5, bw - 80).c_str(), 5, kAccent);
	for (int i = 0; i < visible; i++)
	{
		const Row& row = rows[size_t(top + i)];
		const int y = by + 110 + i * row_h;
		if (row.header)
		{
			DrawText(bx + 40, y + 6, row.label.c_str(), 2, kAccent);
			ps5video::FillRect(bx + 40, y + 34, bw - 80, 1, kSel);
			continue;
		}
		if (top + i == sel)
			ps5video::FillRect(bx + 16, y - 6, bw - 32, row_h, kSel);
		const uint32_t col = row.enabled ? kText : kDim;
		DrawText(bx + 40, y, row.label.c_str(), scale, col);
		if (!row.value.empty())
		{
			const std::string v = (top + i == sel ? "< " + row.value + " >" : row.value);
			DrawText(bx + bw - 40 - TextWidth(v.c_str(), scale), y, v.c_str(), scale, top + i == sel ? kText : kAccent);
		}
	}
	if (int(rows.size()) > visible)
	{
		const int track = visible * row_h;
		const int thumb = std::max(30, track * visible / int(rows.size()));
		const int ty = by + 104 + (track - thumb) * top / std::max(1, int(rows.size()) - visible);
		ps5video::FillRect(bx + bw - 12, ty, 6, thumb, kAccent);
	}
}

int Step(int sel, int dir, const std::vector<Row>& rows)
{
	const int n = int(rows.size());
	for (int i = 0; i < n; i++)
	{
		sel = (sel + dir + n) % n;
		if (!rows[size_t(sel)].header)
			return sel;
	}
	return sel;
}

const char* OnOff(bool b)
{
	return b ? "On" : "Off";
}

// ---- settings ----------------------------------------------------------------------------------------
enum SettingRow
{
	H_VIDEO,
	S_SHADER,
	S_SCALE,
	S_ASPECT,
	S_PALETTE,
	S_OVERSCAN,
	S_LINES,
	S_SMOOTH,
	S_SCANLINES,
	S_FPS,
	H_AUDIO,
	S_AUDIO,
	S_VOLUME,
	S_PSG,
	S_CDDA,
	S_ADPCM,
	S_CDPSG,
	H_EMU,
	S_FF,
	S_REWIND,
	S_SPRITES,
	S_OVERCLOCK,
	S_CDSPEED,
	S_CDBIOS,
	S_ARCADE,
	S_SOFTRESET,
	H_CONTROLS,
	S_PADTYPE,
	S_MULTITAP,
	S_TURBO,
	S_TURBOSPEED,
	S_BTN_FIRST,
	S_BTN_LAST = S_BTN_FIRST + emu::kConsoleButtonCount - 1,
	S_BTN_DEFAULT,
	H_LIBRARY,
	S_COVERS,
	H_SYSTEM,
	S_DEBUGLOGS,
	S_UPDATES,
	S_COUNT
};

// What a setting row says at the bottom of the screen while it is selected ("" = nothing).
const char* SettingHelp(int s)
{
	switch (s)
	{
		case S_PSG: return "The PC Engine's sound chip, as the first models (HuC6280) or later ones had it. From the next game.";
		case S_CDDA:
		case S_ADPCM:
		case S_CDPSG: return "CD games: the CD's music, the ADPCM voices and the console's own sound, mixed as you like.";
		case S_OVERCLOCK: return "Runs the CPU faster: less slowdown in some games, but others run too fast or break.";
		case S_CDSPEED: return "Loads CD games faster than the real drive. Some games break above 1x.";
		case S_CDBIOS:
			return "The card CD games start with, from /data/pce/bios. Auto takes syscard3.pce first (Super CD-ROM², "
				   "Arcade CD). From the next game.";
		case S_ARCADE: return "The Arcade Card (Duo), for Arcade CD games; the others don't see it. From the next game.";
		case S_SOFTRESET: return "Run + Select resets a PC Engine game: turn this on so it can't happen by accident.";
		case S_PADTYPE: return "6 buttons (Avenue Pad 6) for the games that use it (Street Fighter II', CD fighting games).";
		case S_MULTITAP: return "The TurboTap: up to five players (players 2 to 4 are the other signed-in users).";
		case S_TURBO: return "The PC Engine pad's turbo switches, on the 2-button pad.";
		case S_LINES: return "The lines of the picture shown: most games use 224 to 232 of the 240.";
		default: return "";
	}
}

const int kFfSteps[] = {150, 200, 300, 400, 500, 1000, 0};
constexpr int kFfCount = int(sizeof(kFfSteps) / sizeof(kFfSteps[0]));

// ---- updates (ProsperoUpdate.h): the helper checks, downloads and installs; this screen shows it ----------------
struct UpdateView
{
	update::Status st;
	bool helper = false;
	double polled = -1;
};
UpdateView g_upd;

void PollUpdate(bool force)
{
	if (!force && Now() - g_upd.polled < 0.5)
		return;
	g_upd.polled = Now();
	std::string reply;
	g_upd.helper = jailbreak::UpdateRequest("status", reply) && update::Decode(reply, g_upd.st);
}

std::string UpdateValue()
{
	char buf[96];
	if (!g_upd.helper)
		return "Needs the helper";
	const update::Status& st = g_upd.st;
	switch (st.phase)
	{
		case update::Phase::Checking: return "Checking...";
		case update::Phase::UpToDate: return "Up to date";
		case update::Phase::Available:
			snprintf(buf, sizeof(buf), "Install %s (%.1f MB)", st.latest.c_str(), st.size / 1048576.0);
			return buf;
		case update::Phase::Downloading:
			snprintf(buf, sizeof(buf), "Downloading %d%%", st.total ? int(st.done * 100 / st.total) : 0);
			return buf;
		case update::Phase::Installing:
			snprintf(buf, sizeof(buf), "Installing %d/%d", int(st.done), int(st.total));
			return buf;
		case update::Phase::Done: return "Installed: restart";
		case update::Phase::Failed: return "Failed: try again";
		default: return "Check now";
	}
}

void UpdateAction()
{
	std::string reply;
	switch (g_upd.st.phase)
	{
		case update::Phase::Available: jailbreak::UpdateRequest("install", reply); break;
		case update::Phase::Checking:
		case update::Phase::Downloading:
		case update::Phase::Installing: break;
		case update::Phase::Done:
		{
			// the new build, opened in this one's place
			const char* path = "/data/homebrew/" PCEPS5_TITLE_ID "/eboot.bin";
			OrbisLog("[update] restarting %s", path);
			Config().Save();
			OrbisLogClose();
			const int rc = sceSystemServiceLoadExec(path, nullptr);
			if (rc == 0)
				for (int i = 0; i < 100; i++)
					usleep(100 * 1000);
			OrbisLogOpen("boot-after-update");
			OrbisLog("[update] sceSystemServiceLoadExec(%s) -> %x: close the app and open it again", path, unsigned(rc));
			break;
		}
		default: jailbreak::UpdateRequest("check", reply); break;
	}
	PollUpdate(true);
}

Row SettingRowFor(int s)
{
	const Settings& c = Config();
	char buf[48];
	switch (s)
	{
		case H_VIDEO: return {"VIDEO", "", true, true};
		case S_SHADER: return {"Shader", ps5crt::Name(ps5crt::Shader(c.shader))};
		case S_SCALE: return {"Screen size", ps5video::ScaleName(ps5video::Scale(c.scale))};
		case S_ASPECT: return {"Aspect ratio", emu::kAspects[c.aspect % emu::kAspectCount].name};
		case S_PALETTE: return {"Colours", emu::kPalettes[c.palette % emu::kPaletteCount].name};
		case S_OVERSCAN: return {"Picture edges (overscan)", emu::kOverscans[c.overscan % emu::kOverscanCount].name};
		case S_LINES: return {"Lines shown", emu::kLines[c.lines % emu::kLinesCount].name};
		case S_SMOOTH: return {"Smooth picture", c.shader ? "(shader)" : OnOff(c.smooth), c.shader == 0};
		case S_SCANLINES: return {"Scanlines (CRT effect)", c.shader ? "(shader)" : OnOff(c.scanlines), c.shader == 0};
		case S_FPS: return {"Show FPS", OnOff(c.show_fps)};
		case H_AUDIO: return {"AUDIO", "", true, true};
		case S_AUDIO: return {"Sound", OnOff(c.audio)};
		case S_VOLUME: snprintf(buf, sizeof(buf), "%d%%", c.volume); return {"Volume", buf};
		case S_PSG: return {"Sound chip", emu::kPsgRevisions[c.psg_revision % emu::kPsgRevisionCount].name};
		case S_CDDA: snprintf(buf, sizeof(buf), "%d%%", c.cdda_volume); return {"CD music volume", buf};
		case S_ADPCM: snprintf(buf, sizeof(buf), "%d%%", c.adpcm_volume); return {"CD voices (ADPCM) volume", buf};
		case S_CDPSG: snprintf(buf, sizeof(buf), "%d%%", c.cdpsg_volume); return {"CD games: console sound volume", buf};
		case H_EMU: return {"EMULATION", "", true, true};
		case S_FF:
			if (c.ff_speed == 0)
				return {"Fast forward speed (hold R2)", "Unlimited"};
			snprintf(buf, sizeof(buf), "%d%%", c.ff_speed);
			return {"Fast forward speed (hold R2)", buf};
		case S_REWIND: return {"Rewind (hold L2 + R2)", OnOff(c.rewind)};
		case S_SPRITES: return {"Remove the sprite limit", OnOff(c.no_sprite_limit)};
		case S_OVERCLOCK: return {"CPU overclock", emu::kOverclocks[c.overclock % emu::kOverclockCount].name};
		case S_CDSPEED: return {"CD speed", emu::kCdSpeeds[c.cd_speed % emu::kCdSpeedCount].name};
		case S_CDBIOS: return {"System Card (CD games)", emu::kCdBioses[c.cd_bios % emu::kCdBiosCount].name};
		case S_ARCADE: return {"Arcade Card", OnOff(c.arcade_card)};
		case S_SOFTRESET: return {"Block Run + Select reset", OnOff(c.no_soft_reset)};
		case H_CONTROLS: return {"CONTROLS (PC Engine pad button  ->  PS5 button)", "", true, true};
		case S_PADTYPE: return {"Pad", emu::kPadTypes[c.pad_type % emu::kPadTypeCount].name};
		case S_MULTITAP: return {"Multitap (5 players)", OnOff(c.multitap)};
		case S_TURBO: return {"Turbo", emu::kTurboModes[c.turbo_mode % emu::kTurboModeCount].name};
		case S_TURBOSPEED: return {"Turbo speed", emu::kTurboSpeeds[c.turbo_speed % emu::kTurboSpeedCount].name};
		case S_BTN_DEFAULT: return {"Default button layout", ""};
		case H_LIBRARY: return {"LIBRARY", "", true, true};
		case S_COVERS: return {"Download covers", OnOff(c.covers_download)};
		case H_SYSTEM: return {"SYSTEM", "", true, true};
		case S_DEBUGLOGS: return {"Debug logs", OnOff(c.debug_logs)};
		case S_UPDATES: return {"Updates (this is " PCEPS5_VERSION ")", UpdateValue()};
		default:
			if (s >= S_BTN_FIRST && s <= S_BTN_LAST)
			{
				const int i = s - S_BTN_FIRST;
				const int b = c.buttons[i];
				return {std::string("Button ") + emu::kConsoleButtons[i].name,
					emu::kPs5Buttons[b >= 0 && b < emu::kPs5ButtonCount ? b : emu::kPs5ButtonCount - 1].name};
			}
			return {"", ""};
	}
}

int Cycle(int v, int dir, int n)
{
	return ((v + dir) % n + n) % n;
}

// 0, 25, ... 200 %
int StepVolume(int v, int dir)
{
	int i = 0;
	while (i < emu::kVolumeStepCount - 1 && emu::kVolumeSteps[i] < v)
		i++;
	return emu::kVolumeSteps[std::max(0, std::min(emu::kVolumeStepCount - 1, i + dir))];
}

void ChangeSetting(int s, int dir)
{
	Settings& c = Config();
	switch (s)
	{
		case S_SHADER: c.shader = Cycle(c.shader, dir, int(ps5crt::Shader::Count)); break;
		case S_SCALE: c.scale = Cycle(c.scale, dir, int(ps5video::Scale::Count)); break;
		case S_ASPECT: c.aspect = Cycle(c.aspect, dir, emu::kAspectCount); break;
		case S_PALETTE: c.palette = Cycle(c.palette, dir, emu::kPaletteCount); break;
		case S_OVERSCAN: c.overscan = Cycle(c.overscan, dir, emu::kOverscanCount); break;
		case S_LINES: c.lines = Cycle(c.lines, dir, emu::kLinesCount); break;
		case S_SMOOTH: c.smooth = !c.smooth; break;
		case S_SCANLINES: c.scanlines = !c.scanlines; break;
		case S_FPS: c.show_fps = !c.show_fps; break;
		case S_AUDIO: c.audio = !c.audio; break;
		case S_VOLUME: c.volume = std::max(0, std::min(100, c.volume + dir * 10)); break;
		case S_PSG: c.psg_revision = Cycle(c.psg_revision, dir, emu::kPsgRevisionCount); break;
		case S_CDDA: c.cdda_volume = StepVolume(c.cdda_volume, dir); break;
		case S_ADPCM: c.adpcm_volume = StepVolume(c.adpcm_volume, dir); break;
		case S_CDPSG: c.cdpsg_volume = StepVolume(c.cdpsg_volume, dir); break;
		case S_FF:
		{
			int i = 0;
			while (i < kFfCount - 1 && kFfSteps[i] != c.ff_speed)
				i++;
			c.ff_speed = kFfSteps[Cycle(i, dir, kFfCount)];
			break;
		}
		case S_REWIND: c.rewind = !c.rewind; break;
		case S_SPRITES: c.no_sprite_limit = !c.no_sprite_limit; break;
		case S_OVERCLOCK: c.overclock = Cycle(c.overclock, dir, emu::kOverclockCount); break;
		case S_CDSPEED: c.cd_speed = Cycle(c.cd_speed, dir, emu::kCdSpeedCount); break;
		case S_CDBIOS: c.cd_bios = Cycle(c.cd_bios, dir, emu::kCdBiosCount); break;
		case S_ARCADE: c.arcade_card = !c.arcade_card; break;
		case S_SOFTRESET: c.no_soft_reset = !c.no_soft_reset; break;
		case S_TURBO: c.turbo_mode = Cycle(c.turbo_mode, dir, emu::kTurboModeCount); break;
		case S_TURBOSPEED: c.turbo_speed = Cycle(c.turbo_speed, dir, emu::kTurboSpeedCount); break;
		case S_COVERS: c.covers_download = !c.covers_download; break;
		case S_DEBUGLOGS:
			c.debug_logs = !c.debug_logs;
			OrbisLogSetEnabled(c.debug_logs); // at once: the line saying so is the last (or first) one written
			break;
		case S_PADTYPE: c.pad_type = Cycle(c.pad_type, dir, emu::kPadTypeCount); break;
		case S_MULTITAP: c.multitap = !c.multitap; break;
		case S_BTN_DEFAULT: c.DefaultButtons(); break;
		default:
			if (s >= S_BTN_FIRST && s <= S_BTN_LAST)
			{
				int& b = c.buttons[s - S_BTN_FIRST];
				b = Cycle(b, dir, emu::kPs5ButtonCount);
			}
			break;
	}
	emu::ApplySettings();
}

std::vector<Row> SettingRows()
{
	std::vector<Row> rows;
	for (int s = 0; s < S_COUNT; s++)
		rows.push_back(SettingRowFor(s));
	rows.push_back({"Back", ""});
	return rows;
}

void Background(bool over_game)
{
	if (over_game)
	{
		// the paused game, darkened, under the box
		ps5video::FillRect(0, 0, W, H, Rgb(0, 0, 0));
		emu::RedrawLastFrame();
		ps5video::DarkenRect(0, 0, W, H);
		ps5video::DarkenRect(0, 0, W, H);
	}
	else
		Header("Settings");
}

void SettingsScreen(bool over_game)
{
	NavReader nav;
	int sel = S_SHADER;
	for (;;)
	{
		PollUpdate(false);
		const std::vector<Row> rows = SettingRows();
		Background(over_game);
		DrawOptionBox("Settings", rows, sel, over_game);
		if (sel == S_UPDATES && g_upd.st.phase == update::Phase::Failed && !g_upd.st.error.empty())
			DrawText(80, H - 130, FitText(g_upd.st.error, 2, W - 160).c_str(), 2, kAccent);
		else if (sel == S_UPDATES && !g_upd.helper)
			DrawText(80, H - 130, "Updates are downloaded by the helper: send the payload, then open Settings again.", 2, kDim);
		else if (sel < S_COUNT && *SettingHelp(sel))
			DrawText(80, H - 130, FitText(SettingHelp(sel), 2, W - 160).c_str(), 2, kDim);
		if (!over_game)
			Footer(std::string(icon::Cross) + " Change     " + icon::DpadLeftRight + " Adjust     " + icon::Circle + " Back");
		Present();
		const Nav n = nav.Read();
		if (n.up)
			sel = Step(sel, -1, rows);
		if (n.down)
			sel = Step(sel, 1, rows);
		if (sel == S_UPDATES)
		{
			if (n.ok)
				UpdateAction();
		}
		else if (sel < S_COUNT && (n.left || n.right || n.ok))
			ChangeSetting(sel, n.left ? -1 : 1);
		if (n.back || n.options || n.menu || (n.ok && sel == S_COUNT))
		{
			Config().Save();
			return;
		}
	}
}

// Word wrap into lines that fit max_px at `scale`.
std::vector<std::string> Wrap(const std::string& text, int scale, int max_px)
{
	std::vector<std::string> lines;
	std::string cur;
	size_t pos = 0;
	while (pos <= text.size())
	{
		size_t sp = text.find(' ', pos);
		if (sp == std::string::npos)
			sp = text.size();
		const std::string word = text.substr(pos, sp - pos);
		const std::string tryline = cur.empty() ? word : cur + " " + word;
		if (!cur.empty() && TextWidth(tryline.c_str(), scale) > max_px)
		{
			lines.push_back(cur);
			cur = word;
		}
		else
			cur = tryline;
		pos = sp + 1;
	}
	if (!cur.empty())
		lines.push_back(cur);
	return lines;
}
} // namespace

void SettingsMenu()
{
	SettingsScreen(false);
}

PauseAction PauseMenu()
{
	Settings& cfg = Config();
	NavReader nav;
	enum Item
	{
		I_RESUME,
		I_SAVE,
		I_LOAD,
		I_SLOT,
		I_SETTINGS,
		I_RESET,
		I_POWER,
		I_LIST,
		I_QUIT,
		I_COUNT
	};
	int sel = I_RESUME;
	std::string toast;
	double toast_until = 0;

	for (;;)
	{
		char buf[64];
		std::vector<Row> rows(I_COUNT);
		rows[I_RESUME] = {"Resume", ""};
		snprintf(buf, sizeof(buf), "Save state (slot %d)", cfg.state_slot);
		rows[I_SAVE] = {buf, ""};
		snprintf(buf, sizeof(buf), "Load state (slot %d)", cfg.state_slot);
		rows[I_LOAD] = {buf, "", emu::StateExists(cfg.state_slot)};
		snprintf(buf, sizeof(buf), "%d%s", cfg.state_slot, emu::StateExists(cfg.state_slot) ? " (used)" : " (empty)");
		rows[I_SLOT] = {"State slot", buf};
		rows[I_SETTINGS] = {"Settings", ""};
		rows[I_RESET] = {"Reset", ""};
		rows[I_POWER] = {"Power cycle", ""};
		rows[I_LIST] = {"Back to the game list", ""};
		rows[I_QUIT] = {"Quit Beetle PCE PS5", ""};

		Background(true);
		const std::string sys = emu::SystemName();
		DrawOptionBox(emu::GameName(), rows, sel, true);
		if (!sys.empty())
			DrawText(W / 2 + 560 - TextWidth(sys.c_str(), 2) - 40, (H - (120 + I_COUNT * 46 + 30)) / 2 + 40, sys.c_str(), 2, kDim);
		if (!toast.empty() && Now() < toast_until)
		{
			const int tw = TextWidth(toast.c_str(), 3);
			ps5video::FillRect((W - tw) / 2 - 30, H - 110, tw + 60, 70, kSel);
			DrawText((W - tw) / 2, H - 90, toast.c_str(), 3, kText);
		}
		Present();

		const Nav n = nav.Read();
		if (n.up)
			sel = Step(sel, -1, rows);
		if (n.down)
			sel = Step(sel, 1, rows);
		if (n.back || n.options || n.menu)
			return PauseAction::Resume;

		if (sel == I_SLOT && (n.left || n.right || n.ok))
		{
			cfg.state_slot = n.left ? (cfg.state_slot + 8) % 10 + 1 : cfg.state_slot % 10 + 1;
			cfg.Save();
		}
		else if (n.ok)
		{
			switch (sel)
			{
				case I_RESUME: return PauseAction::Resume;
				case I_SAVE:
					toast = emu::SaveState(cfg.state_slot) ? "State saved." : "Could not save the state.";
					toast_until = Now() + 2.0;
					cfg.Save();
					break;
				case I_LOAD:
					if (emu::LoadState(cfg.state_slot))
						return PauseAction::Resume;
					toast = "This slot is empty.";
					toast_until = Now() + 2.0;
					break;
				case I_SETTINGS:
					SettingsScreen(true);
					nav = NavReader();
					break;
				case I_RESET:
					emu::Reset();
					return PauseAction::Resume;
				case I_POWER:
					emu::PowerCycle();
					return PauseAction::Resume;
				case I_LIST: return PauseAction::BackToList;
				case I_QUIT: return PauseAction::Quit;
				default: break;
			}
		}
	}
}

void MessageBox(const std::string& title, const std::string& text)
{
	NavReader nav;
	const int bw = 1400;
	const std::vector<std::string> lines = Wrap(text, 3, bw - 80);
	const int bh = 220 + int(lines.size()) * 44;
	for (;;)
	{
		Header("");
		const int bx = (W - bw) / 2, by = (H - bh) / 2;
		ps5video::FillRect(bx - 4, by - 4, bw + 8, bh + 8, kAccent);
		ps5video::FillRect(bx, by, bw, bh, kPanel);
		DrawText(bx + 40, by + 30, FitText(title, 5, bw - 80).c_str(), 5, kAccent);
		int y = by + 120;
		for (const std::string& l : lines)
		{
			DrawText(bx + 40, y, l.c_str(), 3, kText);
			y += 44;
		}
		DrawText(bx + 40, by + bh - 70, (std::string(icon::Cross) + " OK").c_str(), 3, kDim);
		Present();
		const Nav n = nav.Read();
		if (n.ok || n.back || n.options)
			return;
	}
}
} // namespace fe
