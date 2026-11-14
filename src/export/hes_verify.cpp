// PCEtracker - verification of an exported HES against the tracker's player.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "export/hes_export.h"
#include "export/hes_machine.h"
#include "export/song_sim.h"
#include "psg/psg.h"

#include <stdio.h>

namespace pce {

namespace {
const int kMaxVerifyFrames = 60 * 60 * 10;

std::string fmt(const char* f, int a, int b = 0, int c = 0, int d = 0) {
  char buf[160];
  snprintf(buf, sizeof buf, f, a, b, c, d);
  return buf;
}
}

bool verifyHes(const Song& song, const std::vector<uint8_t>& hes, int frames, VerifyReport* rep) {
  *rep = VerifyReport();

  // --- reference: the tracker's player ---
  SongSim sim(song);
  std::vector<SimFrame> ref;
  std::vector<DdaSegment> segs;
  int passes = 0;
  while ((frames > 0 ? (int)ref.size() < frames : passes < 3) && (int)ref.size() < kMaxVerifyFrames) {
    ref.push_back(SimFrame());
    if (sim.frame(&ref.back(), &segs)) ++passes;
  }
  const int total = (int)ref.size();

  // --- the HES file on the 6280 machine ---
  HesMachine m;
  std::string err;
  if (!m.load(hes, &err)) { rep->detail = err; return false; }
  std::vector<HesMachine::PsgWrite> writes;
  // The driver switches the VBlank interrupt on during its start-up, so the
  // first music frame is the machine's second video frame (as on a console).
  if (!m.runFrame(&writes)) { rep->detail = m.error(); return false; }
  for (size_t i = 0; i < writes.size(); ++i)
    if (!writes[i].timer) { rep->detail = "frame code wrote to the PSG before the first VBlank"; return false; }
  writes.clear();
  for (int f = 0; f < total; ++f)
    if (!m.runFrame(&writes)) { rep->detail = m.error(); return false; }
  for (size_t i = 0; i < writes.size(); ++i) --writes[i].frame;

  // --- (a) frame code: every register write must be identical, frame by frame ---
  size_t wi = 0;
  std::vector<std::vector<std::pair<int, uint8_t> > > dda(kChannels); // (frame, value) per channel
  int timerSel = -1;
  for (int f = 0; f < total; ++f) {
    size_t k = 0;
    const std::vector<std::pair<uint8_t, uint8_t> >& want = ref[(size_t)f].regs;
    for (; wi < writes.size() && (int)writes[wi].frame == f; ++wi) {
      const HesMachine::PsgWrite& w = writes[wi];
      if (w.timer) {
        if (w.reg == REG_SELECT) timerSel = w.value;
        else if (w.reg == REG_WAVE_DATA && timerSel >= 0 && timerSel < kChannels)
          dda[(size_t)timerSel].push_back(std::make_pair(f, w.value));
        continue;
      }
      if (k >= want.size() || want[k].first != w.reg || want[k].second != w.value) {
        rep->detail = fmt("frame %d: register write %d differs (HES wrote $%02X to register %d)",
                          f, (int)k, w.value, w.reg);
        return false;
      }
      ++k;
    }
    if (k != want.size()) {
      rep->detail = fmt("frame %d: HES made %d register writes, the player %d", f, (int)k, (int)want.size());
      return false;
    }
    rep->regWrites += k;
  }

  // --- (b) DDA: each started sample must put out the expected values ---
  // The timer phase on a console is not defined relative to VBlank, so a note
  // may gain or lose a sample at its ends; the values in between must match.
  for (size_t s = 0; s < segs.size(); ++s) {
    const DdaSegment& seg = segs[s];
    int endFrame = total;
    for (size_t n = s + 1; n < segs.size(); ++n)
      if (segs[n].ch == seg.ch) { endFrame = segs[n].frame; break; }
    std::vector<uint8_t> got;
    const std::vector<std::pair<int, uint8_t> >& chw = dda[(size_t)seg.ch];
    for (size_t i = 0; i < chw.size(); ++i)
      if (chw[i].first >= seg.frame && chw[i].first < endFrame) got.push_back(chw[i].second);
    const size_t wantLen = seg.stepChangedAt >= 0 ? (size_t)seg.stepChangedAt : seg.values.size();
    if (wantLen < 8) continue; // too short to judge
    bool match = false;
    size_t compared = 0;
    for (size_t o = 0; o <= 2 && !match; ++o) {
      if (got.size() < o + 4) break;
      size_t n = wantLen < got.size() - o ? wantLen : got.size() - o;
      if (n < 4) break;
      n -= 2;
      match = true;
      for (size_t i = 0; i < n && match; ++i) match = got[o + i] == seg.values[i];
      if (match) {
        compared = n;
        const long diff = (long)(got.size() - o) - (long)seg.values.size();
        // The driver starts a sample after the frame's register writes (a few
        // ms on a busy frame), so a note that is cut may be shorter by that much.
        if (seg.stepChangedAt < 0 && endFrame != total && (diff > 3 || diff < -40)) match = false;
      }
    }
    if (!match) {
      rep->detail = fmt("DDA sample started at frame %d on channel %d: output differs (%d values from HES, %d expected)",
                        seg.frame, seg.ch + 1, (int)got.size(), (int)seg.values.size());
      return false;
    }
    rep->ddaWrites += compared;
  }

  rep->frames = total;
  rep->ok = true;
  return true;
}

} // namespace pce
