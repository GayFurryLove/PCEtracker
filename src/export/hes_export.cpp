// PCEtracker - HES export.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "export/hes_export.h"
#include "export/driver6280.h"
#include "export/song_sim.h"
#include "psg/psg.h"

#include <stdio.h>
#include <string.h>

namespace pce {

namespace {

const int kMaxPassFrames = 60 * 60 * 20; // 20 minutes per pass
const int kMaxBanks = 0x80;              // HES / HuCard address space used here: 1 MiB

// Finds the wave whose 32 samples equal the 32 data writes starting at regs[i].
int findWave(const Song& song, const std::vector<std::pair<uint8_t, uint8_t> >& regs, size_t i) {
  for (size_t w = 0; w < song.waves.size() && w < (size_t)kMaxWaves; ++w) {
    bool same = true;
    for (int k = 0; k < kWaveLength && same; ++k)
      same = regs[i + (size_t)k].second == (song.waves[w].data[k] & 0x1F);
    if (same) return (int)w;
  }
  return -1;
}

// One frame -> stream bytes (without the end-of-frame byte).
// Order matters on the console: DDA must stop before the frame code may leave
// DDA mode, and must start only after the frame code has entered it -
// otherwise a sample write would land in the wave RAM.
void encodeFrame(const Song& song, const SimFrame& f, std::vector<uint8_t>* out) {
  for (size_t i = 0; i < f.events.size(); ++i) {
    const DdaEvent& e = f.events[i];
    if (e.type == DdaEvent::STOP) out->push_back((uint8_t)(SC_DDA_STOP + e.ch));
    if (e.type == DdaEvent::STEP) {
      out->push_back((uint8_t)(SC_DDA_STEP + e.ch));
      out->push_back((uint8_t)e.step);
      out->push_back((uint8_t)(e.step >> 8));
    }
  }
  const std::vector<std::pair<uint8_t, uint8_t> >& r = f.regs;
  for (size_t i = 0; i < r.size();) {
    // Wave upload: $0804=$40, $0804=$00, 32 x $0806 -> one 2-byte command.
    if (i + 34 <= r.size() && r[i].first == REG_CONTROL && r[i].second == 0x40 &&
        r[i + 1].first == REG_CONTROL && r[i + 1].second == 0x00) {
      bool all = true;
      for (int k = 0; k < kWaveLength && all; ++k) all = r[i + 2 + (size_t)k].first == REG_WAVE_DATA;
      int w = all ? findWave(song, r, i + 2) : -1;
      if (w >= 0) {
        out->push_back(SC_WAVE);
        out->push_back((uint8_t)w);
        i += 34;
        continue;
      }
    }
    out->push_back(r[i].first);
    out->push_back(r[i].second);
    ++i;
  }
  for (size_t i = 0; i < f.events.size(); ++i) {
    const DdaEvent& e = f.events[i];
    if (e.type != DdaEvent::START) continue;
    out->push_back((uint8_t)(SC_DDA_START + e.ch));
    out->push_back(e.sample);
    out->push_back((uint8_t)e.step);
    out->push_back((uint8_t)(e.step >> 8));
  }
}

// Encodes one pass through the song. Returns its frame count, or -1 if it never ends.
int encodePass(const Song& song, SongSim& sim, std::vector<uint8_t>* out) {
  SimFrame f;
  std::vector<uint8_t> bytes;
  long lastEnd = -1; // position of the last end-of-frame byte in *out
  for (int n = 1; n <= kMaxPassFrames; ++n) {
    bool last = sim.frame(&f, 0);
    bytes.clear();
    encodeFrame(song, f, &bytes);
    if (bytes.empty() && lastEnd >= 0 && (*out)[(size_t)lastEnd] < SC_END_FRAME + kMaxWaitFrames) {
      ++(*out)[(size_t)lastEnd]; // one more frame without commands
    } else {
      out->insert(out->end(), bytes.begin(), bytes.end());
      out->push_back(SC_END_FRAME);
      lastEnd = (long)out->size() - 1;
    }
    if (last) return n;
  }
  return -1;
}

void put16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void put32(uint8_t* p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

} // namespace

bool buildSongImage(const Song& song, const DriverOptions& opt, std::vector<uint8_t>* image,
                    uint16_t* resetAddr, ExportStats* stats, std::string* err) {
  if (song.waves.size() > (size_t)kMaxWaves || song.samples.size() > (size_t)kMaxSamples) {
    *err = "too many waves or samples";
    return false;
  }

  // --- samples: one byte per 5-bit value, back to back from bank 1 ---
  std::vector<uint8_t> sampleData;
  std::vector<size_t> sampleStart(song.samples.size());
  for (size_t i = 0; i < song.samples.size(); ++i) {
    sampleStart[i] = sampleData.size();
    for (size_t k = 0; k < song.samples[i].data.size(); ++k) sampleData.push_back(song.samples[i].data[k] & 0x1F);
  }
  const int sampleBanks = (int)((sampleData.size() + kBankSize - 1) / kBankSize);
  const int streamBank = 1 + sampleBanks;

  // --- stream: first pass, repeating pass, jump back ---
  SongSim sim(song);
  std::vector<uint8_t> pass1, pass2, pass3;
  int n1 = encodePass(song, sim, &pass1);
  int n2 = n1 < 0 ? -1 : encodePass(song, sim, &pass2);
  int n3 = n2 < 0 ? -1 : encodePass(song, sim, &pass3);
  if (n1 < 0 || n2 < 0 || n3 < 0) { *err = "song is longer than 20 minutes or never loops"; return false; }
  std::vector<uint8_t> stream = pass1;
  const size_t loopAt = stream.size();
  stream.insert(stream.end(), pass2.begin(), pass2.end());
  stream.push_back(SC_JUMP);
  stream.push_back((uint8_t)(kStreamWindow + loopAt % kBankSize));
  stream.push_back((uint8_t)((kStreamWindow + loopAt % kBankSize) >> 8));
  stream.push_back((uint8_t)(streamBank + loopAt / kBankSize));
  const int streamBanks = (int)((stream.size() + kBankSize - 1) / kBankSize);
  if (streamBank + streamBanks > kMaxBanks) { *err = "song does not fit into 1 MiB"; return false; }

  // --- bank 0: driver + tables ---
  std::vector<uint8_t> bank0;
  if (!buildDriverBank((uint8_t)streamBank, opt, &bank0, resetAddr, err)) return false;
  for (size_t w = 0; w < song.waves.size(); ++w)
    for (int k = 0; k < kWaveLength; ++k)
      bank0[(size_t)(kWaveTable - kDriverOrg) + w * kWaveLength + (size_t)k] = song.waves[w].data[k] & 0x1F;
  uint8_t* tab = &bank0[(size_t)(kSampleTables - kDriverOrg)];
  for (size_t i = 0; i < song.samples.size(); ++i) {
    const Sample& s = song.samples[i];
    // Positions are stored the way the timer code keeps them: (bank, address in the $A000 window).
    const size_t pos[3] = { sampleStart[i], sampleStart[i] + s.data.size(),
                            sampleStart[i] + (size_t)(s.loop >= 0 ? s.loop : 0) };
    for (int k = 0; k < 3; ++k) {
      const uint16_t addr = (uint16_t)(kSampleWindow + pos[k] % kBankSize);
      tab[(ST_START_BANK + k * 3) * 64 + i] = (uint8_t)(1 + pos[k] / kBankSize);
      tab[(ST_START_LO + k * 3) * 64 + i] = (uint8_t)addr;
      tab[(ST_START_HI + k * 3) * 64 + i] = (uint8_t)(addr >> 8);
    }
    if (s.loop < 0 || s.loop >= (int)s.data.size()) tab[ST_LOOP_BANK * 64 + i] = 0xFF;
  }

  image->assign((size_t)(streamBank + streamBanks) * kBankSize, 0x00);
  memcpy(&(*image)[0], &bank0[0], kBankSize);
  if (!sampleData.empty()) memcpy(&(*image)[kBankSize], &sampleData[0], sampleData.size());
  memcpy(&(*image)[(size_t)streamBank * kBankSize], &stream[0], stream.size());

  if (stats) {
    stats->framesIntro = n1;
    stats->framesLoop = n2;
    stats->streamBytes = stream.size();
    stats->sampleBytes = sampleData.size();
    stats->imageBytes = image->size();
    stats->cleanLoop = pass3 == pass2;
  }
  return true;
}

bool exportHes(const Song& song, std::vector<uint8_t>* hes, ExportStats* stats, std::string* err) {
  std::vector<uint8_t> image;
  uint16_t reset = 0;
  if (!buildSongImage(song, DriverOptions(), &image, &reset, stats, err)) return false;
  hes->assign(0x20 + image.size(), 0x00);
  uint8_t* h = &(*hes)[0];
  memcpy(h, "HESM", 4);
  h[4] = 0;                 // version
  h[5] = 0;                 // first song
  put16(h + 6, reset);      // init address
  static const uint8_t mpr[8] = { 0xFF, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  memcpy(h + 8, mpr, 8);    // I/O, RAM, ..., bank 0 at $E000
  memcpy(h + 0x10, "DATA", 4);
  put32(h + 0x14, (uint32_t)image.size());
  put32(h + 0x18, 0);       // load address: bank 0
  memcpy(h + 0x20, &image[0], image.size());
  return true;
}

bool exportHesFile(const Song& song, const char* path, ExportStats* stats, VerifyReport* rep,
                   std::string* err) {
  std::vector<uint8_t> hes;
  ExportStats st;
  VerifyReport vr;
  if (!exportHes(song, &hes, &st, err)) return false;
  if (stats) *stats = st;
  bool ok = verifyHes(song, hes, 0, &vr);
  if (rep) *rep = vr;
  if (!ok) { *err = "self-check failed: " + vr.detail; return false; }
  FILE* f = fopen(path, "wb");
  if (!f) { *err = std::string("cannot write ") + path; return false; }
  bool wrote = fwrite(&hes[0], 1, hes.size(), f) == hes.size();
  wrote = (fclose(f) == 0) && wrote;
  if (!wrote) *err = std::string("write error: ") + path;
  return wrote;
}

} // namespace pce
