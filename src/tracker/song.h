// PCEtracker - song data model (module 2/3: tracker + instruments data).
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// Plain data only: no playback, no file I/O, no UI. Every field maps to
// something a PC Engine sound driver can do at 60 Hz.

#ifndef PCETRACKER_SONG_H
#define PCETRACKER_SONG_H

#include <stdint.h>
#include <string>
#include <vector>

namespace pce {

const int kChannels = 6;
const int kMaxRows = 256;
const int kMaxPatterns = 256;
const int kMaxOrders = 256;
const int kMaxInstruments = 255;
const int kMaxWaves = 64;
const int kWaveLength = 32;     // hardware wave RAM size per channel
const int kWaveMax = 31;        // 5-bit samples
const int kVolumeMax = 31;      // 5-bit channel volume
const int kMaxMacroLength = 64;
const int kEffectColumns = 2;
const int kMaxSamples = 64;
const int kMaxSampleLength = 65535;   // 5-bit samples; ROM space is the real limit
// DDA samples are played by the driver from the HuC6280 timer: 7.16 MHz / 1024.
const double kDdaRateHz = 315000000.0 / 88.0 * 2.0 / 1024.0; // ~6991 Hz
const uint32_t kDdaTimerClocks = 1024;  // PSG clocks per timer interrupt

// Notes: 0 = C-1 ... 95 = B-8 (A-4 = 45 = 440 Hz).
// C-1 (32.7 Hz) is close to the lowest pitch the 12-bit period can reach (27.3 Hz).
const uint8_t NOTE_NONE = 0xFF;
const uint8_t NOTE_OFF = 0xFE;  // key off: release macros, or silence if none
const int kNoteCount = 96;
const uint8_t INST_NONE = 0xFF;
const uint8_t VOL_NONE = 0xFF;

struct Effect {
  uint8_t cmd;   // ASCII command letter/digit, 0 = empty
  uint8_t param;
  Effect() : cmd(0), param(0) {}
  Effect(uint8_t c, uint8_t p) : cmd(c), param(p) {}
  bool operator==(const Effect& o) const { return cmd == o.cmd && param == o.param; }
};

struct Cell {
  uint8_t note;
  uint8_t instrument;
  uint8_t volume;
  Effect fx[kEffectColumns];
  Cell() : note(NOTE_NONE), instrument(INST_NONE), volume(VOL_NONE) {}
  bool empty() const;
  bool operator==(const Cell& o) const;
};

struct Pattern {
  int rows;
  std::vector<Cell> cells; // rows * kChannels, row-major
  Pattern() : rows(64), cells(64 * kChannels) {}
  explicit Pattern(int r) : rows(r), cells((size_t)r * kChannels) {}
  Cell& at(int row, int ch) { return cells[(size_t)row * kChannels + ch]; }
  const Cell& at(int row, int ch) const { return cells[(size_t)row * kChannels + ch]; }
  void resize(int r);
};

// Software envelope ("macro"): one value per 60 Hz tick.
// loop < 0: hold the last value. release >= 0: on NOTE_OFF jump past it.
struct Macro {
  std::vector<int8_t> values;
  int loop;
  int release;
  Macro() : loop(-1), release(-1) {}
  bool empty() const { return values.empty(); }
  bool operator==(const Macro& o) const {
    return values == o.values && loop == o.loop && release == o.release;
  }
};

enum MacroKind {
  MACRO_VOLUME = 0, // 0..31, absolute; combined with channel volume as attenuation
  MACRO_ARP,        // semitones relative to the note, -96..96
  MACRO_PITCH,      // period offset added per tick (cumulative), -128..127
  MACRO_WAVE,       // wave bank index; changing wave re-uploads wave RAM
  MACRO_NOISE,      // ch5/ch6 only: 0 = off, 1..32 = noise on, frequency value-1
  MACRO_PAN,        // high nibble L, low nibble R (as signed byte bits)
  MACRO_COUNT
};

struct Instrument {
  std::string name;
  int sample;       // DDA sample index, -1 = wavetable instrument
  uint8_t wave;     // initial wave bank index
  uint8_t volume;   // 0..31
  bool resetPhase;  // restart wave from sample 0 on every note (channel off/on)
  Macro macros[MACRO_COUNT];
  Instrument() : sample(-1), wave(0), volume(kVolumeMax), resetPhase(false) {}
};

struct Wave {
  uint8_t data[kWaveLength];
  Wave();
  bool operator==(const Wave& o) const;
};

// 5-bit PCM played through a channel's DDA mode at the timer rate.
// Pitch follows the note (C-4 = original speed) by stepping through the
// data in 8.8 fixed point - the same integer math a 6280 driver uses.
struct Sample {
  std::string name;
  std::vector<uint8_t> data; // 0..31
  int loop;                  // loop start, -1 = one shot
  Sample() : loop(-1) {}
};

struct Song {
  std::string title;
  std::string author;
  int speed;                         // ticks per row (1..31), one tick = one 60 Hz frame
  std::vector<uint8_t> orders;       // pattern index per order position
  std::vector<Pattern> patterns;
  std::vector<Instrument> instruments;
  std::vector<Wave> waves;
  std::vector<Sample> samples;
  Song();
  Pattern& pattern(int i);           // creates missing patterns
};

// Note helpers
std::string noteName(uint8_t note); // "C-4", "C#4", "OFF", "---"
bool parseNoteName(const std::string& s, uint8_t* note);
uint16_t notePeriod(int note);      // PSG period for a note (clamped to 1..4095)
uint16_t ddaStep(int note);         // DDA sample step, 8.8 fixed point (C-4 = 256)

} // namespace pce

#endif
