// PCEtracker - headless tests for the tracker engine, song format and renderer.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "audio/song_render.h"
#include "audio/wav_reader.h"
#include "project/text_format.h"
#include "tracker/player.h"

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

struct Log : RegisterSink {
  std::vector<std::pair<int, int> > w;
  virtual void write(uint8_t r, uint8_t v) { w.push_back(std::make_pair((int)r, (int)v)); }
  void clear() { w.clear(); }
  // Value of the last write to `reg` while channel `ch` was selected (-1 = none).
  int last(int ch, int reg) const {
    int sel = -1, v = -1;
    for (size_t i = 0; i < w.size(); ++i) {
      if (w[i].first == REG_SELECT) sel = w[i].second;
      else if (w[i].first == reg && (sel == ch || reg == REG_MAIN_VOLUME || reg >= REG_LFO_FREQ)) v = w[i].second;
    }
    return v;
  }
  int count(int ch, int reg) const {
    int sel = -1, n = 0;
    for (size_t i = 0; i < w.size(); ++i) {
      if (w[i].first == REG_SELECT) sel = w[i].second;
      else if (w[i].first == reg && sel == ch) ++n;
    }
    return n;
  }
  int period(int ch) const { return last(ch, REG_FREQ_LO) | (last(ch, REG_FREQ_HI) << 8); }
};

static Cell note(uint8_t n, uint8_t inst = 0, uint8_t vol = VOL_NONE, char fx = 0, uint8_t p = 0) {
  Cell c;
  c.note = n; c.instrument = inst; c.volume = vol;
  if (fx) c.fx[0] = Effect((uint8_t)fx, p);
  return c;
}

static Cell fxOnly(char fx, uint8_t p) { Cell c; c.fx[0] = Effect((uint8_t)fx, p); return c; }

static const uint8_t C4 = 36, A4 = 45;

static void testNoteTable() {
  CHECK(notePeriod(A4) == 254);
  bool mono = true;
  for (int n = 1; n < kNoteCount; ++n) if (notePeriod(n) > notePeriod(n - 1)) mono = false;
  CHECK(mono);
  CHECK(notePeriod(0) <= 4095 && notePeriod(kNoteCount - 1) >= 1);
  uint8_t n;
  CHECK(parseNoteName("C#4", &n) && n == C4 + 1);
  CHECK(noteName(A4) == "A-4");
  CHECK(parseNoteName("OFF", &n) && n == NOTE_OFF);
  CHECK(!parseNoteName("H-4", &n));
}

static void testFirstNoteWrites() {
  Song s;
  s.pattern(0).at(0, 0) = note(C4, 0, 0x1F);
  Player p(s);
  Log log;
  p.playSong();
  p.tick(log);
  // Wave upload sequence on ch1: DDA on/off rewinds the pointer, then 32 samples.
  bool found = false;
  for (size_t i = 0; i + 34 <= log.w.size(); ++i) {
    if (log.w[i] == std::make_pair((int)REG_SELECT, 0) && log.w[i + 1] == std::make_pair((int)REG_CONTROL, 0x40) &&
        log.w[i + 2] == std::make_pair((int)REG_CONTROL, 0x00)) {
      found = true;
      for (int k = 0; k < 32; ++k) found = found && log.w[i + 3 + k].first == REG_WAVE_DATA &&
                                         log.w[i + 3 + k].second == s.waves[0].data[k];
    }
  }
  CHECK(found);
  CHECK(log.period(0) == notePeriod(C4));
  CHECK(log.last(0, REG_CONTROL) == 0x9F);
  CHECK(log.last(0, REG_MAIN_VOLUME) == 0xFF);
  CHECK(log.last(1, REG_CONTROL) == 0x00); // unused channels stay off

  // Nothing changes on the next tick -> the driver writes nothing.
  log.clear();
  p.tick(log);
  CHECK(log.w.empty());
}

static void testSpeedAndRows() {
  Song s;
  s.speed = 3;
  Player p(s);
  Log log;
  p.playSong();
  for (int i = 0; i < 3; ++i) p.tick(log);
  CHECK(p.row() == 1);
  s.pattern(0).at(1, 0) = fxOnly('F', 2);
  for (int i = 0; i < 3; ++i) p.tick(log); // row 1 sets speed 2
  CHECK(p.speed() == 2 && p.row() == 2);
}

static void testArpeggio() {
  Song s;
  s.pattern(0).at(0, 0) = note(A4, 0, VOL_NONE, '0', 0x47);
  Player p(s);
  Log log;
  p.playSong();
  int got[3];
  for (int t = 0; t < 3; ++t) { p.tick(log); got[t] = p.view(0).period; }
  CHECK(got[0] == notePeriod(A4));
  CHECK(got[1] == notePeriod(A4 + 4));
  CHECK(got[2] == notePeriod(A4 + 7));
}

static void testPortamento() {
  Song s;
  s.pattern(0).at(0, 0) = note(A4, 0, VOL_NONE, '1', 5);
  s.pattern(0).at(1, 0) = fxOnly('2', 3);
  Player p(s);
  Log log;
  p.playSong();
  p.tick(log);
  CHECK(p.view(0).period == notePeriod(A4)); // tick 0: no slide
  for (int t = 1; t < 6; ++t) p.tick(log);
  CHECK(p.view(0).period == notePeriod(A4) - 25); // 5 ticks * 5
  for (int t = 0; t < 6; ++t) p.tick(log);
  CHECK(p.view(0).period == notePeriod(A4) - 25 + 15);

  // Tone portamento reaches the target exactly and stops there.
  Song s2;
  s2.pattern(0).at(0, 0) = note(A4);
  Cell tp = note(A4 + 2, INST_NONE, VOL_NONE, '3', 0x40);
  s2.pattern(0).at(1, 0) = tp;
  Player p2(s2);
  p2.playSong();
  for (int t = 0; t < 12; ++t) p2.tick(log);
  CHECK(p2.view(0).period == notePeriod(A4 + 2));
}

static void testVolumeAndMacros() {
  Song s;
  Instrument& in = s.instruments[0];
  in.macros[MACRO_VOLUME].values.push_back(31);
  in.macros[MACRO_VOLUME].values.push_back(29);
  in.macros[MACRO_VOLUME].values.push_back(20);
  s.pattern(0).at(0, 0) = note(A4, 0, 0x1F);
  Player p(s);
  Log log;
  p.playSong();
  int v[4];
  for (int t = 0; t < 4; ++t) { p.tick(log); v[t] = p.view(0).volume; }
  CHECK(v[0] == 31 && v[1] == 29 && v[2] == 20 && v[3] == 20); // holds last value

  // Volume slide down 4 per tick, from 16.
  Song s2;
  s2.pattern(0).at(0, 0) = note(A4, 0, 0x10, 'A', 0x04);
  Player p2(s2);
  p2.playSong();
  for (int t = 0; t < 4; ++t) p2.tick(log);
  CHECK(p2.view(0).volume == 4);
}

static void testNoteOffAndRelease() {
  Song s;
  s.pattern(0).at(0, 0) = note(A4, 0, 0x1F);
  s.pattern(0).at(1, 0).note = NOTE_OFF;
  Player p(s);
  Log log;
  p.playSong();
  for (int t = 0; t < 7; ++t) p.tick(log);
  CHECK(p.view(0).volume == 0);
  CHECK(log.last(0, REG_CONTROL) == 0x80); // stays enabled at volume 0 (no click)

  // With a release point the note keeps sounding through its release part.
  Song s2;
  Macro& m = s2.instruments[0].macros[MACRO_VOLUME];
  const int8_t vals[] = { 31, 31, 25, 20, 15 };
  m.values.assign(vals, vals + 5);
  m.release = 1;
  s2.pattern(0).at(0, 0) = note(A4, 0, 0x1F);
  s2.pattern(0).at(2, 0).note = NOTE_OFF;
  Player p2(s2);
  p2.playSong();
  for (int t = 0; t < 12; ++t) p2.tick(log);
  CHECK(p2.view(0).volume == 31); // sustained at release point while held
  p2.tick(log);
  CHECK(p2.view(0).volume == 25); // key off: continue after release point
}

static void testNoiseOnlyOnCh5Ch6() {
  Song s;
  s.pattern(0).at(0, 0) = note(A4, 0, VOL_NONE, 'N', 0x10);
  s.pattern(0).at(0, 4) = note(A4, 0, VOL_NONE, 'N', 0x10);
  Player p(s);
  Log log;
  p.playSong();
  p.tick(log);
  CHECK(log.count(0, REG_NOISE) == 0);
  CHECK(log.last(4, REG_NOISE) == (0x80 | 0x0F));
}

static void testMuteSolo() {
  Song s;
  s.pattern(0).at(0, 0) = note(A4, 0, 0x1F);
  s.pattern(0).at(0, 1) = note(A4, 0, 0x1F);
  Player p(s);
  Log log;
  p.playSong();
  p.solo(1);
  p.tick(log);
  CHECK(p.view(0).volume == 0 && p.view(1).volume == 31);
  CHECK(log.last(0, REG_CONTROL) == 0x80);
  p.solo(-1);
  p.tick(log);
  CHECK(p.view(0).volume == 31);
}

static void testOrderFlow() {
  Song s;
  s.speed = 1;
  s.pattern(1);
  s.orders.clear();
  s.orders.push_back(0);
  s.orders.push_back(1);
  s.pattern(0).at(0, 0) = fxOnly('D', 0x10); // break to row 0x10 of order 1
  s.pattern(1).at(0x10, 0) = fxOnly('B', 0x00); // jump back -> song loop
  Player p(s);
  Log log;
  p.playSong();
  p.tick(log);
  CHECK(p.order() == 1 && p.row() == 0x10);
  CHECK(!p.songLooped());
  p.tick(log);
  CHECK(p.order() == 0 && p.row() == 0);
  CHECK(p.songLooped());
}

static void testWaveEffectReuploads() {
  Song s;
  s.waves.push_back(Wave());
  for (int i = 0; i < kWaveLength; ++i) s.waves[1].data[i] = (uint8_t)i;
  s.pattern(0).at(0, 0) = note(A4);
  s.pattern(0).at(1, 0) = fxOnly('W', 1);
  Player p(s);
  Log log;
  p.playSong();
  for (int t = 0; t < 6; ++t) p.tick(log);
  log.clear();
  p.tick(log);
  CHECK(log.count(0, REG_WAVE_DATA) == 32);
  CHECK(log.last(0, REG_CONTROL) == 0x9F); // re-enabled after the upload
}

static void testFormatRoundTrip(const char* demoPath) {
  Song s;
  std::string err;
  bool ok = loadSongFile(demoPath, &s, &err);
  if (!ok) printf("  load error: %s\n", err.c_str());
  CHECK(ok);
  std::string a = saveSongText(s);
  Song s2;
  CHECK(loadSongText(a, &s2, &err));
  CHECK(saveSongText(s2) == a);
  CHECK(s2.patterns.size() == s.patterns.size() && s2.instruments.size() == s.instruments.size());

  // Errors point at the line.
  Song bad;
  ok = loadSongText("PCETRACKER 1\npattern 00 rows 4\n00 C-4 00 20 ... ...\nend\n", &bad, &err);
  CHECK(!ok && err.find("line 3") == 0);
  ok = loadSongText("PCETRACKER 9\n", &bad, &err);
  CHECK(!ok);
}

static void testRender(const char* demoPath) {
  Song s;
  std::string err;
  if (!loadSongFile(demoPath, &s, &err)) { CHECK(false); return; }

  // Demo: 4 orders * 32 rows * 6 ticks = 768 ticks = 12.8 s at 60 Hz.
  SongRenderer r(s, PSG_REV_HUC6280A, 44100);
  r.player().playSong();
  std::vector<int16_t> pcm;
  int ticks = 0;
  while (!r.player().songLooped() && ticks < 5000) { r.renderTick(pcm); ++ticks; }
  CHECK(ticks == 768);
  double sec = pcm.size() / 2 / 44100.0;
  CHECK(sec > 768 * (double)kFrameClocks / Psg::kClockHz - 0.01 && sec < 768 * (double)kFrameClocks / Psg::kClockHz + 0.01);
  double e = 0;
  for (size_t i = 0; i < pcm.size(); ++i) e += (double)pcm[i] * pcm[i];
  CHECK(e / pcm.size() > 10000.0);

  // Same song, same output.
  SongRenderer r2(s, PSG_REV_HUC6280A, 44100);
  r2.player().playSong();
  std::vector<int16_t> pcm2;
  for (int t = 0; t < ticks; ++t) r2.renderTick(pcm2);
  CHECK(pcm == pcm2);
}

static void testDdaPlayback() {
  Song s;
  Sample smp;
  for (int i = 0; i < 300; ++i) smp.data.push_back((uint8_t)(i & 31));
  s.samples.push_back(smp);
  s.instruments[0].sample = 0;
  s.pattern(0).at(0, 2) = note(36, 0, 0x1F); // C-4 = original speed
  SongRenderer r(s, PSG_REV_HUC6280A, 44100);
  Player& p = r.player();
  p.playSong();
  struct Count : RegisterSink {
    int data, ctrl, sel;
    Count() : data(0), ctrl(-1), sel(-1) {}
    void write(uint8_t reg, uint8_t v) {
      if (reg == REG_SELECT) sel = v;
      if (reg == REG_WAVE_DATA) ++data;
      if (reg == REG_CONTROL && sel == 2) ctrl = v;
    }
  } cnt;
  p.tick(cnt);
  CHECK(cnt.ctrl == (0xC0 | 0x1F)); // DDA bit + volume
  CHECK(cnt.data == 0);             // no wave upload for DDA instruments
  // ~116.4 timer interrupts per frame at step 1.0 -> 300 samples last ~2.6 frames.
  int writes = 0;
  for (int f = 0; f < 4; ++f) {
    for (int t = 0; t < 116; ++t) { Count c2; p.ddaTick(c2); writes += c2.data; }
  }
  CHECK(writes == 300);
  CHECK(!p.ddaActive());
  Count c3;
  p.tick(c3);
  CHECK(p.view(2).volume == 0); // sample ended -> silent

  // An octave up plays twice as fast.
  Song s2 = s;
  s2.pattern(0).at(0, 2) = note(48, 0, 0x1F);
  Player p2(s2);
  p2.playSong();
  Count c4;
  p2.tick(c4);
  int w2 = 0;
  for (int t = 0; t < 400; ++t) { Count c5; p2.ddaTick(c5); w2 += c5.data; }
  CHECK(w2 == 150);

  // Rendering with DDA produces sound.
  SongRenderer r2(s, PSG_REV_HUC6280A, 44100);
  r2.player().playSong();
  std::vector<int16_t> pcm;
  for (int t = 0; t < 4; ++t) r2.renderTick(pcm);
  double e = 0;
  for (size_t i = 0; i < pcm.size(); ++i) e += (double)pcm[i] * pcm[i];
  CHECK(e > 0);
}

static void testSampleFormatAndImport() {
  Song s;
  Sample smp;
  smp.name = "kick";
  smp.loop = 2;
  const uint8_t vals[] = { 0, 9, 10, 31, 16 };
  smp.data.assign(vals, vals + 5);
  s.samples.push_back(smp);
  s.instruments[0].sample = 0;
  std::string text = saveSongText(s), err;
  CHECK(text.find("sample 00 \"kick\" loop 2 : 09AVG") != std::string::npos);
  Song s2;
  CHECK(loadSongText(text, &s2, &err));
  CHECK(s2.samples.size() == 1 && s2.samples[0].data == smp.data && s2.samples[0].loop == 2);
  CHECK(s2.instruments[0].sample == 0);

  // 1 s of 440 Hz at 44.1 kHz -> ~6991 values covering the full 5-bit range.
  std::vector<float> sine(44100);
  for (size_t i = 0; i < sine.size(); ++i) sine[i] = (float)sin(2 * 3.14159265 * 440 * i / 44100.0) * 0.3f;
  Sample conv;
  convertToDda(&sine[0], sine.size(), 44100, &conv);
  CHECK(conv.data.size() >= 6985 && conv.data.size() <= 6995);
  int mn = 31, mx = 0;
  for (size_t i = 0; i < conv.data.size(); ++i) { if (conv.data[i] < mn) mn = conv.data[i]; if (conv.data[i] > mx) mx = conv.data[i]; }
  CHECK(mn <= 1 && mx >= 30); // normalized
}

int main(int argc, char** argv) {
  const char* demo = argc > 1 ? argv[1] : "songs/demo.pcet";
  struct { const char* name; void (*fn)(); } tests[] = {
    { "note table", testNoteTable },
    { "first note register writes", testFirstNoteWrites },
    { "speed and rows", testSpeedAndRows },
    { "arpeggio", testArpeggio },
    { "portamento", testPortamento },
    { "volume and macros", testVolumeAndMacros },
    { "note off and release", testNoteOffAndRelease },
    { "noise only on ch5/ch6", testNoiseOnlyOnCh5Ch6 },
    { "mute and solo", testMuteSolo },
    { "order flow", testOrderFlow },
    { "wave effect re-uploads", testWaveEffectReuploads },
    { "dda playback", testDdaPlayback },
    { "samples: format and wav conversion", testSampleFormatAndImport },
  };
  for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
    printf("%s\n", tests[i].name);
    tests[i].fn();
  }
  printf("format round trip\n");
  testFormatRoundTrip(demo);
  printf("render demo\n");
  testRender(demo);
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
