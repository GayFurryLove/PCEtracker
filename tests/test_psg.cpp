// PCEtracker - headless tests for the HuC6280 PSG module.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "psg/psg.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace pce;

static int g_failed = 0;
static int g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; \
  printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const int kRate = 44100;

// Square wave: 16 samples high, 16 low.
static void loadSquare(Psg& psg, int ch) {
  psg.writeChannel(ch, REG_CONTROL, 0x00);     // off, wave write mode, index reset
  for (int i = 0; i < 32; ++i) psg.write(REG_WAVE_DATA, i < 16 ? 0x1F : 0x00);
}

static void noteOn(Psg& psg, int ch, uint16_t period, uint8_t balance = 0xFF) {
  psg.write(REG_MAIN_VOLUME, 0xFF);
  psg.writeChannel(ch, REG_FREQ_LO, (uint8_t)(period & 0xFF));
  psg.write(REG_FREQ_HI, (uint8_t)(period >> 8));
  psg.write(REG_BALANCE, balance);
  psg.write(REG_CONTROL, 0x80 | 0x1F);
}

static std::vector<int16_t> renderFrames(Psg& psg, size_t frames) {
  std::vector<int16_t> out(frames * 2);
  size_t got = psg.render(&out[0], frames);
  out.resize(got * 2);
  return out;
}

static double energy(const std::vector<int16_t>& s, int side, size_t skip = 0) {
  double e = 0;
  for (size_t i = skip * 2 + side; i < s.size(); i += 2) e += (double)s[i] * s[i];
  return e / (double)(s.size() / 2 - skip + 1);
}

// Counts upward zero crossings of the left channel after `skip` frames.
static int crossings(const std::vector<int16_t>& s, size_t skip) {
  int n = 0;
  for (size_t i = (skip + 1) * 2; i < s.size(); i += 2)
    if (s[i - 2] < 0 && s[i] >= 0) ++n;
  return n;
}

static void testSilenceAfterReset(PsgRevision rev) {
  Psg psg(rev, kRate);
  std::vector<int16_t> s = renderFrames(psg, kRate / 10);
  CHECK(s.size() == (size_t)(kRate / 10) * 2);
  int peak = 0;
  for (size_t i = 0; i < s.size(); ++i) if (abs(s[i]) > peak) peak = abs(s[i]);
  CHECK(peak == 0);
}

static void testPitch(PsgRevision rev, double hz) {
  Psg psg(rev, kRate);
  loadSquare(psg, 0);
  uint16_t period = psgPeriodForHz(hz);
  noteOn(psg, 0, period);
  std::vector<int16_t> s = renderFrames(psg, kRate + kRate / 10);
  double expected = psgHzForPeriod(period);
  int got = crossings(s, kRate / 10); // exactly one second measured
  printf("  pitch %.1f Hz: period %u, expected %.2f, measured %d\n", hz, period, expected, got);
  CHECK(fabs(got - expected) <= expected * 0.01 + 1.0);
}

static void testBalance() {
  Psg psg(PSG_REV_HUC6280A, kRate);
  loadSquare(psg, 0);
  noteOn(psg, 0, psgPeriodForHz(440), 0xF0); // left only
  std::vector<int16_t> s = renderFrames(psg, kRate / 4);
  CHECK(energy(s, 0) > 1000.0);
  CHECK(energy(s, 1) < 1.0);
}

static void testNoiseOnlyOnChannels5and6() {
  // Noise register on ch 1 (index 0) must be ignored: output identical to no write.
  Psg a(PSG_REV_HUC6280A, kRate), b(PSG_REV_HUC6280A, kRate);
  loadSquare(a, 0); noteOn(a, 0, 500);
  loadSquare(b, 0); noteOn(b, 0, 500);
  b.writeChannel(0, REG_NOISE, 0x80 | 0x10);
  std::vector<int16_t> sa = renderFrames(a, 4000), sb = renderFrames(b, 4000);
  CHECK(sa == sb);

  // On ch 5 (index 4) noise plays even with an all-zero wavetable.
  Psg n(PSG_REV_HUC6280A, kRate);
  noteOn(n, 4, 500);
  n.writeChannel(4, REG_NOISE, 0x80 | 0x18);
  std::vector<int16_t> sn = renderFrames(n, kRate / 4);
  CHECK(energy(sn, 0, 100) > 1000.0);
}

static void testDda() {
  Psg psg(PSG_REV_HUC6280A, kRate);
  psg.write(REG_MAIN_VOLUME, 0xFF);
  psg.writeChannel(2, REG_BALANCE, 0xFF);
  psg.write(REG_CONTROL, 0xC0 | 0x1F); // enable + DDA
  // Write samples at ~7 kHz like a timer IRQ would: 1024 PSG clocks per sample.
  std::vector<int16_t> out;
  for (int i = 0; i < 7000 / 4; ++i) {
    psg.write(REG_WAVE_DATA, (i / 8) & 1 ? 0x1F : 0x00);
    psg.advance(1024);
  }
  std::vector<int16_t> buf(psg.available() * 2 + 2);
  size_t got = psg.read(&buf[0], psg.available());
  buf.resize(got * 2);
  CHECK(got > 0);
  CHECK(energy(buf, 0, 100) > 1000.0);
}

static void testDeterministic() {
  Psg a(PSG_REV_HUC6280, kRate), b(PSG_REV_HUC6280, kRate);
  loadSquare(a, 3); noteOn(a, 3, 321);
  loadSquare(b, 3); noteOn(b, 3, 321);
  CHECK(renderFrames(a, 10000) == renderFrames(b, 10000));
}

static void testRevisionsDiffer() {
  // The two revisions differ in DC behaviour at note start (the "click").
  Psg a(PSG_REV_HUC6280, kRate), b(PSG_REV_HUC6280A, kRate);
  loadSquare(a, 0); noteOn(a, 0, 400);
  loadSquare(b, 0); noteOn(b, 0, 400);
  CHECK(renderFrames(a, 2000) != renderFrames(b, 2000));
}

static void testLfoModulatesChannel1() {
  Psg a(PSG_REV_HUC6280A, kRate), b(PSG_REV_HUC6280A, kRate);
  Psg* both[2] = { &a, &b };
  for (int k = 0; k < 2; ++k) {
    Psg& p = *both[k];
    loadSquare(p, 0); noteOn(p, 0, 300);
    loadSquare(p, 1); noteOn(p, 1, 40, 0x00); // modulator, silent in the mix
    p.write(REG_LFO_FREQ, 0x10);
    p.write(REG_LFO_CONTROL, k == 0 ? 0x00 : 0x01);
  }
  CHECK(renderFrames(a, 8000) != renderFrames(b, 8000));
}

static void testPeriodConversion() {
  CHECK(psgPeriodForHz(1e9) == 1);
  CHECK(psgPeriodForHz(1.0) == 4095);
  uint16_t p = psgPeriodForHz(440.0);
  CHECK(fabs(psgHzForPeriod(p) - 440.0) < 440.0 * 0.005);
}

static void testHigherSampleRate() {
  Psg psg(PSG_REV_HUC6280A, 96000);
  loadSquare(psg, 0);
  uint16_t period = psgPeriodForHz(1000);
  noteOn(psg, 0, period);
  std::vector<int16_t> s = renderFrames(psg, 96000 + 9600);
  double expected = psgHzForPeriod(period);
  CHECK(fabs(crossings(s, 9600) - expected) <= expected * 0.01 + 1.0);
}

int main() {
  struct { const char* name; void (*fn)(); } simple[] = {
    { "balance", testBalance },
    { "noise only on ch5/ch6", testNoiseOnlyOnChannels5and6 },
    { "dda", testDda },
    { "deterministic", testDeterministic },
    { "revisions differ", testRevisionsDiffer },
    { "lfo modulates ch1", testLfoModulatesChannel1 },
    { "period conversion", testPeriodConversion },
    { "96 kHz output", testHigherSampleRate },
  };
  printf("silence after reset\n");
  testSilenceAfterReset(PSG_REV_HUC6280);
  testSilenceAfterReset(PSG_REV_HUC6280A);
  printf("pitch\n");
  testPitch(PSG_REV_HUC6280, 110.0);
  testPitch(PSG_REV_HUC6280A, 440.0);
  testPitch(PSG_REV_HUC6280A, 2000.0);
  for (size_t i = 0; i < sizeof simple / sizeof simple[0]; ++i) {
    printf("%s\n", simple[i].name);
    simple[i].fn();
  }
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
