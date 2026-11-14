// PCEtracker - hes_play: runs a HES file on the built-in HuC6280 machine and
// renders what its code writes to the PSG into a WAV file.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// Usage: hes_play in.hes out.wav [--seconds N] [--rate N] [--rev 6280|6280a]
// Only the instructions used by PCEtracker's driver are emulated; other HES
// files will most likely stop with "unknown opcode".

#include "audio/wav_writer.h"
#include "export/hes_machine.h"
#include "psg/psg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace pce;

int main(int argc, char** argv) {
  if (argc < 3) {
    printf("usage: hes_play in.hes out.wav [--seconds N] [--rate N] [--rev 6280|6280a]\n");
    return 2;
  }
  double seconds = 30;
  int rate = 44100;
  PsgRevision rev = PSG_REV_HUC6280A;
  for (int i = 3; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--seconds")) seconds = atof(argv[i + 1]);
    else if (!strcmp(argv[i], "--rate")) rate = atoi(argv[i + 1]);
    else if (!strcmp(argv[i], "--rev")) rev = strcmp(argv[i + 1], "6280") ? PSG_REV_HUC6280A : PSG_REV_HUC6280;
  }
  if (seconds <= 0 || seconds > 3600 || rate < 8000 || rate > 192000) { printf("bad option value\n"); return 2; }

  FILE* f = fopen(argv[1], "rb");
  if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
  std::vector<uint8_t> hes;
  unsigned char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) hes.insert(hes.end(), buf, buf + n);
  fclose(f);

  HesMachine m;
  std::string err;
  if (!m.load(hes, &err)) { printf("%s\n", err.c_str()); return 1; }
  Psg psg(rev, rate);
  WavWriter wav;
  if (!wav.open(argv[2], rate, 16)) { printf("cannot write %s\n", argv[2]); return 1; }

  const uint32_t frameCycles = 119210;
  const int frames = (int)(seconds * Psg::kClockHz / frameCycles);
  std::vector<HesMachine::PsgWrite> writes;
  std::vector<int16_t> pcm;
  for (int i = 0; i < frames; ++i) {
    writes.clear();
    if (!m.runFrame(&writes)) { printf("%s\n", m.error().c_str()); return 1; }
    uint32_t t = 0;
    for (size_t k = 0; k < writes.size(); ++k) {
      uint32_t c = writes[k].cycle < frameCycles ? writes[k].cycle : frameCycles;
      if (c > t) { psg.advance(c - t); t = c; }
      psg.write(writes[k].reg, writes[k].value);
    }
    psg.advance(frameCycles - t);
    size_t avail = psg.available();
    pcm.resize(avail * 2 + 2);
    avail = psg.read(&pcm[0], avail);
    wav.write(&pcm[0], avail);
  }
  if (!wav.close()) { printf("write error\n"); return 1; }
  printf("wrote %s: %d frames (%.1f s) of %s\n", argv[2], frames, seconds, argv[1]);
  return 0;
}
