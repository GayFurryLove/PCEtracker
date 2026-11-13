// PCEtracker - audio renderer (module 5): Player -> PSG -> PCM.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#ifndef PCETRACKER_SONG_RENDER_H
#define PCETRACKER_SONG_RENDER_H

#include "psg/psg.h"
#include "tracker/player.h"

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace pce {

// One NTSC frame in PSG clocks: 262 lines * 1365 master clocks / 3.
const uint32_t kFrameClocks = 119210;

// Feeds player register writes into a Psg and produces audio, one tick at a time.
// Used both for offline WAV export and (stage 3) the SDL audio callback.
class SongRenderer : private RegisterSink {
public:
  SongRenderer(const Song& song, PsgRevision rev, int sampleRate);

  Player& player() { return player_; }
  Psg& psg() { return psg_; }

  // Runs one tick and appends its audio (interleaved stereo) to `out`.
  // Returns false when the player has stopped.
  bool renderTick(std::vector<int16_t>& out);

private:
  virtual void write(uint8_t reg, uint8_t value);
  Player player_;
  Psg psg_;
};

struct RenderOptions {
  PsgRevision revision;
  int sampleRate;
  int bits;            // 16 or 24
  int pattern;         // -1 = whole song, otherwise render this pattern once
  int loops;           // whole song: extra passes after the first (0 = play once)
  double fadeSeconds;  // silence tail after the end
  RenderOptions() : revision(PSG_REV_HUC6280A), sampleRate(44100), bits(16),
                    pattern(-1), loops(0), fadeSeconds(1.0) {}
};

// Renders to a WAV file. Returns false and sets `error` on failure.
bool renderSongToWav(const Song& song, const RenderOptions& opt, const char* path,
                     std::string* error, double* seconds = 0);

} // namespace pce

#endif
