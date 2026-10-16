// Beetle PCE PS5 frontend: the Beetle PCE core behind a small API.
//
// The core is Mednafen's PC Engine emulator ("pce", the accurate one: PC Engine / TurboGrafx-16, CoreGrafx,
// SuperGrafx, CD-ROM², Super CD-ROM², Arcade Card, TurboGrafx-CD), driven through its libretro interface
// (../libretro.cpp): the frontend is the libretro "frontend" -- it answers the core's environment calls (folders,
// options, log), gets each frame (XRGB8888, its width follows the game's dot clock) and its sound (44.1 kHz)
// through callbacks, and reports the pads. One retro_run() is one frame, called from the frontend's thread.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace emu
{
// The choices the settings menu offers.
struct Choice
{
	const char* name;
	const char* value; // the core option's value ("" where the frontend handles it)
};
extern const Choice kAspects[]; // pce_aspect_ratio: the TV's (auto), square pixels, 4:3, 6:5; 16:9 by the frontend
extern const int kAspectCount;
extern const Choice kPalettes[]; // pce_palette: RGB, Composite
extern const int kPaletteCount;
extern const Choice kOverscans[]; // pce_h_overscan: auto, hidden, shown
extern const int kOverscanCount;
struct LinesChoice
{
	const char* name;
	const char* first; // pce_initial_scanline
	const char* last; // pce_last_scanline
};
extern const LinesChoice kLines[]; // the lines shown: 240, 232 (Mednafen's), 224
extern const int kLinesCount;
extern const Choice kPsgRevisions[]; // pce_psgrevision: auto, HuC6280, HuC6280A
extern const int kPsgRevisionCount;
extern const Choice kOverclocks[]; // pce_ocmultiplier: 1x .. 4x
extern const int kOverclockCount;
extern const Choice kCdSpeeds[]; // pce_cdspeed: 1x, 2x, 4x, 8x
extern const int kCdSpeedCount;
extern const Choice kCdBioses[]; // pce_cdbios: auto (the best card in bios/), System Card 3, 3 US, 2, 2 US, 1
extern const int kCdBiosCount;
extern const Choice kPadTypes[]; // pce_default_joypad_type_pN: 2 buttons, 6 buttons (Avenue Pad 6)
extern const int kPadTypeCount;
extern const Choice kTurboModes[]; // pce_Turbo_Toggling: off, toggle (Square/Triangle), always (Square/Triangle)
extern const int kTurboModeCount;
extern const Choice kTurboSpeeds[]; // pce_Turbo_Delay: fast, medium, slow
extern const int kTurboSpeedCount;
extern const int kVolumeSteps[]; // CD-DA / ADPCM / PSG volume of CD games, % (pce_cddavolume...)
extern const int kVolumeStepCount;

// The buttons of the console's pad the player can move, and the PS5 buttons they can go to.
struct PadButton
{
	const char* name; // "Square"
	unsigned bit; // SCE_PAD_BUTTON_*; 0 = not assigned
};
extern const PadButton kPs5Buttons[]; // Cross, Circle, Square, Triangle, L1, R1, OPTIONS, touchpad, (none)
extern const int kPs5ButtonCount;
struct ConsoleButton
{
	const char* name; // "I", "II", "III (6-button pad)"...
	const char* key; // its key in pce-ps5.ini: "btn_1"
	unsigned retro_id; // the RetroPad button Beetle PCE reads for it
	int default_ps5; // index in kPs5Buttons
};
constexpr int kConsoleButtonCount = 8; // I, II, III, IV, V, VI, Select, Run
extern const ConsoleButton kConsoleButtons[kConsoleButtonCount];

bool InitCore();
void DeinitCore();

// Starts the game. On failure, *error says why (a CD game without a System Card names the files and where they go).
bool LoadGame(const std::string& path, std::string* error);
void CloseGame(); // writes the battery save and ends the game
bool GameLoaded();
std::string GameName(); // the file name without its extension
std::string SystemName(); // "PC Engine", "SuperGrafx", "PC Engine CD" of the running game

// Settings from fe::Config() -> the core's options and the frontend's picture/sound settings.
void ApplySettings();

enum class FrameResult
{
	Continue,
	OpenMenu, // L3 + R3 pressed together
	Stopped, // no game
};
// One frame: the pads and hot keys, retro_run(), the picture (and the messages) on the screen, the sound to
// the audio ring, paced by the display (60 Hz games) or by the sound (50 Hz games). Hot keys: L2 + Up / Down =
// save / load the state slot, L2 + Left / Right = change the slot, R2 held = fast forward, L2 + R2 held =
// rewind.
FrameResult RunFrame();

void Pause(); // nothing runs between frames; kept for the menus' symmetry
void Resume();

bool SaveState(int slot); // 1..10, /data/pce/states/<game>.state<slot>
bool LoadState(int slot);
bool StateExists(int slot);
void Reset();
void PowerCycle(); // the game loaded again, as switching the console off and on
void Osd(const std::string& text); // a message over the game for a few seconds

// Redraws the last frame into the surface (the pause menu draws over it).
void RedrawLastFrame();
} // namespace emu
