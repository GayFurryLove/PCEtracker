// PCEtracker - user configuration.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "ui/config.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace pce {

Config::Config()
  : scale(1), fullscreen(2), softwareRender(false), vsync(true), fps(30),
    audioRate(44100), audioBuffer(1024), revisionA(true) {}

std::string Config::defaultPath() {
  char* base = SDL_GetPrefPath("pcetracker", "pcetracker");
  std::string p = base ? std::string(base) : std::string();
  SDL_free(base);
  return p + "pcetracker.cfg";
}

static const char* kDefaultText =
  "# PCEtracker configuration\n"
  "# Window scale (integer): 1 = 1024x600, 2 = 2048x1200 ...\n"
  "scale=1\n"
  "# fullscreen: 0 = window, 1 = fullscreen, auto = fullscreen when the window does not fit\n"
  "# (e.g. a 1024x600 screen with a title bar and a taskbar)\n"
  "fullscreen=auto\n"
  "# 1 = SDL software renderer (no GPU acceleration needed)\n"
  "software_render=0\n"
  "vsync=1\n"
  "# Screen updates per second while playing. Lower it on a slow CPU (20, 15).\n"
  "fps=30\n"
  "# Audio output. Raise audio_buffer (2048, 4096) if sound crackles on a slow CPU.\n"
  "audio_rate=44100\n"
  "audio_buffer=1024\n"
  "# Chip revision: 6280a (later consoles, TurboGrafx-16) or 6280 (early PC Engine)\n"
  "revision=6280a\n";

void Config::load(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    f = fopen(path.c_str(), "wb");
    if (f) { fputs(kDefaultText, f); fclose(f); }
    return;
  }
  char line[256];
  while (fgets(line, sizeof line, f)) {
    char* eq = strchr(line, '=');
    if (line[0] == '#' || !eq) continue;
    *eq = 0;
    const char* key = line;
    const char* val = eq + 1;
    int v = atoi(val);
    if (!strcmp(key, "scale")) scale = v < 1 ? 1 : (v > 8 ? 8 : v);
    else if (!strcmp(key, "fullscreen")) fullscreen = !strncmp(val, "auto", 4) ? 2 : (v != 0);
    else if (!strcmp(key, "software_render")) softwareRender = v != 0;
    else if (!strcmp(key, "vsync")) vsync = v != 0;
    else if (!strcmp(key, "fps")) fps = v < 10 ? 10 : (v > 60 ? 60 : v);
    else if (!strcmp(key, "audio_rate")) audioRate = (v >= 8000 && v <= 192000) ? v : 44100;
    else if (!strcmp(key, "audio_buffer")) audioBuffer = (v >= 256 && v <= 16384) ? v : 1024;
    else if (!strcmp(key, "revision")) revisionA = strncmp(val, "6280a", 5) == 0;
  }
  fclose(f);
}

} // namespace pce
