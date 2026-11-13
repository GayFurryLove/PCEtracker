// PCEtracker - song player (module 2: tracker engine).
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// The player is a model of the PC Engine sound driver: once per 60 Hz frame
// (tick) it computes what every channel should sound like and emits ONLY
// PSG register writes, in a fixed order, skipping registers that did not
// change. It never touches audio samples. Whatever it does, a 6280 driver
// can do with the same integer arithmetic; the HES/ROM exporters will be
// verified against this player's write stream.

#ifndef PCETRACKER_PLAYER_H
#define PCETRACKER_PLAYER_H

#include "tracker/song.h"

#include <stdint.h>

namespace pce {

// Receives register writes ($0800 + reg). Implemented by the PSG renderer,
// by test loggers and later by the export verifier.
class RegisterSink {
public:
  virtual ~RegisterSink() {}
  virtual void write(uint8_t reg, uint8_t value) = 0;
};

class Player {
public:
  explicit Player(const Song& song);

  // Start playing the song from an order position and row.
  void playSong(int order = 0, int row = 0);
  // Loop one pattern forever (pattern editor preview). Bxx/Dxx are ignored.
  void playPattern(int pattern, int row = 0);
  // Silence all channels on the next tick and stop advancing.
  void stop();

  // Run one 60 Hz tick. Returns false once the player is stopped and silent.
  bool tick(RegisterSink& out);

  void setMute(int ch, bool muted);
  bool muted(int ch) const { return ch_[ch].muted; }
  void solo(int ch); // mute every other channel; solo(-1) unmutes all

  bool playing() const { return playing_; }
  // True once the song wrapped (end of order list or a backward Bxx jump).
  bool songLooped() const { return looped_; }
  void clearLooped() { looped_ = false; }
  int order() const { return order_; }
  int row() const { return row_; }
  int speed() const { return speed_; }
  uint64_t ticksPlayed() const { return ticks_; }

  // Current output state per channel, for UI meters and tests.
  struct ChannelView { bool active; uint8_t note; uint8_t volume; uint16_t period; };
  ChannelView view(int ch) const;

private:
  struct MacroPos { int pos; bool done; };

  struct Channel {
    bool active;         // a note is sounding
    bool used;           // has played since start (channel stays enabled)
    bool muted;
    uint8_t note, inst;
    int volume;          // channel volume 0..31 (volume column / instrument / Axy)
    int period;          // current period without arp/vibrato/macros/detune
    MacroPos macro[MACRO_COUNT];
    bool released;
    int pitchAccum;      // MACRO_PITCH accumulator
    // effects of the current row
    uint8_t arp, volSlide, portaUp, portaDown;
    bool tonePorta, vibratoOn;
    uint8_t cutAt, retrigEvery;
    // effect memory
    uint8_t tonePortaSpeed, vibSpeed, vibDepth;
    int tonePortaTarget;
    int vibPos;
    int detune;
    uint8_t pan;
    int wave;            // wave bank index wanted
    int noise;           // 0 off, 1..32
    bool phaseReset;     // restart wave RAM pointer before next enable
    // computed output
    uint8_t outNote, outVol;
    int outPeriod;
  };

  struct Shadow {        // last values written to the chip (-1 = unknown)
    int control, period, balance, noise, wave;
  };

  void resetState();
  void processRow(int ch, const Cell& cell);
  void trigger(int ch, uint8_t note);
  void tickEffects(int ch);
  void advanceMacros(int ch);
  int macroValue(int ch, MacroKind k, int fallback) const;
  void compute(int ch);
  void emit(RegisterSink& out);
  void selectChannel(RegisterSink& out, int ch);
  void advanceRow();

  const Song& song_;
  Channel ch_[kChannels];
  Shadow hw_[kChannels];
  int hwSelect_, hwMainVol_, hwLfoFreq_, hwLfoCtrl_;
  int mainVol_, lfoFreq_, lfoCtrl_;

  bool playing_, stopping_, patternMode_, looped_;
  int order_, row_, tick_, speed_;
  int loopPattern_;
  int breakRow_, jumpOrder_; // -1 = none
  uint64_t ticks_;
};

} // namespace pce

#endif
