// PCEtracker - pattern/order editing logic (module 2, no UI dependency).
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// Everything the keyboard can do to a song lives here, so it is testable
// without a window. The UI only maps keys to these calls and draws state.

#ifndef PCETRACKER_EDITOR_H
#define PCETRACKER_EDITOR_H

#include "tracker/song.h"

#include <string>
#include <vector>

namespace pce {

// Cursor columns inside one channel: note, instrument (2 digits), volume (2),
// then per effect column: command letter + 2 parameter digits.
enum EditColumn {
  COL_NOTE = 0,
  COL_INST_HI, COL_INST_LO,
  COL_VOL_HI, COL_VOL_LO,
  COL_FX1_CMD, COL_FX1_HI, COL_FX1_LO,
  COL_FX2_CMD, COL_FX2_HI, COL_FX2_LO,
  COL_COUNT
};

// Instrument parameters editable from the instrument screen.
enum InstField { IF_TYPE = 0, IF_WAVE, IF_SAMPLE, IF_VOLUME, IF_PHASE, IF_COUNT };

enum WavePreset { WP_SINE = 0, WP_SQUARE, WP_SAW, WP_TRIANGLE, WP_NOISE, WP_CLEAR,
                  WP_INVERT, WP_SHIFT, WP_HALF, WP_COUNT };

class Editor {
public:
  explicit Editor(Song& song);

  Song& song() { return song_; }
  const Song& song() const { return song_; }

  // --- cursor ---------------------------------------------------------------
  int order() const { return order_; }
  int row() const { return row_; }
  int channel() const { return ch_; }
  int column() const { return col_; }
  int patternIndex() const;
  Pattern& pattern();
  const Pattern& pattern() const;

  void moveRow(int delta);          // wraps inside the pattern
  void setRow(int row);
  void moveColumn(int delta);       // crosses channels
  void moveChannel(int delta);      // keeps the column
  void setCursor(int row, int ch, int col);
  void setOrder(int order);         // also clamps the row

  // --- edit settings ----------------------------------------------------------
  int octave;        // base octave for the lower keyboard row (0..7, C-1 = octave 0)
  int instrument;    // instrument written with notes
  int editStep;      // rows to advance after an entry (0..16)
  bool editMode;     // off: keys only preview, nothing is written

  // --- data entry (no-ops when editMode is off) -----------------------------
  void enterNote(int note);         // absolute note 0..95, or NOTE_OFF
  void enterHexDigit(int digit);    // instrument/volume/effect parameter columns
  void enterEffectCommand(char cmd);// 0-9, A-Z in a command column
  void clearAtCursor();             // the field under the cursor (note clears note+inst+vol)
  void insertRow();                 // channel only, rows below shift down
  void deleteRow();                 // channel only, rows below shift up (pulls up)

  // --- blocks -----------------------------------------------------------------
  bool hasSelection() const { return selActive_; }
  void selectionRect(int* r0, int* c0, int* r1, int* c1) const; // inclusive rows/channels
  void startSelection();            // anchor at the cursor
  void extendSelection();           // call after moving the cursor
  void selectChannel();             // whole channel, or the whole pattern if already selected
  void clearSelection() { selActive_ = false; }
  void copy();
  void cut();
  void paste();                     // at the cursor (top-left of the block)
  void transpose(int semitones);    // selection, or the note under the cursor
  bool clipboardEmpty() const { return clipW_ == 0; }

  // --- orders and patterns ----------------------------------------------------
  void changeOrderPattern(int delta);   // pattern number at the current order position
  void insertOrder();                   // duplicate the current entry after it
  void deleteOrder();
  void newPatternAtOrder();             // first unused pattern, placed after the current order
  void clonePatternAtOrder();           // copy of the current pattern, placed after it
  void changePatternLength(int delta);
  void changeSpeed(int delta);

  // --- instruments, waves, macros (instrument screen) ------------------------
  // Edits apply to `instrument` (the current instrument). Instrument edits are
  // undoable; consecutive edits with the same non-zero gesture key (mouse
  // drawing) merge into one undo step until endGesture().
  Instrument& currentInstrument();
  void setInstrumentName(const std::string& name);
  void changeInstrumentField(int field, int delta);
  void newInstrument();
  void cloneInstrument();
  int currentWaveIndex() const;
  void setWaveSample(int i, int value, int gesture = 0);
  void applyWavePreset(int preset);
  void newWave();                       // copy of the current wave, assigned to the instrument
  void macroSet(int kind, int i, int value, int gesture = 0); // grows the macro if needed
  void macroInsert(int kind, int i);
  void macroDelete(int kind, int i);
  void macroToggleLoop(int kind, int i);
  void macroToggleRelease(int kind, int i);
  void macroClear(int kind);
  static void macroRange(int kind, int* lo, int* hi);
  int addSample(const Sample& s);       // appends, assigns to the instrument; returns index (-1 = full)
  void setSongTitle(const std::string& t);
  void endGesture() { gesture_ = 0; }

  // --- undo -------------------------------------------------------------------
  bool undo();
  bool redo();
  bool dirty() const { return dirty_; }
  void markSaved() { dirty_ = false; }
  // After loading another song: forget undo history and selection, cursor home.
  void reset() { undo_.clear(); redo_.clear(); selActive_ = false; dirty_ = false; gesture_ = 0;
                 order_ = row_ = ch_ = col_ = 0; instrument = 0; clampCursor(); }

private:
  struct Snapshot {
    bool wholeSong;
    int patternIdx;
    Pattern pattern;
    std::vector<uint8_t> orders;
    std::vector<Pattern> patterns;
    int speed;
    int order, row, ch, col;
    bool instruments;
    std::vector<Instrument> insts;
    std::vector<Wave> waves;
    std::string title;
    int instrument;
  };

  Snapshot capture(bool wholeSong) const;
  void restore(const Snapshot& s);
  void pushUndo(bool wholeSong = false);
  void pushInstrumentUndo(int gesture = 0);
  void advance();
  Cell& cell();
  void clampCursor();

  Song& song_;
  int order_, row_, ch_, col_;
  bool selActive_;
  int selRow_, selCh_;  // anchor
  std::vector<Cell> clip_;
  int clipW_, clipH_;
  std::vector<Snapshot> undo_, redo_;
  bool dirty_;
  int gesture_;
};

} // namespace pce

#endif
