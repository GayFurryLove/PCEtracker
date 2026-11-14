// PCEtracker - HuCard ROM export (module 8).
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "export/hes_export.h"

#include <stdio.h>

namespace pce {

namespace {

std::string upper(const std::string& s) {
  std::string r = s;
  for (size_t i = 0; i < r.size(); ++i) {
    unsigned char c = (unsigned char)r[i];
    if (c < 32 || c > 126) r[i] = '?';
  }
  return r;
}

ScreenText centered(int row, const std::string& text, bool highlight) {
  ScreenText t;
  std::string s = upper(text).substr(0, 30);
  t.row = row;
  t.col = (32 - (int)s.size()) / 2;
  t.highlight = highlight;
  t.text = s;
  return t;
}

// What the console shows while the music plays (32 x 28 cells visible).
DriverOptions titleScreen(const Song& song, const ExportStats& st) {
  DriverOptions o;
  o.hucard = true;
  o.screen.push_back(centered(3, "PCETRACKER", true));
  o.screen.push_back(centered(10, song.title.empty() ? "untitled" : song.title, true));
  if (!song.author.empty()) o.screen.push_back(centered(12, "by " + song.author, false));
  char b[40];
  const int secs = (int)(st.framesLoop / 59.826 + 0.5);
  snprintf(b, sizeof b, "loop %d:%02d", secs / 60, secs % 60);
  o.screen.push_back(centered(16, b, false));
  o.screen.push_back(centered(24, "HuC6280 PSG - 6 channels", false));
  return o;
}

} // namespace

bool exportRom(const Song& song, std::vector<uint8_t>* rom, ExportStats* stats, std::string* err) {
  // First pass only to learn the loop length for the screen; the image is identical otherwise.
  std::vector<uint8_t> image;
  uint16_t reset = 0;
  ExportStats st;
  if (!buildSongImage(song, DriverOptions(), &image, &reset, &st, err)) return false;
  if (!buildSongImage(song, titleScreen(song, st), &image, &reset, &st, err)) return false;
  size_t size = 0x8000;
  while (size < image.size()) size *= 2;
  image.resize(size, 0xFF);
  st.imageBytes = size;
  if (stats) *stats = st;
  *rom = image;
  return true;
}

bool exportRomFile(const Song& song, const char* path, ExportStats* stats, VerifyReport* rep,
                   std::string* err) {
  std::vector<uint8_t> rom;
  ExportStats st;
  VerifyReport vr;
  if (!exportRom(song, &rom, &st, err)) return false;
  if (stats) *stats = st;
  bool ok = verifyRom(song, rom, 0, &vr);
  if (rep) *rep = vr;
  if (!ok) { *err = "self-check failed: " + vr.detail; return false; }
  FILE* f = fopen(path, "wb");
  if (!f) { *err = std::string("cannot write ") + path; return false; }
  bool wrote = fwrite(&rom[0], 1, rom.size(), f) == rom.size();
  wrote = (fclose(f) == 0) && wrote;
  if (!wrote) *err = std::string("write error: ") + path;
  return wrote;
}

} // namespace pce
