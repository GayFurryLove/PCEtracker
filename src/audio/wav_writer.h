// PCEtracker - WAV file writer (PCM 16/24-bit, stereo).
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#ifndef PCETRACKER_WAV_WRITER_H
#define PCETRACKER_WAV_WRITER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace pce {

// Streams interleaved stereo frames to a .wav file; the header is patched on close().
class WavWriter {
public:
  WavWriter();
  ~WavWriter();

  // bits: 16 or 24. Returns false if the file cannot be created.
  bool open(const char* path, int sampleRate, int bits);
  bool write(const int16_t* stereo, size_t frames);
  bool close();
  bool isOpen() const { return file_ != 0; }

private:
  WavWriter(const WavWriter&);
  WavWriter& operator=(const WavWriter&);
  bool writeHeader();

  FILE* file_;
  int sampleRate_;
  int bits_;
  uint32_t dataBytes_;
  bool ok_;
};

} // namespace pce

#endif
