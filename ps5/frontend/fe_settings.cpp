// Beetle PCE PS5 frontend: settings file.
// SPDX-License-Identifier: MIT

#include "fe_settings.h"

#include "OrbisPaths.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace fe
{
namespace
{
std::string IniPath()
{
	return OrbisRoot() + "/pce-ps5.ini";
}

int Clamp(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

const char* const kButtonKeys[Settings::kButtons] = {"btn_1", "btn_2", "btn_3", "btn_4", "btn_5", "btn_6", "btn_select",
	"btn_run"};
constexpr int kPs5ButtonChoices = 9; // emu::kPs5ButtonCount
constexpr int kShaderCount = 12; // ps5crt::Shader::Count
} // namespace

constexpr int Settings::kDefaultButtons[Settings::kButtons];

void Settings::DefaultButtons()
{
	for (int i = 0; i < kButtons; i++)
		buttons[i] = kDefaultButtons[i];
}

Settings& Config()
{
	static Settings s;
	return s;
}

void Settings::Load()
{
	// off by default (1.0.1); the host tests turn the default on (orbis_paths.cpp reads the same variable)
	if (const char* d = getenv("PCEPS5_LOGS_DEFAULT"))
		debug_logs = atoi(d) != 0;
	FILE* f = fopen(IniPath().c_str(), "r");
	if (!f)
		return;
	char line[1024];
	while (fgets(line, sizeof(line), f))
	{
		line[strcspn(line, "\r\n")] = 0;
		char* eq = strchr(line, '=');
		if (!eq || line[0] == '#' || line[0] == ';')
			continue;
		*eq = 0;
		const std::string key = line;
		const char* val = eq + 1;
		const int n = atoi(val);
		if (key == "scale")
			scale = Clamp(n, 0, 2);
		else if (key == "shader")
			shader = Clamp(n, 0, kShaderCount - 1);
		else if (key == "aspect")
			aspect = Clamp(n, 0, 4);
		else if (key == "palette")
			palette = Clamp(n, 0, 1);
		else if (key == "overscan")
			overscan = Clamp(n, 0, 2);
		else if (key == "lines")
			lines = Clamp(n, 0, 2);
		else if (key == "smooth")
			smooth = n != 0;
		else if (key == "scanlines")
			scanlines = n != 0;
		else if (key == "show_fps")
			show_fps = n != 0;
		else if (key == "audio")
			audio = n != 0;
		else if (key == "volume")
			volume = Clamp(n, 0, 100);
		else if (key == "psg_revision")
			psg_revision = Clamp(n, 0, 2);
		else if (key == "cdda_volume")
			cdda_volume = Clamp(n, 0, 200);
		else if (key == "adpcm_volume")
			adpcm_volume = Clamp(n, 0, 200);
		else if (key == "cdpsg_volume")
			cdpsg_volume = Clamp(n, 0, 200);
		else if (key == "ff_speed")
			ff_speed = n == 0 ? 0 : Clamp(n, 150, 1000);
		else if (key == "rewind")
			rewind = n != 0;
		else if (key == "no_sprite_limit")
			no_sprite_limit = n != 0;
		else if (key == "overclock")
			overclock = Clamp(n, 0, 3);
		else if (key == "cd_speed")
			cd_speed = Clamp(n, 0, 3);
		else if (key == "cd_bios")
			cd_bios = Clamp(n, 0, 5);
		else if (key == "arcade_card")
			arcade_card = n != 0;
		else if (key == "no_soft_reset")
			no_soft_reset = n != 0;
		else if (key == "pad_type")
			pad_type = Clamp(n, 0, 1);
		else if (key == "multitap")
			multitap = n != 0;
		else if (key == "turbo_mode")
			turbo_mode = Clamp(n, 0, 2);
		else if (key == "turbo_speed")
			turbo_speed = Clamp(n, 0, 2);
		else if (key.compare(0, 4, "btn_") == 0)
		{
			for (int i = 0; i < kButtons; i++)
				if (key == kButtonKeys[i])
					buttons[i] = Clamp(n, 0, kPs5ButtonChoices - 1);
		}
		else if (key == "state_slot")
			state_slot = Clamp(n, 1, 10);
		else if (key == "covers_download")
			covers_download = n != 0;
		else if (key == "language")
			language = Clamp(n, 0, 4);
		else if (key == "debug_logs")
			debug_logs = n != 0;
		else if (key == "shelf_family")
			shelf_family = Clamp(n, 0, 64);
		else if (key == "last_rom")
			last_rom = val;
	}
	fclose(f);
	OrbisLog("[settings] loaded %s", IniPath().c_str());
}

void Settings::Save() const
{
	const std::string tmp = IniPath() + ".tmp";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f)
	{
		OrbisLog("[settings] can't write %s", tmp.c_str());
		return;
	}
	fprintf(f, "# Beetle PCE PS5\n");
	fprintf(f, "scale=%d\n", scale);
	fprintf(f, "shader=%d\n", shader);
	fprintf(f, "aspect=%d\n", aspect);
	fprintf(f, "palette=%d\n", palette);
	fprintf(f, "overscan=%d\n", overscan);
	fprintf(f, "lines=%d\n", lines);
	fprintf(f, "smooth=%d\n", smooth ? 1 : 0);
	fprintf(f, "scanlines=%d\n", scanlines ? 1 : 0);
	fprintf(f, "show_fps=%d\n", show_fps ? 1 : 0);
	fprintf(f, "audio=%d\n", audio ? 1 : 0);
	fprintf(f, "volume=%d\n", volume);
	fprintf(f, "psg_revision=%d\n", psg_revision);
	fprintf(f, "cdda_volume=%d\n", cdda_volume);
	fprintf(f, "adpcm_volume=%d\n", adpcm_volume);
	fprintf(f, "cdpsg_volume=%d\n", cdpsg_volume);
	fprintf(f, "ff_speed=%d\n", ff_speed);
	fprintf(f, "rewind=%d\n", rewind ? 1 : 0);
	fprintf(f, "no_sprite_limit=%d\n", no_sprite_limit ? 1 : 0);
	fprintf(f, "overclock=%d\n", overclock);
	fprintf(f, "cd_speed=%d\n", cd_speed);
	fprintf(f, "cd_bios=%d\n", cd_bios);
	fprintf(f, "arcade_card=%d\n", arcade_card ? 1 : 0);
	fprintf(f, "no_soft_reset=%d\n", no_soft_reset ? 1 : 0);
	fprintf(f, "pad_type=%d\n", pad_type);
	fprintf(f, "multitap=%d\n", multitap ? 1 : 0);
	fprintf(f, "turbo_mode=%d\n", turbo_mode);
	fprintf(f, "turbo_speed=%d\n", turbo_speed);
	for (int i = 0; i < kButtons; i++)
		fprintf(f, "%s=%d\n", kButtonKeys[i], buttons[i]);
	fprintf(f, "state_slot=%d\n", state_slot);
	fprintf(f, "covers_download=%d\n", covers_download ? 1 : 0);
	fprintf(f, "language=%d\n", language);
	fprintf(f, "debug_logs=%d\n", debug_logs ? 1 : 0);
	fprintf(f, "shelf_family=%d\n", shelf_family);
	fprintf(f, "last_rom=%s\n", last_rom.c_str());
	bool ok = ferror(f) == 0;
	ok = fflush(f) == 0 && ok;
	ok = fsync(fileno(f)) == 0 && ok;
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp.c_str(), IniPath().c_str()) != 0)
	{
		remove(tmp.c_str());
		OrbisLog("[settings] can't save %s (disk full?)", IniPath().c_str());
	}
}
} // namespace fe
