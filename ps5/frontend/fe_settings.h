// Beetle PCE PS5 frontend: settings kept in /data/pce/pce-ps5.ini (key=value lines).
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace fe
{
struct Settings
{
	// video
	int scale = 0; // ps5video::Scale: fit, integer, stretch
	int shader = 1; // ps5crt::Shader: 0 off, 1 "CRT Easymode style" (the default for every game), ...
	int aspect = 0; // emu::kAspects: the TV's (the core's, from the dot clock), square pixels, 4:3, 6:5, 16:9
	int palette = 0; // emu::kPalettes: RGB, Composite
	int overscan = 0; // emu::kOverscans: auto, hidden, shown (the picture's left and right edges)
	int lines = 0; // emu::kLines: 240, 232, 224
	bool smooth = false; // bilinear when the scale isn't whole
	bool scanlines = false;
	bool show_fps = false;
	// audio
	bool audio = true;
	int volume = 100; // 0..100
	int psg_revision = 0; // emu::kPsgRevisions: auto, HuC6280, HuC6280A
	int cdda_volume = 100; // CD games: the CD's music, % (0..200)
	int adpcm_volume = 100; // CD games: the ADPCM voices, %
	int cdpsg_volume = 100; // CD games: the PSG (the console's own sound), %
	// emulation
	int ff_speed = 300; // % while R2 is held; 0 = as fast as possible
	bool rewind = true; // L2 + R2 rewinds
	bool no_sprite_limit = false;
	int overclock = 0; // emu::kOverclocks: off, 2x, 3x, 4x (the CPU, against slowdown)
	int cd_speed = 0; // emu::kCdSpeeds: 1x, 2x, 4x, 8x (loading)
	int cd_bios = 0; // emu::kCdBioses: auto, System Card 3.0, 3.0 US, 2.0, 2.0 US, 1.0
	bool arcade_card = true; // the Arcade Card (Arcade CD games; harmless for the others)
	bool no_soft_reset = false; // Run + Select doesn't reset the game
	int state_slot = 1; // 1..10
	// controls
	int pad_type = 0; // emu::kPadTypes: 2 buttons, 6 buttons (every player)
	bool multitap = true; // the 5-player adapter (players 2 to 4 are the other signed-in users)
	int turbo_mode = 0; // emu::kTurboModes: off, III/IV switch I/II's turbo, III/IV are turbo I/II
	int turbo_speed = 0; // emu::kTurboSpeeds: fast, medium, slow
	// the PS5 button (index in emu::kPs5Buttons) of each console button, in emu::kConsoleButtons' order:
	// I, II, III, IV, V, VI, Select, Run. By position by default: Circle, Cross, Square, Triangle, L1, R1, touchpad,
	// OPTIONS.
	static constexpr int kButtons = 8;
	static constexpr int kDefaultButtons[kButtons] = {1, 0, 2, 3, 4, 5, 7, 6};
	int buttons[kButtons] = {1, 0, 2, 3, 4, 5, 7, 6};
	void DefaultButtons();
	// library
	bool covers_download = true; // fetch box art from libretro-thumbnails
	int language = 0;            // fe::Language: 0 English (the default), 1 Spanish, 2 Portuguese (Brazil), 3 French, 4 German
	bool debug_logs = false;     // boot.log and the others in /data/pce/logs (OrbisLogSetEnabled)
	int shelf_family = 0; // fe::Family the shelf shows
	int last_tab = 0; // the shelf's storage tab (fe_tabs.h): 0 Internal, 1 USB, 2 Network
	std::string last_rom; // the shelf puts the selection on this game

	void Load();
	void Save() const;
};

Settings& Config();
} // namespace fe
