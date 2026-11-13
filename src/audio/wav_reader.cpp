// PCEtracker - WAV import for DDA samples.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "audio/wav_reader.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace pce {

namespace {
uint32_t rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t rd16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

std::string baseName(const char* path) {
  std::string s(path);
  size_t sl = s.find_last_of("/\\");
  if (sl != std::string::npos) s.erase(0, sl + 1);
  size_t dot = s.rfind('.');
  if (dot != std::string::npos) s.erase(dot);
  return s;
}
}

void convertToDda(const float* mono, size_t count, double rate, Sample* out) {
  out->data.clear();
  out->loop = -1;
  if (!count || rate <= 0) return;
  // Linear-interpolation resampling to the timer rate, with a simple box
  // pre-filter when downsampling, then peak normalization to 5 bits.
  const double step = rate / kDdaRateHz;
  size_t n = (size_t)((double)count / step);
  if (n > (size_t)kMaxSampleLength) n = (size_t)kMaxSampleLength;
  std::vector<float> res(n);
  const int box = step > 1.0 ? (int)step : 1;
  float peak = 0;
  for (size_t i = 0; i < n; ++i) {
    double pos = i * step;
    size_t a = (size_t)pos;
    double f = pos - (double)a;
    float acc = 0;
    int taps = 0;
    for (int k = 0; k < box; ++k) {
      size_t j = a + (size_t)k;
      if (j >= count) break;
      float x0 = mono[j], x1 = j + 1 < count ? mono[j + 1] : x0;
      acc += (float)(x0 + (x1 - x0) * f);
      ++taps;
    }
    res[i] = taps ? acc / (float)taps : 0.0f;
    if (fabsf(res[i]) > peak) peak = fabsf(res[i]);
  }
  const float gain = peak > 1e-6f ? 1.0f / peak : 0.0f;
  out->data.resize(n);
  for (size_t i = 0; i < n; ++i) {
    int v = (int)floor(res[i] * gain * 15.5f + 15.5f + 0.5f);
    out->data[i] = (uint8_t)(v < 0 ? 0 : v > 31 ? 31 : v);
  }
}

bool importWavSample(const char* path, Sample* out, std::string* error) {
  FILE* f = fopen(path, "rb");
  if (!f) { *error = std::string("cannot open ") + path; return false; }
  std::vector<unsigned char> buf;
  unsigned char tmp[65536];
  size_t got;
  while ((got = fread(tmp, 1, sizeof tmp, f)) > 0) {
    buf.insert(buf.end(), tmp, tmp + got);
    if (buf.size() > 256u * 1024 * 1024) { fclose(f); *error = "file too large"; return false; }
  }
  fclose(f);
  if (buf.size() < 12 || memcmp(&buf[0], "RIFF", 4) || memcmp(&buf[8], "WAVE", 4)) {
    *error = "not a WAV file";
    return false;
  }
  int format = 0, channels = 0, bits = 0;
  uint32_t rate = 0;
  const unsigned char* data = 0;
  size_t dataLen = 0;
  for (size_t p = 12; p + 8 <= buf.size();) {
    uint32_t len = rd32(&buf[p + 4]);
    const unsigned char* body = &buf[p + 8];
    size_t avail = buf.size() - (p + 8);
    if (len > avail) len = (uint32_t)avail;
    if (!memcmp(&buf[p], "fmt ", 4) && len >= 16) {
      format = rd16(body);
      channels = rd16(body + 2);
      rate = rd32(body + 4);
      bits = rd16(body + 14);
      if (format == 0xFFFE && len >= 26) format = rd16(body + 24); // WAVE_FORMAT_EXTENSIBLE
    } else if (!memcmp(&buf[p], "data", 4)) {
      data = body;
      dataLen = len;
    }
    p += 8 + len + (len & 1);
  }
  if (!data || channels < 1 || rate == 0) { *error = "WAV has no audio data"; return false; }
  const bool isFloat = format == 3 && bits == 32;
  if (!(format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) && !isFloat) {
    *error = "unsupported WAV format (use PCM 8/16/24/32-bit or 32-bit float)";
    return false;
  }
  const size_t frameBytes = (size_t)channels * (bits / 8);
  const size_t frames = dataLen / frameBytes;
  // Only read as much as can survive the 9.4 s limit after resampling.
  const size_t maxFrames = (size_t)((double)kMaxSampleLength * rate / kDdaRateHz) + 2;
  const size_t use = frames < maxFrames ? frames : maxFrames;
  std::vector<float> mono(use);
  for (size_t i = 0; i < use; ++i) {
    float sum = 0;
    for (int c = 0; c < channels; ++c) {
      const unsigned char* s = data + i * frameBytes + (size_t)c * (bits / 8);
      float v;
      if (isFloat) { uint32_t u = rd32(s); memcpy(&v, &u, 4); }
      else if (bits == 8) v = (s[0] - 128) / 128.0f;
      else if (bits == 16) v = (int16_t)rd16(s) / 32768.0f;
      else if (bits == 24) v = (float)((int32_t)((s[0] << 8) | (s[1] << 16) | ((uint32_t)s[2] << 24)) >> 8) / 8388608.0f;
      else v = (float)((int32_t)rd32(s)) / 2147483648.0f;
      sum += v;
    }
    mono[i] = sum / (float)channels;
  }
  convertToDda(use ? &mono[0] : 0, use, (double)rate, out);
  out->name = baseName(path);
  if (out->data.empty()) { *error = "WAV is empty"; return false; }
  return true;
}

} // namespace pce
