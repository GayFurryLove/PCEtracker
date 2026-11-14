// PCEtracker - psg_demo: renders a short register-level demo to a WAV file.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// Usage: psg_demo out.wav [--rate 44100] [--bits 16|24] [--rev 6280|6280a]
//
// The demo drives the PSG exactly like console code would: register writes
// once per 60 Hz frame, plus DDA writes from a ~7 kHz timer.

#include "audio/wav_writer.h"
#include "psg/psg.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace pce;

namespace {

// NTSC frame: 263 lines * 1365 master clocks = 358995 master clocks (59.826 Hz).
// The PSG (and the fast CPU clock) run at master/3, so one frame = 119665 PSG clocks.
const uint32_t kFrameClocks = 119665;
// HuC6280 timer: 7.16 MHz / 1024 per tick; reload 0 = 1 tick -> ~6.99 kHz.
const uint32_t kTimerClocks = 1024;

double noteHz(int semitoneFromA4) { return 440.0 * pow(2.0, semitoneFromA4 / 12.0); }

void setPeriod(Psg& p, int ch, uint16_t period) {
  p.writeChannel(ch, REG_FREQ_LO, (uint8_t)(period & 0xFF));
  p.write(REG_FREQ_HI, (uint8_t)(period >> 8));
}

void loadWave(Psg& p, int ch, const uint8_t* w) {
  p.writeChannel(ch, REG_CONTROL, 0x00);
  for (int i = 0; i < 32; ++i) p.write(REG_WAVE_DATA, w[i]);
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("usage: psg_demo out.wav [--rate N] [--bits 16|24] [--rev 6280|6280a]\n");
    return 2;
  }
  int rate = 44100, bits = 16;
  PsgRevision rev = PSG_REV_HUC6280A;
  for (int i = 2; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--rate")) rate = atoi(argv[i + 1]);
    else if (!strcmp(argv[i], "--bits")) bits = atoi(argv[i + 1]);
    else if (!strcmp(argv[i], "--rev")) rev = strcmp(argv[i + 1], "6280") ? PSG_REV_HUC6280A : PSG_REV_HUC6280;
  }
  if (rate < 8000 || rate > 192000 || (bits != 16 && bits != 24)) {
    printf("bad --rate or --bits\n");
    return 2;
  }

  Psg psg(rev, rate);
  WavWriter wav;
  if (!wav.open(argv[1], rate, bits)) { printf("cannot write %s\n", argv[1]); return 1; }

  uint8_t saw[32], tri[32], pulse[32];
  for (int i = 0; i < 32; ++i) {
    saw[i] = (uint8_t)i;
    tri[i] = (uint8_t)(i < 16 ? i * 2 : (31 - i) * 2);
    pulse[i] = i < 8 ? 0x1F : 0x00;
  }
  psg.write(REG_MAIN_VOLUME, 0xEE);
  loadWave(psg, 0, saw);
  loadWave(psg, 1, tri);
  loadWave(psg, 3, pulse);
  for (int ch = 0; ch < 6; ++ch) psg.writeChannel(ch, REG_BALANCE, 0xFF);
  psg.writeChannel(3, REG_BALANCE, 0xF9); // a bit to the left

  const int melody[] = { 0, 3, 7, 12, 7, 3, -2, 2, 5, 10, 5, 2 };
  const int bass[] = { -24, -24, -26, -26 };
  std::vector<int16_t> buf;
  const int frames = 60 * 8;
  for (int f = 0; f < frames; ++f) {
    int step = f / 10;
    // ch1: saw lead, software volume decay per frame
    int vol = 0x1F - (f % 10) * 2;
    if (f % 10 == 0) setPeriod(psg, 0, psgPeriodForHz(noteHz(melody[step % 12])));
    psg.writeChannel(0, REG_CONTROL, (uint8_t)(0x80 | vol));
    // ch2: triangle bass
    if (f % 40 == 0) {
      setPeriod(psg, 1, psgPeriodForHz(noteHz(bass[(f / 40) % 4])));
      psg.writeChannel(1, REG_CONTROL, 0x80 | 0x1C);
    }
    // ch4: pulse arpeggio, 3 notes per row, one per frame
    static const int arp[3] = { 0, 4, 7 };
    setPeriod(psg, 3, psgPeriodForHz(noteHz(arp[f % 3] + 12)));
    psg.writeChannel(3, REG_CONTROL, (uint8_t)(f >= 240 ? 0x80 | 0x14 : 0x00));
    // ch5: noise hat every 20 frames
    if (f % 20 == 0) psg.writeChannel(4, REG_NOISE, 0x80 | 0x1C);
    int nvol = 0x1A - (f % 20) * 3;
    psg.writeChannel(4, REG_CONTROL, (uint8_t)(nvol > 0 ? 0x80 | nvol : 0x00));

    // ch3: DDA "kick" every 40 frames, driven from the timer like real code
    bool kick = (f % 40) < 6;
    psg.writeChannel(2, REG_CONTROL, kick ? 0xC0 | 0x1F : 0x00);
    uint32_t left = kFrameClocks;
    int t = 0;
    while (left >= kTimerClocks) {
      if (kick) {
        int age = (f % 40) * (int)(kFrameClocks / kTimerClocks) + t;
        double env = exp(-age / 900.0);
        double ph = 2.0 * 3.14159265 * 60.0 * age / 6991.0 * (1.0 + 2.0 * env);
        int s = 16 + (int)floor(15.0 * env * sin(ph) + 0.5);
        psg.writeChannel(2, REG_WAVE_DATA, (uint8_t)(s < 0 ? 0 : s > 31 ? 31 : s));
      }
      psg.advance(kTimerClocks);
      left -= kTimerClocks;
      ++t;
    }
    psg.advance(left);

    size_t n = psg.available();
    buf.resize(n * 2 + 2);
    n = psg.read(&buf[0], n);
    wav.write(&buf[0], n);
  }
  if (!wav.close()) { printf("write error\n"); return 1; }
  printf("wrote %s (%d Hz, %d-bit, %s)\n", argv[1], rate, bits,
         rev == PSG_REV_HUC6280 ? "HuC6280" : "HuC6280A");
  return 0;
}
