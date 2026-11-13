// PCEtracker - WAV import for DDA samples.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#ifndef PCETRACKER_WAV_READER_H
#define PCETRACKER_WAV_READER_H

#include "tracker/song.h"

#include <string>

namespace pce {

// Reads a PCM WAV (8/16/24/32-bit int or 32-bit float, any channels, any rate),
// mixes it to mono, resamples it to the DDA timer rate (~6991 Hz), normalizes
// and quantizes it to 5 bits - exactly what the console can play back.
// Longer files are cut at kMaxSampleLength (~9.4 s).
bool importWavSample(const char* path, Sample* out, std::string* error);

// Same conversion from memory (mono float -1..1 at `rate` Hz). Used by tests.
void convertToDda(const float* mono, size_t count, double rate, Sample* out);

} // namespace pce

#endif
