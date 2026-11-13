// PCEtracker - song player.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "tracker/player.h"
#include "psg/psg.h"

namespace pce {

namespace {

// 32-step sine, -127..127 (one table lookup per tick, as a driver would do).
const int8_t kSine[32] = {
     0,   25,   49,   71,   90,  106,  117,  125,
   127,  125,  117,  106,   90,   71,   49,   25,
     0,  -25,  -49,  -71,  -90, -106, -117, -125,
  -127, -125, -117, -106,  -90,  -71,  -49,  -25,
};

const Pattern& emptyPattern() {
  static Pattern p(64);
  return p;
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

Player::Player(const Song& song) : song_(song) {
  for (int c = 0; c < kChannels; ++c) ch_[c].muted = false;
  playing_ = false;
  stopping_ = false;
  jam_ = false;
  resetState();
}

void Player::jamNote(int c, uint8_t note, int instrument) {
  if (c < 0 || c >= kChannels || note >= kNoteCount) return;
  if (!playing_ && !jam_) resetState(); // fresh driver state, like a new song start
  jam_ = true;
  Channel& ch = ch_[c];
  if (instrument >= 0 && instrument < (int)song_.instruments.size()) {
    ch.inst = (uint8_t)instrument;
    ch.volume = clampi(song_.instruments[instrument].volume, 0, kVolumeMax);
  }
  ch.arp = ch.volSlide = ch.portaUp = ch.portaDown = 0;
  ch.tonePorta = ch.vibratoOn = false;
  ch.cutAt = 0xFF;
  ch.retrigEvery = 0;
  trigger(c, note);
}

void Player::jamRelease(int c) {
  if (c < 0 || c >= kChannels || !jam_) return;
  Cell off;
  off.note = NOTE_OFF;
  processRow(c, off);
}

void Player::resetState() {
  for (int c = 0; c < kChannels; ++c) {
    Channel& ch = ch_[c];
    bool keepMute = ch.muted;
    ch = Channel();
    ch.muted = keepMute;
    ch.active = false;
    ch.note = 45;
    ch.inst = 0;
    ch.volume = kVolumeMax;
    ch.period = notePeriod(45);
    for (int k = 0; k < MACRO_COUNT; ++k) { ch.macro[k].pos = 0; ch.macro[k].done = false; }
    ch.released = false;
    ch.pitchAccum = 0;
    ch.arp = ch.volSlide = ch.portaUp = ch.portaDown = 0;
    ch.tonePorta = ch.vibratoOn = false;
    ch.cutAt = 0xFF;
    ch.retrigEvery = 0;
    ch.tonePortaSpeed = ch.vibSpeed = ch.vibDepth = 0;
    ch.tonePortaTarget = ch.period;
    ch.vibPos = 0;
    ch.detune = 0;
    ch.pan = 0xFF;
    ch.wave = -1;
    ch.noise = 0;
    ch.phaseReset = false;
    ch.outNote = 45;
    ch.outVol = 0;
    ch.outPeriod = ch.period;
    ch.used = false;

    Shadow& s = hw_[c];
    s.control = s.period = s.balance = s.noise = s.wave = -1;
  }
  hwSelect_ = hwMainVol_ = hwLfoFreq_ = hwLfoCtrl_ = -1;
  mainVol_ = 0xFF;
  lfoFreq_ = 0;
  lfoCtrl_ = 0;
  patternMode_ = false;
  looped_ = false;
  order_ = row_ = tick_ = 0;
  speed_ = clampi(song_.speed, 1, 31);
  loopPattern_ = 0;
  breakRow_ = jumpOrder_ = -1;
  ticks_ = 0;
}

void Player::playSong(int order, int row) {
  resetState();
  order_ = (order >= 0 && order < (int)song_.orders.size()) ? order : 0;
  row_ = row < 0 ? 0 : row;
  playing_ = true;
  stopping_ = false;
  jam_ = false;
}

void Player::playPattern(int pattern, int row) {
  resetState();
  patternMode_ = true;
  loopPattern_ = pattern;
  row_ = row < 0 ? 0 : row;
  playing_ = true;
  stopping_ = false;
  jam_ = false;
}

void Player::stop() {
  if (playing_ || jam_) stopping_ = true;
}

void Player::setMute(int ch, bool m) {
  if (ch >= 0 && ch < kChannels) ch_[ch].muted = m;
}

void Player::solo(int ch) {
  for (int c = 0; c < kChannels; ++c) ch_[c].muted = (ch >= 0 && c != ch);
}

Player::ChannelView Player::view(int c) const {
  ChannelView v;
  v.active = ch_[c].active && ch_[c].outVol > 0;
  v.note = ch_[c].outNote;
  v.volume = ch_[c].outVol;
  v.period = (uint16_t)ch_[c].outPeriod;
  return v;
}

// ---------------------------------------------------------------------------

void Player::trigger(int c, uint8_t note) {
  Channel& ch = ch_[c];
  ch.note = note;
  ch.active = true;
  ch.used = true;
  ch.released = false;
  for (int k = 0; k < MACRO_COUNT; ++k) { ch.macro[k].pos = 0; ch.macro[k].done = false; }
  ch.pitchAccum = 0;
  ch.vibPos = 0;
  ch.period = notePeriod(note);
  ch.tonePortaTarget = ch.period;
  if (ch.inst < song_.instruments.size()) {
    const Instrument& in = song_.instruments[ch.inst];
    if (in.wave < song_.waves.size()) ch.wave = in.wave;
    ch.phaseReset = in.resetPhase;
  }
  if (ch.wave < 0 && !song_.waves.empty()) ch.wave = 0;
}

void Player::processRow(int c, const Cell& cell) {
  Channel& ch = ch_[c];
  ch.arp = ch.volSlide = ch.portaUp = ch.portaDown = 0;
  ch.tonePorta = ch.vibratoOn = false;
  ch.cutAt = 0xFF;
  ch.retrigEvery = 0;

  if (cell.instrument != INST_NONE && cell.instrument < song_.instruments.size()) {
    ch.inst = cell.instrument;
    ch.volume = clampi(song_.instruments[ch.inst].volume, 0, kVolumeMax);
  }

  for (int i = 0; i < kEffectColumns; ++i) {
    const uint8_t p = cell.fx[i].param;
    switch (cell.fx[i].cmd) {
      case '0': ch.arp = p; break;
      case '1': ch.portaUp = p; break;
      case '2': ch.portaDown = p; break;
      case '3': ch.tonePorta = true; if (p) ch.tonePortaSpeed = p; break;
      case '4':
        ch.vibratoOn = true;
        if (p >> 4) ch.vibSpeed = p >> 4;
        if (p & 15) ch.vibDepth = p & 15;
        break;
      case '8': ch.pan = p; break;
      case 'A': ch.volSlide = p; break;
      case 'B': jumpOrder_ = p; break;
      case 'C': ch.cutAt = p; break;
      case 'D': breakRow_ = p; break;
      case 'F': if (p >= 1 && p <= 31) speed_ = p; break;
      case 'G': mainVol_ = p; break;
      case 'L': lfoCtrl_ = p & 0x83; break;
      case 'M': lfoFreq_ = p; break;
      case 'N': if (c >= 4) ch.noise = p > 32 ? 32 : p; break;
      case 'P': ch.detune = (int)p - 0x80; break;
      case 'R': ch.retrigEvery = p; break;
      case 'W': if (p < song_.waves.size()) ch.wave = p; break;
      default: break; // unknown commands are kept in the song but do nothing
    }
  }

  if (cell.note == NOTE_OFF) {
    ch.released = true;
    bool hasRelease = false;
    if (ch.inst < song_.instruments.size()) {
      const Instrument& in = song_.instruments[ch.inst];
      for (int k = 0; k < MACRO_COUNT; ++k) {
        const Macro& m = in.macros[k];
        if (m.release >= 0 && m.release + 1 < (int)m.values.size()) {
          if (ch.macro[k].pos <= m.release) { ch.macro[k].pos = m.release + 1; ch.macro[k].done = false; }
          if (k == MACRO_VOLUME) hasRelease = true;
        }
      }
    }
    if (!hasRelease) ch.active = false;
  } else if (cell.note < kNoteCount) {
    if (ch.tonePorta && ch.active) {
      ch.tonePortaTarget = notePeriod(cell.note);
      ch.note = cell.note;
    } else {
      trigger(c, cell.note);
    }
  }

  if (cell.volume != VOL_NONE) ch.volume = clampi(cell.volume, 0, kVolumeMax);
}

void Player::tickEffects(int c) {
  Channel& ch = ch_[c];
  const int t = tick_;
  if (ch.cutAt != 0xFF && t == ch.cutAt) ch.active = false;
  if (ch.retrigEvery && t > 0 && t % ch.retrigEvery == 0 && ch.used) trigger(c, ch.note);
  if (t == 0) return;

  if (ch.portaUp) ch.period = clampi(ch.period - ch.portaUp, 1, 4095);
  if (ch.portaDown) ch.period = clampi(ch.period + ch.portaDown, 1, 4095);
  if (ch.tonePorta && ch.tonePortaSpeed) {
    if (ch.period < ch.tonePortaTarget) {
      ch.period += ch.tonePortaSpeed;
      if (ch.period > ch.tonePortaTarget) ch.period = ch.tonePortaTarget;
    } else if (ch.period > ch.tonePortaTarget) {
      ch.period -= ch.tonePortaSpeed;
      if (ch.period < ch.tonePortaTarget) ch.period = ch.tonePortaTarget;
    }
  }
  if (ch.volSlide) {
    int up = ch.volSlide >> 4, down = ch.volSlide & 15;
    ch.volume = clampi(up ? ch.volume + up : ch.volume - down, 0, kVolumeMax);
  }
  if (ch.vibratoOn) ch.vibPos = (ch.vibPos + ch.vibSpeed) & 31;
}

int Player::macroValue(int c, MacroKind k, int fallback) const {
  const Channel& ch = ch_[c];
  if (ch.inst >= song_.instruments.size()) return fallback;
  const Macro& m = song_.instruments[ch.inst].macros[k];
  if (m.empty()) return fallback;
  int pos = clampi(ch.macro[k].pos, 0, (int)m.values.size() - 1);
  return m.values[pos];
}

void Player::advanceMacros(int c) {
  Channel& ch = ch_[c];
  if (ch.inst >= song_.instruments.size()) return;
  const Instrument& in = song_.instruments[ch.inst];
  for (int k = 0; k < MACRO_COUNT; ++k) {
    const Macro& m = in.macros[k];
    MacroPos& mp = ch.macro[k];
    if (m.empty() || mp.done) continue;
    int next = mp.pos + 1;
    if (!ch.released && m.release >= 0 && mp.pos >= m.release) {
      // Sustain until key-off: loop inside the sustain part, or hold at the release point.
      next = (m.loop >= 0 && m.loop <= m.release) ? m.loop : m.release;
    } else if (next >= (int)m.values.size()) {
      if (m.loop >= 0 && m.loop < (int)m.values.size() && (m.release < 0 || m.loop > m.release)) {
        next = m.loop;
      } else {
        next = (int)m.values.size() - 1;
        mp.done = true;
      }
    }
    mp.pos = next;
  }
}

void Player::compute(int c) {
  Channel& ch = ch_[c];

  // Pitch: note + arpeggio (effect and macro) expressed as a period offset,
  // then slides, pitch macro, vibrato and detune - all in period units.
  int arpOff = 0;
  if (ch.arp) {
    int phase = tick_ % 3;
    arpOff = phase == 1 ? (ch.arp >> 4) : phase == 2 ? (ch.arp & 15) : 0;
  }
  int n = clampi((int)ch.note + arpOff + macroValue(c, MACRO_ARP, 0), 0, kNoteCount - 1);
  ch.pitchAccum = clampi(ch.pitchAccum + macroValue(c, MACRO_PITCH, 0), -4095, 4095);
  int per = ch.period + ((int)notePeriod(n) - (int)notePeriod(ch.note)) + ch.pitchAccum + ch.detune;
  if (ch.vibratoOn && ch.vibDepth) per += (kSine[ch.vibPos & 31] * ch.vibDepth) >> 6;
  ch.outPeriod = clampi(per, 1, 4095);
  ch.outNote = (uint8_t)n;

  // Volume: the PSG volume is logarithmic (~1.5 dB/step), so combining
  // channel volume and the volume macro is an addition of attenuations.
  int mv = clampi(macroValue(c, MACRO_VOLUME, kVolumeMax), 0, kVolumeMax);
  int v = ch.volume - (kVolumeMax - mv);
  if (!ch.active || ch.muted || v < 0) v = 0;
  ch.outVol = (uint8_t)v;

  int w = macroValue(c, MACRO_WAVE, -1);
  if (w >= 0 && w < (int)song_.waves.size()) ch.wave = w;
  if (c >= 4) {
    int nz = macroValue(c, MACRO_NOISE, -1);
    if (nz >= 0) ch.noise = clampi(nz, 0, 32);
  }
  if (ch.inst < song_.instruments.size() && !song_.instruments[ch.inst].macros[MACRO_PAN].empty())
    ch.pan = (uint8_t)macroValue(c, MACRO_PAN, 0xFF);
}

// ---------------------------------------------------------------------------

void Player::selectChannel(RegisterSink& out, int c) {
  if (hwSelect_ != c) { out.write(REG_SELECT, (uint8_t)c); hwSelect_ = c; }
}

void Player::emit(RegisterSink& out) {
  if (mainVol_ != hwMainVol_) { out.write(REG_MAIN_VOLUME, (uint8_t)mainVol_); hwMainVol_ = mainVol_; }
  if (lfoFreq_ != hwLfoFreq_) { out.write(REG_LFO_FREQ, (uint8_t)lfoFreq_); hwLfoFreq_ = lfoFreq_; }
  if (lfoCtrl_ != hwLfoCtrl_) { out.write(REG_LFO_CONTROL, (uint8_t)lfoCtrl_); hwLfoCtrl_ = lfoCtrl_; }

  for (int c = 0; c < kChannels; ++c) {
    Channel& ch = ch_[c];
    Shadow& s = hw_[c];
    const bool upload = ch.used && ch.wave >= 0 && ch.wave != s.wave;
    if (upload || (ch.phaseReset && ch.used)) {
      // Wave RAM can only be written with the channel off. DDA on -> off
      // rewinds the wave pointer to 0, then 32 writes fill the table.
      selectChannel(out, c);
      out.write(REG_CONTROL, 0x40);
      out.write(REG_CONTROL, 0x00);
      if (upload) {
        const Wave& w = song_.waves[ch.wave];
        for (int i = 0; i < kWaveLength; ++i) out.write(REG_WAVE_DATA, (uint8_t)(w.data[i] & 0x1F));
        s.wave = ch.wave;
      }
      s.control = 0x00;
      ch.phaseReset = false;
    }
    if (ch.outPeriod != s.period) {
      selectChannel(out, c);
      out.write(REG_FREQ_LO, (uint8_t)(ch.outPeriod & 0xFF));
      out.write(REG_FREQ_HI, (uint8_t)(ch.outPeriod >> 8));
      s.period = ch.outPeriod;
    }
    if (ch.pan != s.balance) {
      selectChannel(out, c);
      out.write(REG_BALANCE, ch.pan);
      s.balance = ch.pan;
    }
    if (c >= 4) {
      int nz = ch.noise ? (0x80 | (ch.noise - 1)) : 0x00;
      if (nz != s.noise) {
        selectChannel(out, c);
        out.write(REG_NOISE, (uint8_t)nz);
        s.noise = nz;
      }
    }
    // A channel that has played stays enabled at volume 0 when silent:
    // switching it off would cause a DC click on the HuC6280.
    int ctrl = ch.used ? (0x80 | ch.outVol) : 0x00;
    if (ctrl != s.control) {
      selectChannel(out, c);
      out.write(REG_CONTROL, (uint8_t)ctrl);
      s.control = ctrl;
    }
  }
}

void Player::advanceRow() {
  if (patternMode_) {
    const Pattern& p = loopPattern_ < (int)song_.patterns.size() ? song_.patterns[loopPattern_] : emptyPattern();
    // A break or the last row ends this pass of the pattern (counts as a loop).
    if (breakRow_ >= 0) { row_ = breakRow_; looped_ = true; }
    else if (++row_ >= p.rows) { row_ = 0; looped_ = true; }
    if (row_ >= p.rows) row_ = 0;
    breakRow_ = jumpOrder_ = -1;
    return;
  }
  int newOrder = order_, newRow = row_ + 1;
  if (jumpOrder_ >= 0 || breakRow_ >= 0) {
    newOrder = jumpOrder_ >= 0 ? jumpOrder_ : order_ + 1;
    newRow = breakRow_ >= 0 ? breakRow_ : 0;
    if (jumpOrder_ >= 0 && jumpOrder_ <= order_) looped_ = true;
  } else {
    int pi = song_.orders.empty() ? 0 : song_.orders[order_];
    const Pattern& p = pi < (int)song_.patterns.size() ? song_.patterns[pi] : emptyPattern();
    if (newRow >= p.rows) { newOrder = order_ + 1; newRow = 0; }
  }
  if (newOrder >= (int)song_.orders.size()) { newOrder = 0; looped_ = true; }
  int pi = song_.orders.empty() ? 0 : song_.orders[newOrder];
  const Pattern& np = pi < (int)song_.patterns.size() ? song_.patterns[pi] : emptyPattern();
  if (newRow >= np.rows) newRow = 0;
  order_ = newOrder;
  row_ = newRow;
  breakRow_ = jumpOrder_ = -1;
}

bool Player::tick(RegisterSink& out) {
  if (!playing_) {
    if (!jam_) return false;
    if (stopping_) {
      for (int c = 0; c < kChannels; ++c) { ch_[c].active = false; compute(c); }
      emit(out);
      jam_ = stopping_ = false;
      return true;
    }
    // Preview only: no rows, but macros and the driver output keep running.
    for (int c = 0; c < kChannels; ++c) {
      compute(c);
      advanceMacros(c);
    }
    emit(out);
    return true;
  }
  if (stopping_) {
    for (int c = 0; c < kChannels; ++c) { ch_[c].active = false; compute(c); }
    emit(out);
    playing_ = stopping_ = false;
    return true;
  }

  if (tick_ == 0) {
    int pi = patternMode_ ? loopPattern_ : (song_.orders.empty() ? 0 : song_.orders[order_]);
    const Pattern& p = pi < (int)song_.patterns.size() ? song_.patterns[pi] : emptyPattern();
    int r = row_ < p.rows ? row_ : 0;
    for (int c = 0; c < kChannels; ++c) processRow(c, p.at(r, c));
  }
  for (int c = 0; c < kChannels; ++c) {
    tickEffects(c);
    compute(c);
    advanceMacros(c);
  }
  emit(out);

  ++ticks_;
  if (++tick_ >= speed_) {
    tick_ = 0;
    advanceRow();
  }
  return true;
}

} // namespace pce
