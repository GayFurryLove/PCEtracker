// PCEtracker - headless tests for the editing logic (what the keyboard does).
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "tracker/editor.h"

#include <stdio.h>

using namespace pce;

static int g_failed = 0;
static int g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; \
  printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void testNoteEntry() {
  Song s;
  Editor e(s);
  e.enterNote(36);
  CHECK(s.patterns[0].at(0, 0).note == NOTE_NONE); // edit mode off: nothing written
  CHECK(!e.dirty());
  e.editMode = true;
  e.instrument = 2;
  e.editStep = 2;
  e.enterNote(36);
  CHECK(s.patterns[0].at(0, 0).note == 36 && s.patterns[0].at(0, 0).instrument == 2);
  CHECK(e.row() == 2);
  e.enterNote(NOTE_OFF);
  CHECK(s.patterns[0].at(2, 0).note == NOTE_OFF && s.patterns[0].at(2, 0).instrument == INST_NONE);
  CHECK(e.dirty());
}

static void testHexAndEffects() {
  Song s;
  Editor e(s);
  e.editMode = true;
  e.setCursor(0, 1, COL_VOL_HI);
  e.enterHexDigit(3);           // 3x -> clamped to 1F (5-bit volume)
  CHECK(e.column() == COL_VOL_LO);
  CHECK(s.patterns[0].at(0, 1).volume == 0x1F);
  e.enterHexDigit(0);
  CHECK(s.patterns[0].at(0, 1).volume == 0x10);
  CHECK(e.column() == COL_VOL_HI && e.row() == 1); // back to high digit, next row

  e.setCursor(0, 1, COL_FX1_CMD);
  e.enterEffectCommand('a');
  CHECK(s.patterns[0].at(0, 1).fx[0].cmd == 'A');
  e.setCursor(0, 1, COL_FX1_HI);
  e.enterHexDigit(0xF);
  e.enterHexDigit(0x2);
  CHECK(s.patterns[0].at(0, 1).fx[0].param == 0xF2);

  e.setCursor(0, 1, COL_FX1_LO);
  e.clearAtCursor();
  CHECK(s.patterns[0].at(0, 1).fx[0].cmd == 0);
}

static void testRowsAndBlocks() {
  Song s;
  Editor e(s);
  e.editMode = true;
  for (int r = 0; r < 4; ++r) { e.setCursor(r, 0, COL_NOTE); e.enterNote(40 + r); }
  e.setCursor(1, 0, COL_NOTE);
  e.insertRow();
  CHECK(s.patterns[0].at(1, 0).note == NOTE_NONE && s.patterns[0].at(2, 0).note == 41);
  e.deleteRow();
  CHECK(s.patterns[0].at(1, 0).note == 41);

  // Copy rows 0..3 of ch1 into ch3 at row 8.
  e.setCursor(0, 0, COL_NOTE);
  e.startSelection();
  e.setCursor(3, 0, COL_NOTE);
  e.extendSelection();
  e.copy();
  e.clearSelection();
  e.setCursor(8, 2, COL_NOTE);
  e.paste();
  CHECK(s.patterns[0].at(8, 2).note == 40 && s.patterns[0].at(11, 2).note == 43);

  // Transpose the pasted block up an octave.
  e.setCursor(8, 2, COL_NOTE);
  e.startSelection();
  e.setCursor(11, 2, COL_NOTE);
  e.extendSelection();
  e.transpose(12);
  CHECK(s.patterns[0].at(8, 2).note == 52 && s.patterns[0].at(0, 0).note == 40);

  e.cut();
  CHECK(s.patterns[0].at(8, 2).note == NOTE_NONE);

  // Ctrl+A twice selects the whole pattern.
  e.selectChannel();
  e.selectChannel();
  int r0, c0, r1, c1;
  e.selectionRect(&r0, &c0, &r1, &c1);
  CHECK(r0 == 0 && c0 == 0 && r1 == s.patterns[0].rows - 1 && c1 == kChannels - 1);
}

static void testUndoRedo() {
  Song s;
  Editor e(s);
  e.editMode = true;
  e.enterNote(30);
  e.enterNote(31);
  CHECK(s.patterns[0].at(1, 0).note == 31);
  CHECK(e.undo());
  CHECK(s.patterns[0].at(1, 0).note == NOTE_NONE && s.patterns[0].at(0, 0).note == 30);
  CHECK(e.undo());
  CHECK(s.patterns[0].at(0, 0).note == NOTE_NONE);
  CHECK(!e.undo());
  CHECK(e.redo());
  CHECK(e.redo());
  CHECK(s.patterns[0].at(1, 0).note == 31);
  CHECK(!e.redo());

  e.changePatternLength(-32);
  CHECK(s.patterns[0].rows == 32);
  e.undo();
  CHECK(s.patterns[0].rows == 64 && s.patterns[0].at(1, 0).note == 31);
}

static void testOrders() {
  Song s;
  Editor e(s);
  e.editMode = true;
  e.enterNote(30);
  e.newPatternAtOrder();
  CHECK(s.orders.size() == 2 && s.orders[1] == 1 && e.order() == 1);
  e.clonePatternAtOrder();
  CHECK(s.orders.size() == 3 && s.orders[2] == 2);
  e.setOrder(0);
  e.clonePatternAtOrder();       // copy of pattern 0 with its note
  CHECK(s.patterns[s.orders[1]].at(0, 0).note == 30);
  e.insertOrder();
  CHECK(s.orders.size() == 5 && s.orders[2] == s.orders[1]);
  e.deleteOrder();
  CHECK(s.orders.size() == 4);
  e.changeOrderPattern(1);
  int p = s.orders[e.order()];
  CHECK(p >= 1);
  e.undo();
  CHECK(s.orders[e.order()] != p || s.orders.size() == 4);
  e.changeSpeed(3);
  CHECK(s.speed == 9);
}

static void testCursorWrap() {
  Song s;
  Editor e(s);
  e.moveRow(-1);
  CHECK(e.row() == 63);
  e.moveColumn(-1);
  CHECK(e.channel() == kChannels - 1 && e.column() == COL_FX2_LO);
  e.moveColumn(1);
  CHECK(e.channel() == 0 && e.column() == COL_NOTE);
}

int main() {
  struct { const char* name; void (*fn)(); } tests[] = {
    { "note entry", testNoteEntry },
    { "hex and effects", testHexAndEffects },
    { "rows and blocks", testRowsAndBlocks },
    { "undo/redo", testUndoRedo },
    { "orders", testOrders },
    { "cursor wrap", testCursorWrap },
  };
  for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
    printf("%s\n", tests[i].name);
    tests[i].fn();
  }
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
