// PCEtracker - text-cell screen.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "ui/screen.h"
#include "font8x8_basic.h"

namespace pce {

namespace {
const Uint8 kPalette[C_COUNT][3] = {
  { 0x06, 0x06, 0x06 }, // deep black
  { 0xF0, 0x8A, 0x24 }, // PC Engine orange
  { 0xF2, 0xF2, 0xF2 }, // white
  { 0x55, 0x4A, 0x40 }, // dim
  { 0x24, 0x14, 0x06 }, // dark orange
  { 0x5C, 0x30, 0x0C }, // mid orange
  { 0xE0, 0x30, 0x20 }, // red
};
}

Screen::Screen() : r_(0), font_(0) {}

Screen::~Screen() {
  if (font_) SDL_DestroyTexture(font_);
}

bool Screen::init(SDL_Renderer* r) {
  r_ = r;
  // 128 glyphs in one row: white pixels, transparent background; colored with ColorMod.
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 128 * kCell, kCell, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return false;
  SDL_LockSurface(s);
  for (int g = 0; g < 128; ++g)
    for (int y = 0; y < 8; ++y) {
      Uint32* row = (Uint32*)((Uint8*)s->pixels + y * s->pitch);
      for (int x = 0; x < 8; ++x)
        row[g * 8 + x] = ((font8x8_basic[g][y] >> x) & 1) ? 0xFFFFFFFFu : 0x00000000u;
    }
  SDL_UnlockSurface(s);
  font_ = SDL_CreateTextureFromSurface(r_, s);
  SDL_FreeSurface(s);
  if (!font_) return false;
  SDL_SetTextureBlendMode(font_, SDL_BLENDMODE_BLEND);
  return true;
}

void Screen::setColor(Color c) {
  SDL_SetRenderDrawColor(r_, kPalette[c][0], kPalette[c][1], kPalette[c][2], 255);
}

void Screen::clear() {
  setColor(C_BLACK);
  SDL_RenderClear(r_);
}

void Screen::fillPx(int x, int y, int w, int h, Color c) {
  SDL_Rect rc = { x, y, w, h };
  setColor(c);
  SDL_RenderFillRect(r_, &rc);
}

void Screen::fill(int col, int row, int w, int h, Color c) {
  fillPx(col * kCell, row * kCell, w * kCell, h * kCell, c);
}

void Screen::frame(int col, int row, int w, int h, Color c) {
  // 1 px outside the cells: stays clear of text in the row above/below
  // (the 8x8 font leaves its last pixel row empty).
  SDL_Rect rc = { col * kCell - 1, row * kCell - 1, w * kCell + 2, h * kCell + 2 };
  setColor(c);
  SDL_RenderDrawRect(r_, &rc);
}

void Screen::glyph(int col, int row, unsigned char ch, Color fg) {
  if (ch == ' ' || ch >= 128) return;
  SDL_Rect src = { ch * kCell, 0, kCell, kCell };
  SDL_Rect dst = { col * kCell, row * kCell, kCell, kCell };
  SDL_SetTextureColorMod(font_, kPalette[fg][0], kPalette[fg][1], kPalette[fg][2]);
  SDL_RenderCopy(r_, font_, &src, &dst);
}

void Screen::text(int col, int row, const char* s, Color fg) {
  if (row < 0 || row >= kRows) return;
  SDL_SetTextureColorMod(font_, kPalette[fg][0], kPalette[fg][1], kPalette[fg][2]);
  for (; *s && col < kCols; ++s, ++col) {
    unsigned char ch = (unsigned char)*s;
    if (ch == ' ' || ch >= 128 || col < 0) continue;
    SDL_Rect src = { ch * kCell, 0, kCell, kCell };
    SDL_Rect dst = { col * kCell, row * kCell, kCell, kCell };
    SDL_RenderCopy(r_, font_, &src, &dst);
  }
}

void Screen::textBg(int col, int row, const char* s, Color fg, Color bg) {
  int n = 0;
  while (s[n]) ++n;
  fill(col, row, n, 1, bg);
  text(col, row, s, fg);
}

void Screen::present() { SDL_RenderPresent(r_); }

SDL_Texture* Screen::makeString(const char* s, Color fg) {
  int n = 0;
  while (s[n]) ++n;
  if (n == 0) return 0;
  SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, n * kCell, kCell, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!surf) return 0;
  const Uint32 on = 0xFF000000u | ((Uint32)kPalette[fg][0] << 16) | ((Uint32)kPalette[fg][1] << 8) | kPalette[fg][2];
  SDL_LockSurface(surf);
  for (int i = 0; i < n; ++i) {
    unsigned char ch = (unsigned char)s[i];
    for (int y = 0; y < kCell; ++y) {
      Uint32* row = (Uint32*)((Uint8*)surf->pixels + y * surf->pitch) + i * kCell;
      for (int x = 0; x < kCell; ++x)
        row[x] = (ch < 128 && ((font8x8_basic[ch][y] >> x) & 1)) ? on : 0x00000000u;
    }
  }
  SDL_UnlockSurface(surf);
  SDL_Texture* t = SDL_CreateTextureFromSurface(r_, surf);
  SDL_FreeSurface(surf);
  if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  return t;
}

void Screen::blit(SDL_Texture* t, int col, int row) {
  if (!t) return;
  int w = 0, h = 0;
  SDL_QueryTexture(t, 0, 0, &w, &h);
  SDL_Rect dst = { col * kCell, row * kCell, w, h };
  SDL_RenderCopy(r_, t, 0, &dst);
}

} // namespace pce
