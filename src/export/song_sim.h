// PCEtracker - frame-by-frame simulation of the player for the exporters.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// Runs the tracker's Player exactly as the audio renderer does (60 Hz ticks,
// DDA timer ticks in between) and reports, per frame, the PSG register writes
// and the DDA events the console driver needs.

#ifndef PCETRACKER_SONG_SIM_H
#define PCETRACKER_SONG_SIM_H

#include "tracker/player.h"

#include <stdint.h>
#include <utility>
#include <vector>

namespace pce {

struct DdaEvent {
  enum Type { START, STEP, STOP };
  Type type;
  uint8_t ch, sample;
  uint16_t step;
};

struct SimFrame {
  std::vector<std::pair<uint8_t, uint8_t> > regs; // (register, value) in driver order
  std::vector<DdaEvent> events;
};

// Values written to a channel's DDA register by one sample start.
struct DdaSegment {
  int frame;                 // frame of the START
  int ch;
  std::vector<uint8_t> values;
  int stepChangedAt;         // values.size() at the first STEP after the start, -1 = never
};

class SongSim {
public:
  explicit SongSim(const Song& song);
  // Runs one frame. Returns true if it was the last frame of a pass through
  // the song (the song wrapped). `segments` (optional) collects DDA output.
  bool frame(SimFrame* out, std::vector<DdaSegment>* segments);
  int frameCount() const { return frames_; }

private:
  Player player_;
  uint32_t timerNext_;
  bool prevOn_[kChannels];
  uint16_t prevStep_[kChannels];
  int frames_;
};

} // namespace pce

#endif
