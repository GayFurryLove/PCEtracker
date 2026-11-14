// PCEtracker - text-cell screen on top of SDL_Renderer.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// The whole UI is a 128x75 grid of 8x8 cells (1024x600 logical pixels),
// scaled by an integer factor. Drawing is plain rectangles and glyph blits.

#ifndef PCETRACKER_SCREEN_H
#define PCETRACKER_SCREEN_H

#include <SDL.h>

namespace pce {

const int kScreenW = 1024, kScreenH = 600;
const int kCell = 8;
const int kCols = kScreenW / kCell; // 128
const int kRows = kScreenH / kCell; // 75

// Palette: deep black, PC Engine orange, white + a few derived shades.
enum Color {
  C_BLACK = 0, C_ORANGE, C_WHITE,
  C_DIM,        // empty fields, separators
  C_ORANGE_DK,  // beat-row and cursor-row background
  C_ORANGE_MID, // selection background, inactive panel lines
  C_RED,        // muted channel marker
  C_COUNT
};

class Screen {
public:
  Screen();
  ~Screen();
  bool init(SDL_Renderer* r);

  void clear();
  void fill(int col, int row, int w, int h, Color c);       // in cells
  void fillPx(int x, int y, int w, int h, Color c);         // in pixels
  void text(int col, int row, const char* s, Color fg);
  void textBg(int col, int row, const char* s, Color fg, Color bg);
  void glyph(int col, int row, unsigned char ch, Color fg);
  void frame(int col, int row, int w, int h, Color c);      // 1px box around cells
  void present();

  // A pre-rendered string: drawing it is one blit instead of one per glyph.
  // Used for text that repeats thousands of times per frame (empty cells).
  SDL_Texture* makeString(const char* s, Color fg);
  void blit(SDL_Texture* t, int col, int row);

private:
  void setColor(Color c);
  SDL_Renderer* r_;
  SDL_Texture* font_;
};

} // namespace pce

#endif
