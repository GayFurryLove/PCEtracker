// PCEtracker - WAV file writer.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "wav_writer.h"

namespace pce {

namespace {
void put16(unsigned char* p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
void put32(unsigned char* p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }
}

WavWriter::WavWriter() : file_(0), sampleRate_(0), bits_(16), dataBytes_(0), ok_(false) {}

WavWriter::~WavWriter() { close(); }

bool WavWriter::open(const char* path, int sampleRate, int bits) {
  close();
  if (bits != 16 && bits != 24) return false;
  file_ = fopen(path, "wb");
  if (!file_) return false;
  sampleRate_ = sampleRate;
  bits_ = bits;
  dataBytes_ = 0;
  ok_ = writeHeader();
  return ok_;
}

bool WavWriter::writeHeader() {
  unsigned char h[44];
  const uint32_t blockAlign = 2u * (uint32_t)(bits_ / 8);
  h[0] = 'R'; h[1] = 'I'; h[2] = 'F'; h[3] = 'F';
  put32(h + 4, 36u + dataBytes_);
  h[8] = 'W'; h[9] = 'A'; h[10] = 'V'; h[11] = 'E';
  h[12] = 'f'; h[13] = 'm'; h[14] = 't'; h[15] = ' ';
  put32(h + 16, 16);
  put16(h + 20, 1); // PCM
  put16(h + 22, 2); // stereo
  put32(h + 24, (uint32_t)sampleRate_);
  put32(h + 28, (uint32_t)sampleRate_ * blockAlign);
  put16(h + 32, blockAlign);
  put16(h + 34, (uint32_t)bits_);
  h[36] = 'd'; h[37] = 'a'; h[38] = 't'; h[39] = 'a';
  put32(h + 40, dataBytes_);
  return fseek(file_, 0, SEEK_SET) == 0 && fwrite(h, 1, 44, file_) == 44;
}

bool WavWriter::write(const int16_t* stereo, size_t frames) {
  if (!file_) return false;
  unsigned char buf[3 * 2 * 256];
  const size_t bps = (size_t)(bits_ / 8);
  while (frames > 0) {
    size_t n = frames > 256 ? 256 : frames;
    unsigned char* p = buf;
    for (size_t i = 0; i < n * 2; ++i) {
      int32_t s = stereo[i];
      if (bits_ == 24) {
        // The PSG mixer produces 16-bit values; 24-bit output is the same
        // signal with 8 extra zero bits (headroom for later processing).
        uint32_t v = (uint32_t)(s * 256);
        p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16);
      } else {
        uint32_t v = (uint32_t)s;
        p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
      }
      p += bps;
    }
    size_t bytes = (size_t)(p - buf);
    if (fwrite(buf, 1, bytes, file_) != bytes) ok_ = false;
    dataBytes_ += (uint32_t)bytes;
    stereo += n * 2;
    frames -= n;
  }
  return ok_;
}

bool WavWriter::close() {
  if (!file_) return true;
  bool ok = ok_ && writeHeader();
  ok = (fclose(file_) == 0) && ok;
  file_ = 0;
  return ok;
}

} // namespace pce
