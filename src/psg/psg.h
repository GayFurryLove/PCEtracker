// PCEtracker - HuC6280 PSG module public interface.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// This is the only header other modules include to make PC Engine sound.
// It knows nothing about songs, instruments or UI: it takes register writes
// at points in time and produces 16-bit stereo PCM.

#ifndef PCETRACKER_PSG_H
#define PCETRACKER_PSG_H

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace pce {

namespace detail { class PCE_PSG; }

// HuC6280 PSG register addresses ($0800-$0809 on the console, low 4 bits).
enum PsgReg {
  REG_SELECT      = 0x0, // channel select 0-5
  REG_MAIN_VOLUME = 0x1, // global balance: high nibble L, low nibble R
  REG_FREQ_LO     = 0x2, // period bits 0-7
  REG_FREQ_HI     = 0x3, // period bits 8-11
  REG_CONTROL     = 0x4, // bit7 enable, bit6 DDA, bits0-4 volume
  REG_BALANCE     = 0x5, // channel balance: high nibble L, low nibble R
  REG_WAVE_DATA   = 0x6, // 5-bit sample (wave RAM write or DDA value)
  REG_NOISE       = 0x7, // ch 4-5 only: bit7 enable, bits0-4 noise frequency
  REG_LFO_FREQ    = 0x8, // LFO frequency divider
  REG_LFO_CONTROL = 0x9  // bit7 LFO reset/halt, bits0-1 LFO depth shift
};

enum PsgRevision {
  PSG_REV_HUC6280  = 0, // original chip (PC Engine, early units)
  PSG_REV_HUC6280A = 1  // revised chip (later PC Engine, Duo, TurboGrafx-16)
};

class Psg {
public:
  // Clock of the PSG timestamps: NTSC master clock / 3 = 21.47727 MHz / 3.
  static const double kClockHz;
  static const int kChannels = 6;

  Psg(PsgRevision revision, int sampleRate);
  ~Psg();

  // Power-on reset of all registers; output buffers are cleared.
  void reset();

  // Write a register at the current time position.
  void write(uint8_t reg, uint8_t value);

  // Convenience: select channel ch, then write reg (2 writes, like the CPU does).
  void writeChannel(int ch, uint8_t reg, uint8_t value);

  // Emulate the chip for the given number of PSG clocks.
  void advance(uint32_t clocks);

  // Emulate exactly as long as needed to produce `frames` more output frames,
  // then read them (interleaved L,R). Returns frames written.
  size_t render(int16_t* stereoOut, size_t frames);

  // Read already produced frames (interleaved L,R). Returns frames written.
  size_t read(int16_t* stereoOut, size_t maxFrames);
  size_t available() const;

  // Output gain only (not a chip feature). 1.0 = Mednafen reference level.
  void setGain(double gain);

  PsgRevision revision() const { return revision_; }
  int sampleRate() const { return sampleRate_; }

  // Read-only view of channel state, for UI meters and tests.
  uint16_t channelPeriod(int ch) const;
  uint8_t channelControl(int ch) const;
  void peekWave(int ch, uint8_t out[32]) const;

private:
  Psg(const Psg&);
  Psg& operator=(const Psg&);
  void flush();

  detail::PCE_PSG* core_;
  PsgRevision revision_;
  int sampleRate_;
  int32_t time_; // PSG clocks since the last flush
  // Produced frames not yet read (interleaved L,R). Pre-reserved for 1 s, so it
  // only grows if the caller emulates more than a second without reading.
  std::vector<int16_t> pending_;
  size_t pendingHead_; // index of the first unread sample in pending_
};

// Period register value for a frequency in Hz (wave mode, 32-step wavetable).
// Clamped to the valid 1..4095 range.
uint16_t psgPeriodForHz(double hz);
double psgHzForPeriod(uint16_t period);

} // namespace pce

#endif
