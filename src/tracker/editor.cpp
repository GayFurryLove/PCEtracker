// PCEtracker - pattern/order editing logic.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "tracker/editor.h"

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
    selActive_(false), selRow_(0), selCh_(0), clipW_(0), clipH_(0), dirty_(false) {
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
  return s;
}

void Editor::restore(const Snapshot& s) {
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
  undo_.push_back(capture(wholeSong));
  if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
  redo_.clear();
  dirty_ = true;
}

bool Editor::undo() {
  if (undo_.empty()) return false;
  Snapshot s = undo_.back();
  undo_.pop_back();
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

} // namespace pce
