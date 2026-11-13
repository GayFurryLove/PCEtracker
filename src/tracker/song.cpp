// PCEtracker - song data model.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "tracker/song.h"

#include <string.h>

namespace pce {

bool Cell::empty() const {
  if (note != NOTE_NONE || instrument != INST_NONE || volume != VOL_NONE) return false;
  for (int i = 0; i < kEffectColumns; ++i) if (fx[i].cmd) return false;
  return true;
}

bool Cell::operator==(const Cell& o) const {
  if (note != o.note || instrument != o.instrument || volume != o.volume) return false;
  for (int i = 0; i < kEffectColumns; ++i) if (!(fx[i] == o.fx[i])) return false;
  return true;
}

void Pattern::resize(int r) {
  if (r < 1) r = 1;
  if (r > kMaxRows) r = kMaxRows;
  rows = r;
  cells.resize((size_t)r * kChannels);
}

Wave::Wave() {
  // Default: square wave, so a fresh instrument is audible.
  for (int i = 0; i < kWaveLength; ++i) data[i] = i < kWaveLength / 2 ? kWaveMax : 0;
}

bool Wave::operator==(const Wave& o) const { return memcmp(data, o.data, sizeof data) == 0; }

Song::Song() : speed(6) {
  orders.push_back(0);
  patterns.push_back(Pattern(64));
  instruments.push_back(Instrument());
  instruments[0].name = "default";
  waves.push_back(Wave());
}

Pattern& Song::pattern(int i) {
  while ((int)patterns.size() <= i) patterns.push_back(Pattern(64));
  return patterns[i];
}

static const char* kNames[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };

std::string noteName(uint8_t note) {
  if (note == NOTE_NONE) return "---";
  if (note == NOTE_OFF) return "OFF";
  if (note >= kNoteCount) return "???";
  std::string s(kNames[note % 12]);
  s += (char)('1' + note / 12);
  return s;
}

bool parseNoteName(const std::string& s, uint8_t* note) {
  if (s == "---" || s == "...") { *note = NOTE_NONE; return true; }
  if (s == "OFF" || s == "===") { *note = NOTE_OFF; return true; }
  if (s.size() != 3) return false;
  for (int i = 0; i < 12; ++i) {
    if (s[0] == kNames[i][0] && s[1] == kNames[i][1] && s[2] >= '1' && s[2] <= '8') {
      *note = (uint8_t)((s[2] - '1') * 12 + i);
      return true;
    }
  }
  return false;
}

// Equal temperament, A-4 = 440 Hz: round(7159090.9 / (64 * hz)).
// Fixed constants (not computed at run time) so that every platform and the
// exported 6280 driver use bit-identical periods.
static const uint16_t kPeriods[kNoteCount] = {
  3420, 3229, 3047, 2876, 2715, 2562, 2419, 2283, 2155, 2034, 1920, 1812, // 1
  1710, 1614, 1524, 1438, 1357, 1281, 1209, 1141, 1077, 1017,  960,  906, // 2
   855,  807,  762,  719,  679,  641,  605,  571,  539,  508,  480,  453, // 3
   428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,  226, // 4
   214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,  113, // 5
   107,  101,   95,   90,   85,   80,   76,   71,   67,   64,   60,   57, // 6
    53,   50,   48,   45,   42,   40,   38,   36,   34,   32,   30,   28, // 7
    27,   25,   24,   22,   21,   20,   19,   18,   17,   16,   15,   14, // 8
};

uint16_t notePeriod(int note) {
  if (note < 0) note = 0;
  if (note >= kNoteCount) note = kNoteCount - 1;
  return kPeriods[note];
}

} // namespace pce
