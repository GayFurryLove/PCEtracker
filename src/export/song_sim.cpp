// PCEtracker - frame-by-frame simulation of the player for the exporters.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "export/song_sim.h"
#include "audio/song_render.h"

namespace pce {

namespace {

struct RegRecorder : RegisterSink {
  std::vector<std::pair<uint8_t, uint8_t> >* out;
  virtual void write(uint8_t r, uint8_t v) { out->push_back(std::make_pair(r, v)); }
};

// Receives the timer-tick writes: select, sample value, select restore.
struct DdaRecorder : RegisterSink {
  std::vector<DdaSegment>* segs;
  int current[kChannels]; // index of the open segment per channel, -1 = none
  int sel;
  virtual void write(uint8_t r, uint8_t v) {
    if (r == REG_SELECT) { sel = v; return; }
    if (r != REG_WAVE_DATA || !segs || sel < 0 || sel >= kChannels || current[sel] < 0) return;
    (*segs)[(size_t)current[sel]].values.push_back(v);
  }
};

} // namespace

SongSim::SongSim(const Song& song) : player_(song), timerNext_(kDdaTimerClocks), frames_(0) {
  for (int c = 0; c < kChannels; ++c) { prevOn_[c] = false; prevStep_[c] = 0; }
  player_.playSong();
}

bool SongSim::frame(SimFrame* out, std::vector<DdaSegment>* segments) {
  out->regs.clear();
  out->events.clear();
  RegRecorder rec;
  rec.out = &out->regs;
  player_.tick(rec);

  // Per-channel index of the newest segment (segments are appended in order).
  DdaRecorder dda;
  dda.segs = segments;
  dda.sel = -1;
  for (int c = 0; c < kChannels; ++c) dda.current[c] = -1;
  if (segments)
    for (size_t i = 0; i < segments->size(); ++i) dda.current[(*segments)[i].ch] = (int)i;

  for (int c = 0; c < kChannels; ++c) {
    Player::DdaView v = player_.ddaView(c);
    DdaEvent e;
    e.ch = (uint8_t)c;
    e.sample = (uint8_t)(v.sample < 0 ? 0 : v.sample);
    e.step = v.step;
    if (v.on && v.pos == 0) {
      // A freshly triggered sample: its position is exactly 0 until the first timer tick.
      e.type = DdaEvent::START;
      out->events.push_back(e);
      if (segments) {
        DdaSegment s;
        s.frame = frames_;
        s.ch = c;
        s.stepChangedAt = -1;
        segments->push_back(s);
        dda.current[c] = (int)segments->size() - 1;
      }
    } else if (v.on && prevOn_[c] && v.step != prevStep_[c]) {
      e.type = DdaEvent::STEP;
      out->events.push_back(e);
      if (segments && dda.current[c] >= 0) {
        DdaSegment& s = (*segments)[(size_t)dda.current[c]];
        if (s.stepChangedAt < 0) s.stepChangedAt = (int)s.values.size();
      }
    } else if (!v.on && prevOn_[c]) {
      e.type = DdaEvent::STOP;
      out->events.push_back(e);
    }
    prevOn_[c] = v.on;
    prevStep_[c] = v.step;
  }

  // Timer interrupts until the next frame, as in SongRenderer::renderTick().
  if (player_.ddaActive()) {
    while (timerNext_ < kFrameClocks) {
      player_.ddaTick(dda);
      timerNext_ += kDdaTimerClocks;
    }
  } else {
    while (timerNext_ < kFrameClocks) timerNext_ += kDdaTimerClocks;
  }
  timerNext_ -= kFrameClocks;
  ++frames_;

  if (player_.songLooped()) {
    player_.clearLooped();
    // The real timer phase is unknown anyway (it differs by at most one
    // sample); restarting it per pass makes every pass reproducible.
    timerNext_ = kDdaTimerClocks;
    return true;
  }
  return false;
}

} // namespace pce
