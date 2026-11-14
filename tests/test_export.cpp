// PCEtracker - tests for the 6280 driver and the HES export: the exported
// file runs on the built-in HuC6280 machine and must reproduce the player.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "export/driver6280.h"
#include "export/hes_export.h"
#include "export/hes_machine.h"
#include "project/text_format.h"

#include <stdio.h>
#include <string.h>

using namespace pce;

static int g_failed = 0;
static int g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; \
  printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Cell cellOf(uint8_t n, uint8_t inst, char fx = 0, uint8_t p = 0) {
  Cell c;
  c.note = n; c.instrument = inst;
  if (fx) c.fx[0] = Effect((uint8_t)fx, p);
  return c;
}

static bool exportAndVerify(const Song& s, const char* name, ExportStats* st, std::vector<uint8_t>* hesOut = 0) {
  std::vector<uint8_t> hes;
  std::string err;
  if (!exportHes(s, &hes, st, &err)) { printf("  %s: export failed: %s\n", name, err.c_str()); return false; }
  VerifyReport vr;
  bool ok = verifyHes(s, hes, 0, &vr);
  printf("  %s: %u bytes, stream %u, samples %u, frames %d+%d, clean loop %d -> %s",
         name, (unsigned)hes.size(), (unsigned)st->streamBytes, (unsigned)st->sampleBytes,
         st->framesIntro, st->framesLoop, (int)st->cleanLoop, ok ? "verified" : "FAILED");
  if (ok) printf(" (%d frames, %u reg writes, %u DDA values)\n", vr.frames, (unsigned)vr.regWrites, (unsigned)vr.ddaWrites);
  else printf(": %s\n", vr.detail.c_str());
  if (hesOut) *hesOut = hes;
  return ok;
}

static void testDriverBuilds() {
  std::vector<uint8_t> bank;
  uint16_t reset = 0;
  std::string err;
  CHECK(buildDriverBank(3, &bank, &reset, &err));
  CHECK(bank.size() == (size_t)kBankSize);
  CHECK(reset >= kDriverOrg && reset < kWaveTable);
  // reset vector at $FFFE points to the reset code
  CHECK((bank[0x1FFE] | (bank[0x1FFF] << 8)) == reset);
}

static void testDefaultSong() {
  Song s;
  s.pattern(0).at(0, 0) = cellOf(45, 0);
  ExportStats st;
  CHECK(exportAndVerify(s, "one note", &st));
  CHECK(st.framesIntro == 64 * 6 && st.framesLoop == 64 * 6);
  CHECK(st.cleanLoop);
}

static void testDemo(const char* path) {
  Song s;
  std::string err;
  if (!loadSongFile(path, &s, &err)) { printf("  %s\n", err.c_str()); CHECK(false); return; }
  ExportStats st;
  std::vector<uint8_t> hes;
  CHECK(exportAndVerify(s, "demo", &st, &hes));
  CHECK(st.cleanLoop);
  CHECK(memcmp(&hes[0], "HESM", 4) == 0 && hes[8] == 0xFF && hes[9] == 0xF8);

  // The verifier must notice a wrong byte: damage one register value in the stream.
  const size_t streamAt = 0x20 + (size_t)(1 + (st.sampleBytes + kBankSize - 1) / kBankSize) * kBankSize;
  std::vector<uint8_t> bad = hes;
  bad[streamAt + 1] ^= 0x01;
  VerifyReport vr;
  CHECK(!verifyHes(s, bad, 120, &vr));
  // ... and an instruction the machine does not know stops the run.
  bad = hes;
  bad[0x20] = 0x02; // first driver byte -> undefined opcode
  CHECK(!verifyHes(s, bad, 10, &vr));
  CHECK(vr.detail.find("unknown opcode") != std::string::npos);
}

// Big and looped samples on several channels at once, bank crossings in both
// the sample data and the stream, wave changes, vibrato on every frame.
static void testStress() {
  Song s;
  s.speed = 6;
  s.waves.push_back(Wave());
  for (int i = 0; i < kWaveLength; ++i) s.waves[1].data[i] = (uint8_t)i;

  Sample big;
  for (int i = 0; i < 20000; ++i) big.data.push_back((uint8_t)((i * 7 + 3) % 32));
  Sample looped;
  for (int i = 0; i < 500; ++i) looped.data.push_back((uint8_t)((i * 5) % 32));
  looped.loop = 100;
  s.samples.push_back(big);
  s.samples.push_back(looped);

  Instrument ddaBig, ddaLoop, saw;
  ddaBig.sample = 0;
  ddaLoop.sample = 1;
  saw.wave = 1;
  saw.macros[MACRO_WAVE].values.push_back(1);
  saw.macros[MACRO_WAVE].values.push_back(0);
  saw.macros[MACRO_WAVE].loop = 0;          // wave upload every frame
  s.instruments.push_back(ddaBig);   // 01
  s.instruments.push_back(ddaLoop);  // 02
  s.instruments.push_back(saw);      // 03

  Pattern& p = s.pattern(0);
  p.resize(128);
  p.at(0, 2) = cellOf(36, 1);        // ch3: big sample at original speed (2.9 s, crosses 2 banks)
  p.at(8, 5) = cellOf(43, 1);        // ch6: the same sample, a fifth up, at the same time
  p.at(40, 2) = cellOf(48, 1);       // ch3: retrigger an octave up
  p.at(0, 4) = cellOf(45, 2);        // ch5: looped sample ...
  p.at(30, 4).note = NOTE_OFF;       // ... until key off
  p.at(0, 0) = cellOf(45, 0, '4', 0x47);
  p.at(0, 1) = cellOf(33, 0, '4', 0x35);
  p.at(0, 3) = cellOf(40, 3, '4', 0x62);
  for (int r = 1; r < 128; ++r) {
    p.at(r, 0).fx[0] = Effect('4', 0);
    p.at(r, 1).fx[0] = Effect('4', 0);
    p.at(r, 3).fx[0] = Effect('4', 0);
  }
  ExportStats st;
  CHECK(exportAndVerify(s, "stress", &st));
  CHECK(st.sampleBytes > 2u * kBankSize);   // samples span several banks
  CHECK(st.streamBytes > 2u * kBankSize);   // so does the stream
}

static void testOrderJumpLoop() {
  // Bxx back into the song: the intro plays once, the rest loops.
  Song s;
  s.speed = 3;
  s.pattern(1);
  s.orders.push_back(1);
  s.pattern(0).resize(8);
  s.pattern(1).resize(8);
  s.pattern(0).at(0, 0) = cellOf(40, 0);
  s.pattern(1).at(0, 0) = cellOf(47, 0, '1', 4);
  s.pattern(1).at(7, 1).fx[0] = Effect('B', 1);
  ExportStats st;
  CHECK(exportAndVerify(s, "order jump", &st));
  CHECK(st.framesIntro == 16 * 3 && st.framesLoop == 8 * 3);
}

int main(int argc, char** argv) {
  const char* demo = argc > 1 ? argv[1] : "songs/demo.pcet";
  printf("driver builds\n");
  testDriverBuilds();
  printf("default song\n");
  testDefaultSong();
  printf("demo song\n");
  testDemo(demo);
  printf("stress song\n");
  testStress();
  printf("order jump loop\n");
  testOrderJumpLoop();
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
