// PCEtracker - HES export (modules 7/8: driver image + HES container).
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#ifndef PCETRACKER_HES_EXPORT_H
#define PCETRACKER_HES_EXPORT_H

#include "tracker/song.h"

#include <stdint.h>
#include <string>
#include <vector>

namespace pce {

struct ExportStats {
  int framesIntro;      // frames of the first pass through the song
  int framesLoop;       // frames of the repeating pass
  size_t streamBytes;
  size_t sampleBytes;
  size_t imageBytes;    // whole image, bank aligned
  bool cleanLoop;       // the third pass produced the same stream as the second
  ExportStats() : framesIntro(0), framesLoop(0), streamBytes(0), sampleBytes(0), imageBytes(0), cleanLoop(true) {}
};

// Builds the bank-aligned program image (driver + samples + stream).
bool buildSongImage(const Song& song, std::vector<uint8_t>* image, uint16_t* resetAddr,
                    ExportStats* stats, std::string* err);

// Wraps the image in a HES file.
bool exportHes(const Song& song, std::vector<uint8_t>* hes, ExportStats* stats, std::string* err);

struct VerifyReport {
  bool ok;
  int frames;           // frames compared
  size_t regWrites;     // frame (VBlank) register writes compared, all must be identical
  size_t ddaWrites;     // DDA sample writes checked
  std::string detail;   // first difference, if any
  VerifyReport() : ok(false), frames(0), regWrites(0), ddaWrites(0) {}
};

// Runs the HES file on the built-in HuC6280 machine and compares every PSG
// register write with what the tracker's player produces for the same song.
// frames <= 0: intro + two loops (capped at 10 minutes).
bool verifyHes(const Song& song, const std::vector<uint8_t>& hes, int frames, VerifyReport* rep);

// Export + verify + write. The file is only written when verification passes.
bool exportHesFile(const Song& song, const char* path, ExportStats* stats, VerifyReport* rep,
                   std::string* err);

} // namespace pce

#endif
