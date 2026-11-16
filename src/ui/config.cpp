// PCEtracker - user configuration.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "ui/config.h"
#include "ui/fs.h"
#include "util/file_utf8.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace pce {

Config::Config()
  : scale(1), fullscreen(2), softwareRender(false), vsync(true), fps(30),
    audioRate(44100), audioBuffer(1024), revisionA(true), splash(true) {}

std::string Config::defaultPath() {
  // Portable: a pcetracker.cfg next to the program, or in the folder it was started from.
  char* basePath = SDL_GetBasePath();
  std::string exeDir = basePath ? std::string(basePath) : std::string();
  SDL_free(basePath);
  if (!exeDir.empty() && fs::exists(fs::join(exeDir, "pcetracker.cfg"))) return fs::join(exeDir, "pcetracker.cfg");
  if (fs::exists("pcetracker.cfg")) return fs::absolute("pcetracker.cfg");
  char* base = SDL_GetPrefPath("pcetracker", "pcetracker");
  std::string p = base ? std::string(base) : std::string();
  SDL_free(base);
  return p + "pcetracker.cfg";
}

std::string Config::songPath(const std::string& name) const {
  if (songDir.empty() || fs::isAbsolute(name)) return name;
  return fs::join(fs::absolute(songDir), name);
}

std::string Config::startDir() const {
  if (!songDir.empty()) {
    std::string d = fs::absolute(songDir);
    if (fs::isDir(d)) return d;
  }
  return fs::currentDir();
}

namespace {

// Every key with its comment block; written for a new file and appended for
// keys an older file is missing (so new options show up by themselves).
struct KeyDoc { const char* key; const char* text; };
const KeyDoc kKeys[] = {
  { "scale", "# Window scale (integer): 1 = 1024x600, 2 = 2048x1200 ...\nscale=1\n" },
  { "fullscreen", "# fullscreen: 0 = window, 1 = fullscreen, auto = fullscreen when the window does not fit\n"
                  "# (e.g. a 1024x600 screen with a title bar and a taskbar)\nfullscreen=auto\n" },
  { "software_render", "# 1 = SDL software renderer (no GPU acceleration needed)\nsoftware_render=0\n" },
  { "vsync", "vsync=1\n" },
  { "fps", "# Screen updates per second while playing. Lower it on a slow CPU (20, 15).\nfps=30\n" },
  { "audio_rate", "# Audio output. Raise audio_buffer (2048, 4096) if sound crackles on a slow CPU.\naudio_rate=44100\n" },
  { "audio_buffer", "audio_buffer=1024\n" },
  { "revision", "# Chip revision: 6280a (later consoles, TurboGrafx-16) or 6280 (early PC Engine)\nrevision=6280a\n" },
  { "demo", "# Startup logo with the mascot: 1 = show it, 0 = start straight into the tracker\ndemo=1\n" },
  { "song_dir", "# Folder the file browser (open, save, export, import) starts in for a new song;\n"
                "# new songs are saved there. Empty = the folder the tracker was started from.\n"
                "# Examples: song_dir=D:\\Music\\pce   song_dir=~/music/pce\nsong_dir=\n" },
};
const int kKeyCount = (int)(sizeof kKeys / sizeof kKeys[0]);

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n\"");
  if (b == std::string::npos) return std::string();
  size_t e = s.find_last_not_of(" \t\r\n\"");
  return s.substr(b, e - b + 1);
}

} // namespace

void Config::load(const std::string& p) {
  path = p;
  FILE* f = openFile(p.c_str(), "rb");
  if (!f) {
    f = openFile(p.c_str(), "wb");
    if (f) {
      fputs("# PCEtracker configuration\n", f);
      for (int i = 0; i < kKeyCount; ++i) fputs(kKeys[i].text, f);
      fclose(f);
    }
    return;
  }
  bool seen[kKeyCount] = { false };
  char line[1024];
  while (fgets(line, sizeof line, f)) {
    char* eq = strchr(line, '=');
    if (line[0] == '#' || !eq) continue;
    *eq = 0;
    // "key = value" works as well as "key=value".
    const std::string key = trim(line);
    const std::string val = trim(eq + 1);
    const char* v0 = val.c_str();
    const int v = atoi(v0);
    for (int i = 0; i < kKeyCount; ++i) if (key == kKeys[i].key) seen[i] = true;
    if (key == "scale") scale = v < 1 ? 1 : (v > 8 ? 8 : v);
    else if (key == "fullscreen") fullscreen = !strncmp(v0, "auto", 4) ? 2 : (v != 0);
    else if (key == "software_render") softwareRender = v != 0;
    else if (key == "vsync") vsync = v != 0;
    else if (key == "fps") fps = v < 10 ? 10 : (v > 60 ? 60 : v);
    else if (key == "audio_rate") audioRate = (v >= 8000 && v <= 192000) ? v : 44100;
    else if (key == "audio_buffer") audioBuffer = (v >= 256 && v <= 16384) ? v : 1024;
    else if (key == "revision") revisionA = strncmp(v0, "6280a", 5) == 0;
    else if (key == "demo" || key == "splash") splash = !(val == "0" || val == "off" || val == "no");
    else if (key == "song_dir") songDir = fs::expandHome(val);
  }
  fclose(f);
  // Older file: add the options it does not know yet, with their comments.
  std::string missing;
  for (int i = 0; i < kKeyCount; ++i)
    if (!seen[i] && strcmp(kKeys[i].key, "vsync") && strcmp(kKeys[i].key, "audio_buffer")) missing += kKeys[i].text;
  if (!missing.empty() && (f = openFile(p.c_str(), "ab")) != 0) {
    fputs("\n# --- added by a newer PCEtracker ---\n", f);
    fputs(missing.c_str(), f);
    fclose(f);
  }
}

} // namespace pce
