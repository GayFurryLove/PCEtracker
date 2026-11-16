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
  bool splash;        // startup logo with the mascot (config key "demo")
  std::string songDir;// start folder of the file browser, folder for new songs ("" = current dir)
  std::string path;   // the file that was loaded
  Config();

  // `name` as is if it is absolute or songDir is empty, else songDir/name.
  std::string songPath(const std::string& name) const;
  // Absolute folder the file browser starts in for a new song.
  std::string startDir() const;

  // Loads `path`; writes a commented default file if it does not exist and
  // appends keys that an older file does not have yet (with their comments).
  void load(const std::string& path);
  // pcetracker.cfg next to the program or in the current folder (portable
  // install), otherwise the per-user file (Linux ~/.local/share/pcetracker/...).
  static std::string defaultPath();
};

} // namespace pce

#endif
