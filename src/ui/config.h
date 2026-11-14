// PCEtracker - user configuration (pcetracker.cfg, key=value).
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#ifndef PCETRACKER_CONFIG_H
#define PCETRACKER_CONFIG_H

#include <string>

namespace pce {

struct Config {
  int scale;          // integer window scale: 1 = 1024x600
  int fullscreen;     // 0 window, 1 fullscreen, 2 auto (fullscreen if the window does not fit)
  bool softwareRender;// force SDL's software renderer (no GPU needed)
  bool vsync;
  int fps;            // screen updates per second while playing (10..60)
  int audioRate;      // output sample rate
  int audioBuffer;    // samples per audio callback (bigger = safer on slow CPUs)
  bool revisionA;     // HuC6280A (true) or HuC6280 (false)
  Config();

  // Loads `path`; writes a commented default file if it does not exist.
  void load(const std::string& path);
  static std::string defaultPath();
};

} // namespace pce

#endif
