// PCEtracker - order screen (F8 / F11): the order list, the pattern list and
// a preview of the pattern under the cursor. Arranging does not need edit mode.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "ui/app.h"

#include <stdio.h>
#include <string.h>

namespace pce {

namespace {

const int kOrdX = 1, kOrdW = 30;      // order list
const int kPatX = 33, kPatW = 34;     // pattern list
const int kPrevX = 69;                // preview of the pattern under the cursor
const int kViewHeader = 15, kViewBottom = 73; // same rows as the pattern view (app.cpp)

int listTop(int cursor, int count, int visible) {
  if (count <= visible) return 0;
  int t = cursor - visible / 2;
  if (t < 0) t = 0;
  if (t > count - visible) t = count - visible;
  return t;
}

std::string hex2(int v) {
  char b[4];
  snprintf(b, sizeof b, "%02X", v & 0xFF);
  return b;
}

int hexDigit(SDL_Scancode sc) {
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return 1 + (sc - SDL_SCANCODE_1);
  if (sc == SDL_SCANCODE_0) return 0;
  if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return 1 + (sc - SDL_SCANCODE_KP_1);
  if (sc == SDL_SCANCODE_KP_0) return 0;
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_F) return 10 + (sc - SDL_SCANCODE_A);
  return -1;
}

int cellsUsed(const Pattern& p) {
  int n = 0;
  for (size_t i = 0; i < p.cells.size(); ++i) n += !p.cells[i].empty();
  return n;
}

// Edit mode on for the duration of a call (orders/pattern list do not need it).
struct EditOn {
  bool& f; bool old;
  explicit EditOn(bool& flag) : f(flag), old(flag) { f = true; }
  ~EditOn() { f = old; }
};

} // namespace

bool App::handleOrderKey(const SDL_KeyboardEvent& k) {
  const SDL_Scancode sc = k.keysym.scancode;
  const Uint16 mod = k.keysym.mod;
  const bool ctrl = (mod & KMOD_CTRL) != 0, shift = (mod & KMOD_SHIFT) != 0, alt = (mod & KMOD_ALT) != 0;
  const int nOrd = (int)song_.orders.size();
  const int nPat = (int)song_.patterns.size();
  if (alt) return false;
  if (ctrl) {
    EditOn on(editor_.editMode);
    switch (sc) {
      case SDL_SCANCODE_N: editor_.newPatternAtOrder(); message("New empty pattern " + hex2(editor_.patternIndex())); return true;
      case SDL_SCANCODE_D: editor_.clonePatternAtOrder(); message("Pattern cloned as " + hex2(editor_.patternIndex())); return true;
      case SDL_SCANCODE_INSERT: editor_.insertOrder(); return true;
      case SDL_SCANCODE_DELETE: editor_.deleteOrder(); return true;
      default: return false; // undo, save, mute ... stay global
    }
  }
  if (sc == SDL_SCANCODE_TAB) { orderFocus_ ^= 1; if (orderFocus_) patCursor_ = editor_.patternIndex(); return true; }

  if (orderFocus_ == 0) {
    // --- order list ---
    if (shift && (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN)) {
      editor_.moveOrder(sc == SDL_SCANCODE_UP ? -1 : 1);
      return true;
    }
    switch (sc) {
      case SDL_SCANCODE_UP: editor_.setOrder(editor_.order() - 1); return true;
      case SDL_SCANCODE_DOWN: editor_.setOrder(editor_.order() + 1); return true;
      case SDL_SCANCODE_PAGEUP: editor_.setOrder(editor_.order() - 16); return true;
      case SDL_SCANCODE_PAGEDOWN: editor_.setOrder(editor_.order() + 16); return true;
      case SDL_SCANCODE_HOME: editor_.setOrder(0); return true;
      case SDL_SCANCODE_END: editor_.setOrder(nOrd - 1); return true;
      case SDL_SCANCODE_INSERT: { EditOn on(editor_.editMode); editor_.insertOrder(); return true; }
      case SDL_SCANCODE_DELETE: case SDL_SCANCODE_BACKSPACE: { EditOn on(editor_.editMode); editor_.deleteOrder(); return true; }
      case SDL_SCANCODE_EQUALS: case SDL_SCANCODE_KP_PLUS: editor_.setOrderPattern(editor_.patternIndex() + 1); return true;
      case SDL_SCANCODE_MINUS: case SDL_SCANCODE_KP_MINUS: editor_.setOrderPattern(editor_.patternIndex() - 1); return true;
      default: break;
    }
    int d = hexDigit(sc);
    if (d >= 0) {
      // Digits shift in from the right: "0" "5" -> 05, like most trackers.
      editor_.setOrderPattern(((editor_.patternIndex() << 4) | d) & 0xFF);
      return true;
    }
    return false; // ENTER plays, F-keys etc. stay global
  }

  // --- pattern list ---
  switch (sc) {
    case SDL_SCANCODE_UP: if (patCursor_ > 0) --patCursor_; return true;
    case SDL_SCANCODE_DOWN: if (patCursor_ + 1 < nPat) ++patCursor_; return true;
    case SDL_SCANCODE_PAGEUP: patCursor_ = patCursor_ > 16 ? patCursor_ - 16 : 0; return true;
    case SDL_SCANCODE_PAGEDOWN: patCursor_ = patCursor_ + 16 < nPat ? patCursor_ + 16 : nPat - 1; return true;
    case SDL_SCANCODE_HOME: patCursor_ = 0; return true;
    case SDL_SCANCODE_END: patCursor_ = nPat - 1; return true;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
      editor_.setOrderPattern(patCursor_);
      message("Order " + hex2(editor_.order()) + " plays pattern " + hex2(patCursor_));
      return true;
    case SDL_SCANCODE_INSERT:
      editor_.insertOrderPattern(patCursor_);
      message("Pattern " + hex2(patCursor_) + " added at order " + hex2(editor_.order()));
      return true;
    default: return false;
  }
}

void App::handleOrderMouse(const SDL_Event& e) {
  const int top = kViewHeader + 2, rows = kViewBottom - top + 1;
  if (e.type == SDL_MOUSEWHEEL) {
    if (orderFocus_ == 0) editor_.setOrder(editor_.order() + (e.wheel.y > 0 ? -3 : 3));
    else {
      patCursor_ += e.wheel.y > 0 ? -3 : 3;
      if (patCursor_ >= (int)song_.patterns.size()) patCursor_ = (int)song_.patterns.size() - 1;
      if (patCursor_ < 0) patCursor_ = 0;
    }
    return;
  }
  if (e.type != SDL_MOUSEBUTTONDOWN) return;
  const int cx = e.button.x / kCell, cy = e.button.y / kCell;
  if (cy < top || cy >= top + rows) return;
  if (cx >= kOrdX && cx < kOrdX + kOrdW) {
    int o = listTop(editor_.order(), (int)song_.orders.size(), rows) + (cy - top);
    if (o < (int)song_.orders.size()) {
      orderFocus_ = 0;
      if (o == editor_.order() && e.button.clicks >= 2) view_ = VIEW_PATTERN; // double click: edit it
      editor_.setOrder(o);
    }
  } else if (cx >= kPatX && cx < kPatX + kPatW) {
    int p = listTop(patCursor_, (int)song_.patterns.size(), rows) + (cy - top);
    if (p < (int)song_.patterns.size()) {
      orderFocus_ = 1;
      if (p == patCursor_ && e.button.clicks >= 2) editor_.setOrderPattern(p);
      patCursor_ = p;
    }
  }
}

void App::drawOrderView(int top, int bottom) {
  char b[128];
  screen_.fill(0, top, kCols, 1, C_ORANGE);
  snprintf(b, sizeof b, "ORDERS %02X POSITIONS   PATTERNS %02X", (int)song_.orders.size(), (int)song_.patterns.size());
  screen_.text(1, top, b, C_BLACK);
  const char* hint = "F9 EDIT PATTERN  CTRL+N NEW  CTRL+D CLONE  TAB LIST";
  screen_.text(kCols - 1 - (int)strlen(hint), top, hint, C_BLACK);

  const int y0 = top + 2, rows = bottom - y0 + 1;
  if (patCursor_ >= (int)song_.patterns.size()) patCursor_ = (int)song_.patterns.size() - 1;
  if (patCursor_ < 0) patCursor_ = 0;

  // --- order list ---
  screen_.text(kOrdX, top + 1, "POS  PATTERN  ROWS", orderFocus_ == 0 ? C_ORANGE : C_DIM);
  const int nOrd = (int)song_.orders.size();
  int first = listTop(editor_.order(), nOrd, rows);
  for (int i = 0; i < rows; ++i) {
    int o = first + i;
    if (o >= nOrd) break;
    const int pi = song_.orders[o];
    const int prow = pi < (int)song_.patterns.size() ? song_.patterns[pi].rows : 64;
    snprintf(b, sizeof b, " %02X    %02X      %02X        ", o, pi, prow);
    const bool cur = o == editor_.order();
    const int y = y0 + i;
    if (cur && orderFocus_ == 0) screen_.textBg(kOrdX, y, b, C_BLACK, C_ORANGE);
    else if (cur) screen_.textBg(kOrdX, y, b, C_WHITE, C_ORANGE_DK);
    else screen_.text(kOrdX, y, b, (orderFocus_ == 1 && pi == patCursor_) ? C_ORANGE : C_WHITE);
    if (playing_ && o == playOrder_) screen_.text(kOrdX + 22, y, "<PLAY", cur ? C_BLACK : C_ORANGE);
  }

  // --- pattern list ---
  screen_.text(kPatX, top + 1, "PAT  ROWS  USED  CELLS", orderFocus_ == 1 ? C_ORANGE : C_DIM);
  const int nPat = (int)song_.patterns.size();
  first = listTop(patCursor_, nPat, rows);
  for (int i = 0; i < rows; ++i) {
    int p = first + i;
    if (p >= nPat) break;
    const int used = editor_.patternUseCount(p);
    const int cells = cellsUsed(song_.patterns[p]);
    snprintf(b, sizeof b, " %02X   %02X    %s   %s        ", p, song_.patterns[p].rows,
             used ? (std::string("x") + hex2(used)).c_str() : "-- ", cells ? hex2(cells > 255 ? 255 : cells).c_str() : "empty");
    const int y = y0 + i;
    const bool cur = p == patCursor_;
    if (cur && orderFocus_ == 1) screen_.textBg(kPatX, y, b, C_BLACK, C_ORANGE);
    else if (cur) screen_.textBg(kPatX, y, b, C_WHITE, C_ORANGE_DK);
    else screen_.text(kPatX, y, b, p == editor_.patternIndex() ? C_ORANGE : (used ? C_WHITE : C_DIM));
  }

  // --- preview: notes of the pattern under the cursor ---
  const int pv = orderFocus_ == 1 ? patCursor_ : editor_.patternIndex();
  snprintf(b, sizeof b, "PATTERN %02X PREVIEW", pv);
  screen_.text(kPrevX, top + 1, b, C_DIM);
  if (pv < nPat) {
    const Pattern& p = song_.patterns[pv];
    for (int r = 0; r < rows && r < p.rows; ++r) {
      const int y = y0 + r;
      screen_.text(kPrevX, y, hex2(r).c_str(), r % 4 == 0 ? C_WHITE : C_DIM);
      for (int c = 0; c < kChannels; ++c) {
        const Cell& cl = p.at(r, c);
        const int x = kPrevX + 3 + c * 9;
        std::string nn = noteName(cl.note);
        screen_.text(x, y, nn.c_str(), cl.note == NOTE_NONE ? C_DIM : C_WHITE);
        screen_.text(x + 4, y, cl.instrument == INST_NONE ? ".." : hex2(cl.instrument).c_str(),
                     cl.instrument == INST_NONE ? C_DIM : C_ORANGE);
      }
    }
  }
}

} // namespace pce
