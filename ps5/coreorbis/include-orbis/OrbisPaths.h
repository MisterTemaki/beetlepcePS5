// Beetle PCE PS5: the /data/pce folder layout and the boot log (orbis-shims/orbis_paths.cpp).
//
//   /data/pce/roms         games (.pce .sgx, also in .zip; CDs .cue .chd .ccd .toc .m3u); sub-folders are fine
//   /data/pce/bios         the System Cards for CD games (syscard3.pce ...) and gexpress.pce
//   /data/pce/saves        the backup RAM, <system>/<game>.srm
//   /data/pce/states       save states, <game>.state1 .. <game>.state10
//   /data/pce/covers       box art (downloaded, or your own <ROM file name>.png), crc-cache.txt, wanted.txt
//   /data/pce/logs         boot.log (this run) and boot.prev.log (the run before); installer.log, helper.log
//   /data/pce/pce-ps5.ini  the frontend's settings
//
// USB drives are searched too: /mnt/usbN/pce/roms (N = 0..7) and /mnt/extN/pce/roms.
//
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

#ifndef ORBIS_ROOT_DEFAULT
#define ORBIS_ROOT_DEFAULT "/data/pce"
#endif

// Root folder (ORBIS_ROOT_DEFAULT; the host tests point it elsewhere with PCEPS5_ROOT).
const std::string& OrbisRoot();
// <root>/<sub>, created at boot by OrbisPathsInit.
std::string OrbisDir(const char* sub);
// Creates the folder tree. Returns false when the root itself can't be created (no /data access).
bool OrbisPathsInit();
// Folders that hold ROMs and exist right now (internal first, then USB drives).
std::vector<std::string> OrbisRomRoots();

bool OrbisIsDir(const std::string& path);
bool OrbisIsFile(const std::string& path);
bool OrbisMkdirs(const std::string& path);

// <root>/logs/<name>.log (the run before kept as <name>.prev.log): boot.log for the emulator, installer.log
// for the installer/helper payload. Every line also goes to stdout (once the log is open). Lines logged before the file is open
// (before the jailbreak shows /data to the app) are kept and written first.
void OrbisLogOpen(const char* name = "boot");
void OrbisLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void OrbisLogClose();
// Debug logs on or off: the "Debug logs" setting (debug_logs= in pce-ps5.ini, on unless it says 0).
// OrbisLogOpen reads it, so the app, the installer and the helper all follow it; while off, OrbisLog writes
// nothing (no file, no stdout) and the log files of earlier runs are left as they are.
void OrbisLogSetEnabled(bool on);
// Reads the setting again and applies it (the helper, which keeps running, calls this for each request).
void OrbisLogRefresh();
bool OrbisLogEnabled();
// The open log's file descriptor, for the crash handler's signal-safe write(); -1 before it is open.
int OrbisLogFd();
