// PCEtracker - pcet_render: render a .pcet song (or one pattern) to WAV.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "audio/song_render.h"
#include "export/hes_export.h"
#include "project/text_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace pce;

static void usage() {
  printf("usage: pcet_render song.pcet out.wav [options]\n"
         "  --rate N        sample rate, 8000..192000 (default 44100)\n"
         "  --bits 16|24    sample format (default 16)\n"
         "  --rev 6280|6280a  chip revision (default 6280a)\n"
         "  --pattern N     render only pattern N (hex), once\n"
         "  --loops N       extra passes of the whole song (default 0)\n"
         "  --tail SEC      silence after the end (default 1)\n"
         "  --resave FILE   also write the song back in canonical text form\n"
         "  --hes FILE      also export a HES file (PC Engine sound format)\n"
         "  --rom FILE      also export a HuCard ROM (.pce) that plays the song\n");
}

int main(int argc, char** argv) {
  if (argc < 3) { usage(); return 2; }
  RenderOptions opt;
  const char* resave = 0;
  const char* hesPath = 0;
  const char* romPath = 0;
  for (int i = 3; i < argc; ++i) {
    const char* a = argv[i];
    const char* v = i + 1 < argc ? argv[i + 1] : 0;
    if (!v) { usage(); return 2; }
    if (!strcmp(a, "--rate")) opt.sampleRate = atoi(v);
    else if (!strcmp(a, "--bits")) opt.bits = atoi(v);
    else if (!strcmp(a, "--rev")) opt.revision = strcmp(v, "6280") ? PSG_REV_HUC6280A : PSG_REV_HUC6280;
    else if (!strcmp(a, "--pattern")) opt.pattern = (int)strtol(v, 0, 16);
    else if (!strcmp(a, "--loops")) opt.loops = atoi(v);
    else if (!strcmp(a, "--tail")) opt.fadeSeconds = atof(v);
    else if (!strcmp(a, "--resave")) resave = v;
    else if (!strcmp(a, "--hes")) hesPath = v;
    else if (!strcmp(a, "--rom")) romPath = v;
    else { usage(); return 2; }
    ++i;
  }
  if (opt.sampleRate < 8000 || opt.sampleRate > 192000 || (opt.bits != 16 && opt.bits != 24) ||
      opt.loops < 0 || opt.fadeSeconds < 0 || opt.fadeSeconds > 60) {
    printf("bad option value\n");
    return 2;
  }

  Song song;
  std::string err;
  if (!loadSongFile(argv[1], &song, &err)) { printf("%s: %s\n", argv[1], err.c_str()); return 1; }
  if (resave && !saveSongFile(resave, song, &err)) { printf("%s\n", err.c_str()); return 1; }

  if (hesPath) {
    ExportStats st;
    VerifyReport vr;
    if (!exportHesFile(song, hesPath, &st, &vr, &err)) { printf("HES export failed: %s\n", err.c_str()); return 1; }
    printf("wrote %s: %u bytes (stream %u, samples %u), %d + %d frames%s\n", hesPath,
           (unsigned)(st.imageBytes + 0x20), (unsigned)st.streamBytes, (unsigned)st.sampleBytes,
           st.framesIntro, st.framesLoop, st.cleanLoop ? "" : "  WARNING: the loop does not repeat exactly");
    printf("  self-check on the built-in 6280 machine: %d frames, %u register writes identical, %u DDA values\n",
           vr.frames, (unsigned)vr.regWrites, (unsigned)vr.ddaWrites);
  }

  if (romPath) {
    ExportStats st;
    VerifyReport vr;
    if (!exportRomFile(song, romPath, &st, &vr, &err)) { printf("ROM export failed: %s\n", err.c_str()); return 1; }
    printf("wrote %s: %u KB HuCard image\n", romPath, (unsigned)(st.imageBytes / 1024));
    printf("  self-check (boot like a console): %d frames, %u register writes identical, %u DDA values\n",
           vr.frames, (unsigned)vr.regWrites, (unsigned)vr.ddaWrites);
  }

  double sec = 0;
  if (!renderSongToWav(song, opt, argv[2], &err, &sec)) { printf("%s\n", err.c_str()); return 1; }
  printf("wrote %s: %.2f s, %d Hz, %d-bit, %s\n", argv[2], sec, opt.sampleRate, opt.bits,
         opt.revision == PSG_REV_HUC6280 ? "HuC6280" : "HuC6280A");
  return 0;
}
