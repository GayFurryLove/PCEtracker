// PCEtracker - pattern/order editing logic.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "tracker/editor.h"

#include <math.h>
#include <stdio.h>

namespace pce {

namespace {
const size_t kUndoLimit = 200;
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int setNibble(int value, bool high, int digit) {
  if (value < 0) value = 0;
  return high ? ((digit << 4) | (value & 0x0F)) : ((value & 0xF0) | digit);
}
}

Editor::Editor(Song& song)
  : octave(3), instrument(0), editStep(1), editMode(false),
    song_(song), order_(0), row_(0), ch_(0), col_(COL_NOTE),
    selActive_(false), selRow_(0), selCh_(0), clipW_(0), clipH_(0), dirty_(false), gesture_(0) {
  if (song_.orders.empty()) song_.orders.push_back(0);
  song_.pattern(song_.orders[0]);
}

int Editor::patternIndex() const { return song_.orders[order_]; }
Pattern& Editor::pattern() { return song_.pattern(song_.orders[order_]); }
const Pattern& Editor::pattern() const { return song_.patterns[song_.orders[order_]]; }
Cell& Editor::cell() { return pattern().at(row_, ch_); }

void Editor::clampCursor() {
  order_ = clampi(order_, 0, (int)song_.orders.size() - 1);
  song_.pattern(song_.orders[order_]);
  row_ = clampi(row_, 0, pattern().rows - 1);
  ch_ = clampi(ch_, 0, kChannels - 1);
  col_ = clampi(col_, 0, COL_COUNT - 1);
}

void Editor::moveRow(int d) {
  int rows = pattern().rows;
  row_ = ((row_ + d) % rows + rows) % rows;
}

void Editor::setRow(int r) { row_ = clampi(r, 0, pattern().rows - 1); }

void Editor::moveColumn(int d) {
  int pos = ch_ * COL_COUNT + col_ + d;
  int total = kChannels * COL_COUNT;
  pos = (pos % total + total) % total;
  ch_ = pos / COL_COUNT;
  col_ = pos % COL_COUNT;
}

void Editor::moveChannel(int d) { ch_ = ((ch_ + d) % kChannels + kChannels) % kChannels; }

void Editor::setCursor(int row, int ch, int col) {
  row_ = row; ch_ = ch; col_ = col;
  clampCursor();
}

void Editor::setOrder(int o) {
  order_ = o;
  clampCursor();
}

// --- undo ---------------------------------------------------------------------

Editor::Snapshot Editor::capture(bool wholeSong) const {
  Snapshot s;
  s.wholeSong = wholeSong;
  s.patternIdx = song_.orders[order_];
  if (wholeSong) {
    s.orders = song_.orders;
    s.patterns = song_.patterns;
  } else {
    s.pattern = song_.patterns[s.patternIdx];
  }
  s.speed = song_.speed;
  s.order = order_; s.row = row_; s.ch = ch_; s.col = col_;
  s.instruments = false;
  s.instrument = instrument;
  s.title = song_.title;
  return s;
}

void Editor::restore(const Snapshot& s) {
  if (s.instruments) {
    song_.instruments = s.insts;
    song_.waves = s.waves;
    song_.title = s.title;
    instrument = s.instrument;
    if (instrument >= (int)song_.instruments.size()) instrument = (int)song_.instruments.size() - 1;
    dirty_ = true;
    return;
  }
  if (s.wholeSong) {
    song_.orders = s.orders;
    song_.patterns = s.patterns;
  } else {
    song_.pattern(s.patternIdx) = s.pattern;
  }
  song_.speed = s.speed;
  order_ = s.order; row_ = s.row; ch_ = s.ch; col_ = s.col;
  selActive_ = false;
  clampCursor();
  dirty_ = true;
}

void Editor::pushUndo(bool wholeSong) {
  gesture_ = 0;
  undo_.push_back(capture(wholeSong));
  if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
  redo_.clear();
  dirty_ = true;
}

void Editor::pushInstrumentUndo(int gesture) {
  dirty_ = true;
  if (gesture != 0 && gesture == gesture_) return; // same mouse stroke: one undo step
  gesture_ = gesture;
  Snapshot s = capture(false);
  s.instruments = true;
  s.insts = song_.instruments;
  s.waves = song_.waves;
  undo_.push_back(s);
  if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
  redo_.clear();
}

bool Editor::undo() {
  if (undo_.empty()) return false;
  Snapshot s = undo_.back();
  undo_.pop_back();
  gesture_ = 0;
  if (s.instruments) {
    Snapshot cur = capture(false);
    cur.instruments = true;
    cur.insts = song_.instruments;
    cur.waves = song_.waves;
    redo_.push_back(cur);
    restore(s);
    return true;
  }
  // Save the state we are leaving with the same scope, then go back.
  int saveOrder = order_;
  order_ = clampi(s.order, 0, (int)song_.orders.size() - 1);
  Snapshot cur = capture(s.wholeSong);
  cur.patternIdx = s.patternIdx;
  if (!s.wholeSong) cur.pattern = song_.pattern(s.patternIdx);
  cur.order = saveOrder; cur.row = row_; cur.ch = ch_; cur.col = col_;
  redo_.push_back(cur);
  restore(s);
  return true;
}

bool Editor::redo() {
  if (redo_.empty()) return false;
  Snapshot s = redo_.back();
  redo_.pop_back();
  gesture_ = 0;
  if (s.instruments) {
    Snapshot cur = capture(false);
    cur.instruments = true;
    cur.insts = song_.instruments;
    cur.waves = song_.waves;
    undo_.push_back(cur);
    restore(s);
    return true;
  }
  Snapshot cur = capture(s.wholeSong);
  cur.patternIdx = s.patternIdx;
  if (!s.wholeSong) cur.pattern = song_.pattern(s.patternIdx);
  undo_.push_back(cur);
  restore(s);
  return true;
}

// --- data entry ---------------------------------------------------------------

void Editor::advance() {
  if (editStep > 0) moveRow(editStep);
}

void Editor::enterNote(int note) {
  if (!editMode) return;
  pushUndo();
  Cell& c = cell();
  if (note == NOTE_OFF) {
    c.note = NOTE_OFF;
    c.instrument = INST_NONE;
  } else {
    c.note = (uint8_t)clampi(note, 0, kNoteCount - 1);
    c.instrument = (uint8_t)clampi(instrument, 0, kMaxInstruments - 1);
  }
  advance();
}

void Editor::enterHexDigit(int digit) {
  if (!editMode || digit < 0 || digit > 15) return;
  if (col_ == COL_NOTE || col_ == COL_FX1_CMD || col_ == COL_FX2_CMD) return;
  pushUndo();
  Cell& c = cell();
  const bool high = (col_ == COL_INST_HI || col_ == COL_VOL_HI || col_ == COL_FX1_HI || col_ == COL_FX2_HI);
  switch (col_) {
    case COL_INST_HI: case COL_INST_LO: {
      int v = setNibble(c.instrument == INST_NONE ? 0 : c.instrument, high, digit);
      c.instrument = (uint8_t)(v >= kMaxInstruments ? kMaxInstruments - 1 : v);
      break;
    }
    case COL_VOL_HI: case COL_VOL_LO: {
      int v = setNibble(c.volume == VOL_NONE ? 0 : c.volume, high, digit);
      c.volume = (uint8_t)(v > kVolumeMax ? kVolumeMax : v); // 5-bit hardware volume
      break;
    }
    default: {
      Effect& f = c.fx[col_ >= COL_FX2_CMD ? 1 : 0];
      if (!f.cmd) f.cmd = '0';
      f.param = (uint8_t)setNibble(f.param, high, digit);
      break;
    }
  }
  if (high) {
    ++col_;
  } else {
    --col_;
    advance();
  }
}

void Editor::enterEffectCommand(char cmd) {
  if (!editMode || (col_ != COL_FX1_CMD && col_ != COL_FX2_CMD)) return;
  if (cmd >= 'a' && cmd <= 'z') cmd = (char)(cmd - 'a' + 'A');
  if (!((cmd >= '0' && cmd <= '9') || (cmd >= 'A' && cmd <= 'Z'))) return;
  pushUndo();
  cell().fx[col_ == COL_FX2_CMD ? 1 : 0].cmd = (uint8_t)cmd;
  advance();
}

void Editor::clearAtCursor() {
  if (!editMode) return;
  pushUndo();
  Cell& c = cell();
  switch (col_) {
    case COL_NOTE: c.note = NOTE_NONE; c.instrument = INST_NONE; c.volume = VOL_NONE; break;
    case COL_INST_HI: case COL_INST_LO: c.instrument = INST_NONE; break;
    case COL_VOL_HI: case COL_VOL_LO: c.volume = VOL_NONE; break;
    default: c.fx[col_ >= COL_FX2_CMD ? 1 : 0] = Effect(); break;
  }
  advance();
}

void Editor::insertRow() {
  if (!editMode) return;
  pushUndo();
  Pattern& p = pattern();
  for (int r = p.rows - 1; r > row_; --r) p.at(r, ch_) = p.at(r - 1, ch_);
  p.at(row_, ch_) = Cell();
}

void Editor::deleteRow() {
  if (!editMode) return;
  pushUndo();
  Pattern& p = pattern();
  for (int r = row_; r < p.rows - 1; ++r) p.at(r, ch_) = p.at(r + 1, ch_);
  p.at(p.rows - 1, ch_) = Cell();
}

// --- blocks -------------------------------------------------------------------

void Editor::startSelection() {
  selActive_ = true;
  selRow_ = row_;
  selCh_ = ch_;
}

void Editor::extendSelection() {
  if (!selActive_) startSelection();
}

void Editor::selectionRect(int* r0, int* c0, int* r1, int* c1) const {
  if (!selActive_) { *r0 = *r1 = row_; *c0 = *c1 = ch_; return; }
  *r0 = selRow_ < row_ ? selRow_ : row_;
  *r1 = selRow_ < row_ ? row_ : selRow_;
  *c0 = selCh_ < ch_ ? selCh_ : ch_;
  *c1 = selCh_ < ch_ ? ch_ : selCh_;
}

void Editor::selectChannel() {
  int r0, c0, r1, c1;
  selectionRect(&r0, &c0, &r1, &c1);
  const int last = pattern().rows - 1;
  bool wholeChannel = selActive_ && r0 == 0 && r1 == last && c0 == ch_ && c1 == ch_;
  selActive_ = true;
  selRow_ = 0;
  if (wholeChannel) { selCh_ = 0; row_ = last; ch_ = kChannels - 1; }
  else { selCh_ = ch_; row_ = last; }
}

void Editor::copy() {
  int r0, c0, r1, c1;
  selectionRect(&r0, &c0, &r1, &c1);
  clipW_ = c1 - c0 + 1;
  clipH_ = r1 - r0 + 1;
  clip_.resize((size_t)clipW_ * clipH_);
  for (int r = 0; r < clipH_; ++r)
    for (int c = 0; c < clipW_; ++c) clip_[(size_t)r * clipW_ + c] = pattern().at(r0 + r, c0 + c);
}

void Editor::cut() {
  if (!editMode) return;
  copy();
  pushUndo();
  int r0, c0, r1, c1;
  selectionRect(&r0, &c0, &r1, &c1);
  for (int r = r0; r <= r1; ++r)
    for (int c = c0; c <= c1; ++c) pattern().at(r, c) = Cell();
  selActive_ = false;
}

void Editor::paste() {
  if (!editMode || clipW_ == 0) return;
  pushUndo();
  Pattern& p = pattern();
  for (int r = 0; r < clipH_ && row_ + r < p.rows; ++r)
    for (int c = 0; c < clipW_ && ch_ + c < kChannels; ++c) p.at(row_ + r, ch_ + c) = clip_[(size_t)r * clipW_ + c];
  selActive_ = false;
}

void Editor::transpose(int semis) {
  if (!editMode) return;
  pushUndo();
  int r0, c0, r1, c1;
  selectionRect(&r0, &c0, &r1, &c1);
  for (int r = r0; r <= r1; ++r)
    for (int c = c0; c <= c1; ++c) {
      Cell& x = pattern().at(r, c);
      if (x.note < kNoteCount) x.note = (uint8_t)clampi(x.note + semis, 0, kNoteCount - 1);
    }
}

// --- orders -------------------------------------------------------------------

void Editor::changeOrderPattern(int d) {
  if (!editMode) return;
  pushUndo(true);
  int v = clampi(song_.orders[order_] + d, 0, kMaxPatterns - 1);
  song_.orders[order_] = (uint8_t)v;
  song_.pattern(v);
  clampCursor();
}

void Editor::insertOrder() {
  if (!editMode || (int)song_.orders.size() >= kMaxOrders) return;
  pushUndo(true);
  song_.orders.insert(song_.orders.begin() + order_ + 1, song_.orders[order_]);
  ++order_;
  clampCursor();
}

void Editor::deleteOrder() {
  if (!editMode || song_.orders.size() <= 1) return;
  pushUndo(true);
  song_.orders.erase(song_.orders.begin() + order_);
  clampCursor();
}

void Editor::newPatternAtOrder() {
  if (!editMode || (int)song_.orders.size() >= kMaxOrders) return;
  std::vector<bool> used(kMaxPatterns, false);
  for (size_t i = 0; i < song_.orders.size(); ++i) used[song_.orders[i]] = true;
  int freeIdx = -1;
  for (int i = 0; i < kMaxPatterns; ++i) {
    bool empty = i >= (int)song_.patterns.size();
    if (!empty) {
      empty = true;
      const Pattern& p = song_.patterns[i];
      for (size_t k = 0; k < p.cells.size() && empty; ++k) empty = p.cells[k].empty();
    }
    if (!used[i] && empty) { freeIdx = i; break; }
  }
  if (freeIdx < 0) return;
  pushUndo(true);
  int rows = pattern().rows;
  song_.pattern(freeIdx) = Pattern(rows);
  song_.orders.insert(song_.orders.begin() + order_ + 1, (uint8_t)freeIdx);
  ++order_;
  row_ = 0;
  clampCursor();
}

void Editor::clonePatternAtOrder() {
  if (!editMode) return;
  int src = patternIndex();
  newPatternAtOrder();
  if (patternIndex() != src) pattern() = song_.patterns[src];
}

void Editor::changePatternLength(int d) {
  if (!editMode) return;
  int rows = clampi(pattern().rows + d, 1, kMaxRows);
  if (rows == pattern().rows) return;
  pushUndo();
  pattern().resize(rows);
  clampCursor();
}

void Editor::changeSpeed(int d) {
  if (!editMode) return;
  int s = clampi(song_.speed + d, 1, 31);
  if (s == song_.speed) return;
  pushUndo();
  song_.speed = s;
}

// --- instruments --------------------------------------------------------------

Instrument& Editor::currentInstrument() {
  if (song_.instruments.empty()) song_.instruments.push_back(Instrument());
  instrument = clampi(instrument, 0, (int)song_.instruments.size() - 1);
  return song_.instruments[instrument];
}

void Editor::setInstrumentName(const std::string& name) {
  pushInstrumentUndo();
  currentInstrument().name = name.substr(0, 32);
}

void Editor::setSongTitle(const std::string& t) {
  pushInstrumentUndo();
  song_.title = t.substr(0, 64);
}

void Editor::changeInstrumentField(int field, int d) {
  pushInstrumentUndo();
  Instrument& in = currentInstrument();
  switch (field) {
    case IF_TYPE:
      // Wave <-> DDA. Switching to DDA picks the first sample if there is one.
      if (in.sample >= 0) in.sample = -1;
      else if (!song_.samples.empty()) in.sample = 0;
      break;
    case IF_WAVE:
      in.wave = (uint8_t)clampi(in.wave + d, 0, (int)song_.waves.size() - 1);
      break;
    case IF_SAMPLE:
      if (!song_.samples.empty()) in.sample = clampi(in.sample + d, 0, (int)song_.samples.size() - 1);
      break;
    case IF_VOLUME:
      in.volume = (uint8_t)clampi(in.volume + d, 0, kVolumeMax);
      break;
    case IF_PHASE:
      in.resetPhase = !in.resetPhase;
      break;
    default: break;
  }
}

void Editor::newInstrument() {
  if ((int)song_.instruments.size() >= kMaxInstruments) return;
  pushInstrumentUndo();
  Instrument in;
  char b[16];
  snprintf(b, sizeof b, "inst %02X", (int)song_.instruments.size());
  in.name = b;
  song_.instruments.push_back(in);
  instrument = (int)song_.instruments.size() - 1;
}

void Editor::cloneInstrument() {
  if ((int)song_.instruments.size() >= kMaxInstruments) return;
  pushInstrumentUndo();
  Instrument in = currentInstrument();
  in.name = (in.name + " copy").substr(0, 32);
  song_.instruments.push_back(in);
  instrument = (int)song_.instruments.size() - 1;
}

int Editor::currentWaveIndex() const {
  if (song_.instruments.empty() || song_.waves.empty()) return 0;
  int i = clampi(instrument, 0, (int)song_.instruments.size() - 1);
  return clampi(song_.instruments[i].wave, 0, (int)song_.waves.size() - 1);
}

void Editor::setWaveSample(int i, int v, int gesture) {
  if (i < 0 || i >= kWaveLength || song_.waves.empty()) return;
  pushInstrumentUndo(gesture);
  song_.waves[currentWaveIndex()].data[i] = (uint8_t)clampi(v, 0, kWaveMax);
}

void Editor::applyWavePreset(int preset) {
  if (song_.waves.empty()) return;
  pushInstrumentUndo();
  uint8_t* w = song_.waves[currentWaveIndex()].data;
  uint8_t old[kWaveLength];
  for (int i = 0; i < kWaveLength; ++i) old[i] = w[i];
  uint32_t lfsr = 0x1234567u;
  for (int i = 0; i < kWaveLength; ++i) {
    int v = w[i];
    switch (preset) {
      case WP_SINE: v = (int)floor(15.5 + 15.5 * sin(2.0 * 3.14159265358979 * i / kWaveLength) + 0.5); break;
      case WP_SQUARE: v = i < kWaveLength / 2 ? kWaveMax : 0; break;
      case WP_SAW: v = i; break;
      case WP_TRIANGLE: v = i < 16 ? i * 2 : (31 - i) * 2; break;
      case WP_NOISE: lfsr = lfsr * 1103515245u + 12345u; v = (int)((lfsr >> 16) & 31); break;
      case WP_CLEAR: v = 16; break;
      case WP_INVERT: v = kWaveMax - old[i]; break;
      case WP_SHIFT: v = old[(i + 1) % kWaveLength]; break;       // phase shift by one step
      case WP_HALF: v = 16 + (old[i] - 16) / 2; break;            // half amplitude around center
      default: break;
    }
    w[i] = (uint8_t)clampi(v, 0, kWaveMax);
  }
}

void Editor::newWave() {
  if ((int)song_.waves.size() >= kMaxWaves) return;
  pushInstrumentUndo();
  Wave w = song_.waves.empty() ? Wave() : song_.waves[currentWaveIndex()];
  song_.waves.push_back(w);
  currentInstrument().wave = (uint8_t)(song_.waves.size() - 1);
}

void Editor::macroRange(int kind, int* lo, int* hi) {
  switch (kind) {
    case MACRO_VOLUME: *lo = 0; *hi = kVolumeMax; break;
    case MACRO_ARP: *lo = -48; *hi = 48; break;
    case MACRO_PITCH: *lo = -64; *hi = 63; break;
    case MACRO_WAVE: *lo = 0; *hi = kMaxWaves - 1; break;
    case MACRO_NOISE: *lo = 0; *hi = 32; break;
    default: *lo = -128; *hi = 127; break; // pan: raw byte (shown as $LR)
  }
}

void Editor::macroSet(int kind, int i, int v, int gesture) {
  if (kind < 0 || kind >= MACRO_COUNT || i < 0 || i >= kMaxMacroLength) return;
  pushInstrumentUndo(gesture);
  Macro& m = currentInstrument().macros[kind];
  int lo, hi;
  macroRange(kind, &lo, &hi);
  int8_t fill = m.values.empty() ? (int8_t)clampi(kind == MACRO_VOLUME ? kVolumeMax : 0, lo, hi) : m.values.back();
  while ((int)m.values.size() <= i) m.values.push_back(fill);
  m.values[i] = (int8_t)clampi(v, lo, hi);
}

void Editor::macroInsert(int kind, int i) {
  Macro& m = currentInstrument().macros[kind];
  if ((int)m.values.size() >= kMaxMacroLength) return;
  pushInstrumentUndo();
  int8_t v = m.values.empty() ? (int8_t)(kind == MACRO_VOLUME ? kVolumeMax : 0)
                              : m.values[clampi(i, 0, (int)m.values.size() - 1)];
  i = clampi(i, 0, (int)m.values.size());
  m.values.insert(m.values.begin() + i, v);
  if (m.loop >= i) ++m.loop;
  if (m.release >= i) ++m.release;
}

void Editor::macroDelete(int kind, int i) {
  Macro& m = currentInstrument().macros[kind];
  if (i < 0 || i >= (int)m.values.size()) return;
  pushInstrumentUndo();
  m.values.erase(m.values.begin() + i);
  if (m.loop > i) --m.loop; else if (m.loop == i) m.loop = -1;
  if (m.release > i) --m.release; else if (m.release == i) m.release = -1;
  if (m.loop >= (int)m.values.size()) m.loop = -1;
  if (m.release >= (int)m.values.size()) m.release = -1;
}

void Editor::macroToggleLoop(int kind, int i) {
  Macro& m = currentInstrument().macros[kind];
  if (i < 0 || i >= (int)m.values.size()) return;
  pushInstrumentUndo();
  m.loop = m.loop == i ? -1 : i;
}

void Editor::macroToggleRelease(int kind, int i) {
  Macro& m = currentInstrument().macros[kind];
  if (i < 0 || i >= (int)m.values.size()) return;
  pushInstrumentUndo();
  m.release = m.release == i ? -1 : i;
}

void Editor::macroClear(int kind) {
  pushInstrumentUndo();
  currentInstrument().macros[kind] = Macro();
}

int Editor::addSample(const Sample& smp) {
  if ((int)song_.samples.size() >= kMaxSamples) return -1;
  song_.samples.push_back(smp); // sample data itself is not part of undo (memory)
  pushInstrumentUndo();
  currentInstrument().sample = (int)song_.samples.size() - 1;
  return currentInstrument().sample;
}

} // namespace pce
