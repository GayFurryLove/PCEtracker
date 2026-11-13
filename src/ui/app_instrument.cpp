// PCEtracker - instrument screen: parameters, wave editor, macro editor,
// DDA sample import, text entry.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "ui/app.h"
#include "audio/wav_reader.h"

#include <stdio.h>
#include <string.h>

namespace pce {

namespace {

// Layout of the instrument screen (cells).
const int kParamX = 1;
const int kParamTop = 17;          // first parameter row (NAME)
const int kMacroListTop = 25;      // first macro row
const int kWaveX = 46;             // wave box: 32 samples x 2 cells
const int kWaveTop = 17;           // value 31 row
const int kWaveH = 32;             // one row per 5-bit level
const int kMacroX = 46;            // macro graph: 64 steps x 1 cell
const int kMacroTop = 53;
const int kMacroH = 16;

const char* kMacroLabel[MACRO_COUNT] = { "VOLUME", "ARPEGGIO", "PITCH", "WAVE", "NOISE", "PAN" };
const char* kMacroUnit[MACRO_COUNT] = { "0-31 volume", "semitones", "period/tick", "wave number",
                                        "0 off, n = noise freq n-1", "$LR byte" };

std::string hex2(int v) {
  char b[4];
  snprintf(b, sizeof b, "%02X", v & 0xFF);
  return b;
}

struct Guard {
  SDL_AudioDeviceID d;
  explicit Guard(SDL_AudioDeviceID dev) : d(dev) { if (d) SDL_LockAudioDevice(d); }
  ~Guard() { if (d) SDL_UnlockAudioDevice(d); }
};

} // namespace

// --- text entry ---------------------------------------------------------------

void App::startTextEdit(int what) {
  textEdit_ = what;
  textBuf_ = what == TEXT_INST_NAME ? editor_.currentInstrument().name : song_.title;
  SDL_StartTextInput();
  message(what == TEXT_INST_NAME ? "Instrument name: type, ENTER = ok, ESC = cancel"
                                 : "Song title: type, ENTER = ok, ESC = cancel");
}

void App::handleTextInput(const char* text) {
  Guard g(audioDev_);
  for (const char* p = text; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (c >= 32 && c < 127 && c != '"' && textBuf_.size() < (textEdit_ == TEXT_INST_NAME ? 27u : 40u))
      textBuf_ += (char)c; // ASCII only: the pixel font has no other glyphs
  }
  redraw_ = true;
}

bool App::handleTextKey(const SDL_KeyboardEvent& k) {
  switch (k.keysym.scancode) {
    case SDL_SCANCODE_BACKSPACE:
      if (!textBuf_.empty()) textBuf_.erase(textBuf_.size() - 1);
      return true;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
      if (textEdit_ == TEXT_INST_NAME) editor_.setInstrumentName(textBuf_);
      else editor_.setSongTitle(textBuf_);
      // fall through
    case SDL_SCANCODE_ESCAPE:
      textEdit_ = TEXT_NONE;
      SDL_StopTextInput();
      message_.clear();
      return true;
    default:
      return true; // everything else is text
  }
}

// --- samples ------------------------------------------------------------------

void App::importSample(const char* path) {
  redraw_ = true;
  Sample smp;
  std::string err;
  if (!importWavSample(path, &smp, &err)) { message("IMPORT FAILED: " + err); return; }
  Guard g(audioDev_);
  int idx = editor_.addSample(smp);
  if (idx < 0) { message("Too many samples"); return; }
  view_ = VIEW_INSTRUMENT;
  char b[96];
  snprintf(b, sizeof b, "Sample %02X: %.24s, %u values (%.2f s at 6991 Hz, 5-bit) -> instrument %02X",
           idx, smp.name.c_str(), (unsigned)smp.data.size(), smp.data.size() / kDdaRateHz, editor_.instrument);
  message(b);
}

// --- keyboard -----------------------------------------------------------------

void App::jamInstrument(SDL_Scancode sc, bool repeat) {
  int note;
  if (repeat || !renderer_ || renderer_->player().playing() || !noteKey(sc, &note)) return;
  // Noise macros only work on ch5/ch6, so preview such instruments there.
  const Instrument& in = editor_.currentInstrument();
  int ch = in.macros[MACRO_NOISE].empty() ? editor_.channel() : 4;
  if (!in.macros[MACRO_NOISE].empty() && editor_.channel() >= 4) ch = editor_.channel();
  renderer_->player().jamNote(ch, (uint8_t)note, editor_.instrument);
  jamCh_ = ch;
  jamKey_ = sc;
}

bool App::handleInstrumentKey(const SDL_KeyboardEvent& k) {
  const SDL_Scancode sc = k.keysym.scancode;
  const Uint16 mod = k.keysym.mod;
  const bool ctrl = (mod & KMOD_CTRL) != 0, shift = (mod & KMOD_SHIFT) != 0, alt = (mod & KMOD_ALT) != 0;
  Instrument& in = editor_.currentInstrument();

  if (ctrl) {
    switch (sc) {
      case SDL_SCANCODE_N: editor_.newInstrument(); message("New instrument"); return true;
      case SDL_SCANCODE_D: editor_.cloneInstrument(); message("Instrument cloned"); return true;
      case SDL_SCANCODE_W: editor_.newWave(); message("New wave (copy) assigned"); return true;
      default: return false; // undo, save, mute ... stay global
    }
  }
  if (sc == SDL_SCANCODE_TAB) {
    focus_ = (focus_ + (shift ? FOCUS_COUNT - 1 : 1)) % FOCUS_COUNT;
    return true;
  }
  if (alt && focus_ == FOCUS_WAVE && sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
    editor_.applyWavePreset(sc - SDL_SCANCODE_1);
    return true;
  }
  if (alt && focus_ == FOCUS_MACRO) {
    switch (sc) {
      case SDL_SCANCODE_L: editor_.macroToggleLoop(macroKind_, macroCursor_); return true;
      case SDL_SCANCODE_R: editor_.macroToggleRelease(macroKind_, macroCursor_); return true;
      case SDL_SCANCODE_C: editor_.macroClear(macroKind_); macroCursor_ = 0; return true;
      default: break;
    }
  }
  if (alt) return false;

  const int big = shift ? 16 : 1;
  switch (focus_) {
    case FOCUS_PARAMS:
      switch (sc) {
        case SDL_SCANCODE_UP: paramRow_ = (paramRow_ + IF_COUNT) % (IF_COUNT + 1); return true;
        case SDL_SCANCODE_DOWN: paramRow_ = (paramRow_ + 1) % (IF_COUNT + 1); return true;
        case SDL_SCANCODE_LEFT: case SDL_SCANCODE_RIGHT:
          if (paramRow_ > 0) editor_.changeInstrumentField(paramRow_ - 1, sc == SDL_SCANCODE_LEFT ? -big : big);
          return true;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
          if (paramRow_ == 0) { startTextEdit(TEXT_INST_NAME); return true; }
          if (paramRow_ - 1 == IF_TYPE || paramRow_ - 1 == IF_PHASE) editor_.changeInstrumentField(paramRow_ - 1, 1);
          return true;
        default: break;
      }
      break;
    case FOCUS_WAVE: {
      const Wave& w = song_.waves[editor_.currentWaveIndex()];
      switch (sc) {
        case SDL_SCANCODE_LEFT: waveCursor_ = (waveCursor_ + kWaveLength - 1) % kWaveLength; return true;
        case SDL_SCANCODE_RIGHT: waveCursor_ = (waveCursor_ + 1) % kWaveLength; return true;
        case SDL_SCANCODE_UP: editor_.setWaveSample(waveCursor_, w.data[waveCursor_] + (shift ? 4 : 1)); return true;
        case SDL_SCANCODE_DOWN: editor_.setWaveSample(waveCursor_, w.data[waveCursor_] - (shift ? 4 : 1)); return true;
        case SDL_SCANCODE_PAGEUP: editor_.changeInstrumentField(IF_WAVE, -1); return true;
        case SDL_SCANCODE_PAGEDOWN: editor_.changeInstrumentField(IF_WAVE, 1); return true;
        default: break;
      }
      break;
    }
    case FOCUS_MACRO: {
      const Macro& m = in.macros[macroKind_];
      int cur = macroCursor_ < (int)m.values.size() ? m.values[macroCursor_]
                                                    : (m.values.empty() ? (macroKind_ == MACRO_VOLUME ? 31 : 0) : m.values.back());
      switch (sc) {
        case SDL_SCANCODE_LEFT: if (macroCursor_ > 0) --macroCursor_; return true;
        case SDL_SCANCODE_RIGHT: if (macroCursor_ < kMaxMacroLength - 1) ++macroCursor_; return true;
        case SDL_SCANCODE_HOME: macroCursor_ = 0; return true;
        case SDL_SCANCODE_END: macroCursor_ = m.values.empty() ? 0 : (int)m.values.size() - 1; return true;
        case SDL_SCANCODE_UP: editor_.macroSet(macroKind_, macroCursor_, cur + (shift ? 8 : 1)); return true;
        case SDL_SCANCODE_DOWN: editor_.macroSet(macroKind_, macroCursor_, cur - (shift ? 8 : 1)); return true;
        case SDL_SCANCODE_INSERT: editor_.macroInsert(macroKind_, macroCursor_); return true;
        case SDL_SCANCODE_DELETE: case SDL_SCANCODE_BACKSPACE:
          editor_.macroDelete(macroKind_, macroCursor_);
          if (macroCursor_ > 0 && macroCursor_ >= (int)in.macros[macroKind_].values.size()) --macroCursor_;
          return true;
        case SDL_SCANCODE_PAGEUP: macroKind_ = (macroKind_ + MACRO_COUNT - 1) % MACRO_COUNT; return true;
        case SDL_SCANCODE_PAGEDOWN: macroKind_ = (macroKind_ + 1) % MACRO_COUNT; return true;
        default: break;
      }
      break;
    }
    default: break;
  }
  // Note keys preview the instrument from any focus.
  int note;
  if (noteKey(sc, &note)) { jamInstrument(sc, k.repeat != 0); return true; }
  return false; // ENTER/SPACE/F-keys etc. keep their global meaning
}

// --- mouse --------------------------------------------------------------------

void App::handleInstrumentMouse(const SDL_Event& e) {
  if (e.type == SDL_MOUSEWHEEL) {
    if (focus_ == FOCUS_MACRO) macroKind_ = (macroKind_ + (e.wheel.y > 0 ? MACRO_COUNT - 1 : 1)) % MACRO_COUNT;
    return;
  }
  int px = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x;
  int py = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
  int cx = px / kCell, cy = py / kCell;

  // Wave box: x picks the sample, y the 5-bit value. Drag to draw.
  if ((drag_ == 1) || (drag_ == 0 && cx >= kWaveX && cx < kWaveX + 64 && cy >= kWaveTop && cy < kWaveTop + kWaveH)) {
    int i = (px / kCell - kWaveX) / 2;
    int v = kWaveMax - (py / kCell - kWaveTop);
    if (i < 0) i = 0;
    if (i >= kWaveLength) i = kWaveLength - 1;
    focus_ = FOCUS_WAVE;
    waveCursor_ = i;
    editor_.setWaveSample(i, v, 1);
    drag_ = 1;
    return;
  }
  // Macro graph.
  if ((drag_ == 2) || (drag_ == 0 && cx >= kMacroX && cx < kMacroX + kMaxMacroLength && cy >= kMacroTop && cy < kMacroTop + kMacroH)) {
    int i = cx - kMacroX;
    if (i < 0) i = 0;
    if (i >= kMaxMacroLength) i = kMaxMacroLength - 1;
    int lo, hi;
    Editor::macroRange(macroKind_, &lo, &hi);
    if (macroKind_ == MACRO_PAN) { lo = 0; hi = 255; }
    int rowFromBottom = (kMacroTop + kMacroH - 1) - cy;
    if (rowFromBottom < 0) rowFromBottom = 0;
    if (rowFromBottom > kMacroH - 1) rowFromBottom = kMacroH - 1;
    int v = lo + (rowFromBottom * (hi - lo) + (kMacroH - 1) / 2) / (kMacroH - 1);
    focus_ = FOCUS_MACRO;
    macroCursor_ = i;
    editor_.macroSet(macroKind_, i, macroKind_ == MACRO_PAN ? (int)(int8_t)(uint8_t)v : v, 2);
    drag_ = 2;
    return;
  }
  if (e.type != SDL_MOUSEBUTTONDOWN) return;
  // Parameter rows and macro list.
  if (cx >= kParamX && cx < 40) {
    if (cy >= kParamTop && cy <= kParamTop + IF_COUNT) {
      focus_ = FOCUS_PARAMS;
      paramRow_ = cy - kParamTop;
    } else if (cy >= kMacroListTop && cy < kMacroListTop + MACRO_COUNT) {
      focus_ = FOCUS_MACRO;
      macroKind_ = cy - kMacroListTop;
    }
  }
}

// --- drawing ------------------------------------------------------------------

void App::drawInstrumentView(int top, int bottom) {
  (void)bottom;
  Instrument& in = editor_.currentInstrument();
  char b[128];

  screen_.fill(0, top, kCols, 1, C_ORANGE);
  snprintf(b, sizeof b, "INSTRUMENT %02X  %s", editor_.instrument, in.name.c_str());
  screen_.text(1, top, b, C_BLACK);
  const char* hint = "TAB FOCUS  F9 PATTERN  CTRL+N NEW  CTRL+D CLONE";
  screen_.text(kCols - 1 - (int)strlen(hint), top, hint, C_BLACK);

  // --- parameters ---
  const bool dda = in.sample >= 0;
  std::string rows[IF_COUNT + 1];
  rows[0] = textEdit_ == TEXT_INST_NAME ? textBuf_ + "_" : in.name;
  rows[1 + IF_TYPE] = dda ? "DDA SAMPLE" : "WAVETABLE";
  snprintf(b, sizeof b, "%02X of %02X%s", in.wave, (int)song_.waves.size(), dda ? "  (unused)" : "");
  rows[1 + IF_WAVE] = b;
  if (dda && in.sample < (int)song_.samples.size()) {
    const Sample& s = song_.samples[in.sample];
    snprintf(b, sizeof b, "%02X %.14s %.2fs", in.sample, s.name.c_str(), s.data.size() / kDdaRateHz);
  } else {
    snprintf(b, sizeof b, song_.samples.empty() ? "-- (drop a .wav file)" : "-- (%d loaded)", (int)song_.samples.size());
  }
  rows[1 + IF_SAMPLE] = b;
  rows[1 + IF_VOLUME] = hex2(in.volume);
  rows[1 + IF_PHASE] = in.resetPhase ? "RESET ON NOTE" : "FREE RUNNING";
  static const char* labels[IF_COUNT + 1] = { "NAME", "TYPE", "WAVE", "SAMPLE", "VOLUME", "PHASE" };
  screen_.text(kParamX, kParamTop - 1, "PARAMETERS", focus_ == FOCUS_PARAMS ? C_ORANGE : C_DIM);
  for (int i = 0; i <= IF_COUNT; ++i) {
    int y = kParamTop + i;
    bool sel = focus_ == FOCUS_PARAMS && paramRow_ == i;
    if (sel) screen_.fill(kParamX, y, 38, 1, C_ORANGE_MID);
    screen_.text(kParamX + 1, y, labels[i], C_ORANGE);
    screen_.text(kParamX + 9, y, rows[i].c_str(), C_WHITE);
  }

  // --- macro list ---
  screen_.text(kParamX, kMacroListTop - 1, "MACROS (one value per 60 Hz tick)", focus_ == FOCUS_MACRO ? C_ORANGE : C_DIM);
  for (int k = 0; k < MACRO_COUNT; ++k) {
    const Macro& m = in.macros[k];
    int y = kMacroListTop + k;
    if (k == macroKind_) screen_.fill(kParamX, y, 38, 1, focus_ == FOCUS_MACRO ? C_ORANGE_MID : C_ORANGE_DK);
    screen_.text(kParamX + 1, y, kMacroLabel[k], C_ORANGE);
    if (m.empty()) {
      screen_.text(kParamX + 11, y, "off", C_DIM);
    } else {
      snprintf(b, sizeof b, "len %2d  loop %s  rel %s", (int)m.values.size(),
               m.loop >= 0 ? hex2(m.loop).c_str() : "--", m.release >= 0 ? hex2(m.release).c_str() : "--");
      screen_.text(kParamX + 11, y, b, C_WHITE);
    }
  }

  // --- hardware notes ---
  static const char* notes[] = {
    "HARDWARE",
    "wave RAM: 32 samples x 5 bits per channel",
    "new wave on a playing channel = click",
    "volume 0-31, ~1.5 dB per step (log)",
    "noise: channels 5 and 6 only",
    "DDA: 5-bit, ~6991 Hz timer, any channel",
    "LFO: ch2 modulates ch1 (L/M effects)",
    "",
    "DROP A .WAV ON THE WINDOW",
    "to import it as a DDA sample",
    "",
    "WAVE  ALT+1..9: sine square saw tri",
    "      noise clear invert shift half",
    "      PGUP/PGDN wave  CTRL+W new wave",
    "MACRO INS/DEL step  ALT+L loop",
    "      ALT+R release  ALT+C clear",
    "      PGUP/PGDN macro  mouse: draw",
    "NOTE KEYS preview the instrument",
  };
  for (int i = 0; i < (int)(sizeof notes / sizeof notes[0]); ++i)
    screen_.text(kParamX, 33 + i, notes[i], i == 0 || i == 8 ? C_ORANGE : (i < 8 ? C_WHITE : C_DIM));

  // --- wave editor ---
  const int wi = editor_.currentWaveIndex();
  const Wave& w = song_.waves[wi];
  snprintf(b, sizeof b, "WAVE %02X  32 x 5-BIT%s", wi, dda ? "  (instrument plays a DDA sample)" : "");
  screen_.text(kWaveX, kWaveTop - 1, b, focus_ == FOCUS_WAVE ? C_ORANGE : C_DIM);
  screen_.fill(kWaveX, kWaveTop, 64, kWaveH, C_BLACK);
  screen_.frame(kWaveX, kWaveTop, 64, kWaveH, focus_ == FOCUS_WAVE ? C_ORANGE : C_ORANGE_MID);
  screen_.fillPx(kWaveX * kCell, (kWaveTop + 15) * kCell + kCell / 2, 64 * kCell, 1, C_ORANGE_DK); // center line
  if (focus_ == FOCUS_WAVE) screen_.fill(kWaveX + waveCursor_ * 2, kWaveTop, 2, kWaveH, C_ORANGE_DK);
  for (int i = 0; i < kWaveLength; ++i) {
    int v = w.data[i];
    int y = kWaveTop + (kWaveMax - v);
    screen_.fillPx((kWaveX + i * 2) * kCell + 1, y * kCell, 2 * kCell - 2, (kWaveTop + kWaveH - y) * kCell,
                   i == waveCursor_ && focus_ == FOCUS_WAVE ? C_WHITE : C_ORANGE);
  }
  screen_.text(kWaveX - 3, kWaveTop, "1F", C_DIM);
  screen_.text(kWaveX - 3, kWaveTop + kWaveH - 1, "00", C_DIM);
  snprintf(b, sizeof b, "POS %02X  VALUE %02X", waveCursor_, w.data[waveCursor_]);
  screen_.text(kWaveX + 64 - (int)strlen(b), kWaveTop + kWaveH + 1, b, C_WHITE);

  // --- macro editor ---
  const Macro& m = in.macros[macroKind_];
  snprintf(b, sizeof b, "MACRO %s  (%s)", kMacroLabel[macroKind_], kMacroUnit[macroKind_]);
  screen_.text(kMacroX, kMacroTop - 2, b, focus_ == FOCUS_MACRO ? C_ORANGE : C_DIM);
  screen_.fill(kMacroX, kMacroTop, kMaxMacroLength, kMacroH, C_BLACK);
  screen_.frame(kMacroX, kMacroTop, kMaxMacroLength, kMacroH, focus_ == FOCUS_MACRO ? C_ORANGE : C_ORANGE_MID);
  int lo, hi;
  Editor::macroRange(macroKind_, &lo, &hi);
  if (macroKind_ == MACRO_PAN) { lo = 0; hi = 255; }
  const int hPx = kMacroH * kCell;
  const int baseVal = (lo < 0 && hi > 0) ? 0 : lo;
  const int baseY = kMacroTop * kCell + hPx - 1 - (int)((long)(baseVal - lo) * (hPx - 1) / (hi - lo));
  if (focus_ == FOCUS_MACRO) screen_.fill(kMacroX + macroCursor_, kMacroTop, 1, kMacroH, C_ORANGE_DK);
  if (baseVal != lo) screen_.fillPx(kMacroX * kCell, baseY, kMaxMacroLength * kCell, 1, C_ORANGE_DK);
  for (int i = 0; i < (int)m.values.size(); ++i) {
    int v = macroKind_ == MACRO_PAN ? (int)(uint8_t)m.values[i] : m.values[i];
    int y = kMacroTop * kCell + hPx - 1 - (int)((long)(v - lo) * (hPx - 1) / (hi - lo));
    int y0 = y < baseY ? y : baseY, y1 = y < baseY ? baseY : y;
    Color c = (i == macroCursor_ && focus_ == FOCUS_MACRO) ? C_WHITE
            : (m.loop >= 0 && i >= m.loop && (m.release < 0 || i <= m.release)) ? C_ORANGE : C_ORANGE_MID;
    if (m.release >= 0 && m.loop >= 0 && m.loop > m.release && i >= m.loop) c = C_ORANGE;
    screen_.fillPx((kMacroX + i) * kCell + 1, y0, kCell - 2, y1 - y0 + 1, c);
  }
  // Loop / release markers under the graph.
  for (int i = 0; i < (int)m.values.size(); ++i) {
    if (i == m.loop) screen_.glyph(kMacroX + i, kMacroTop + kMacroH + 1, 'L', C_ORANGE);
    if (i == m.release) screen_.glyph(kMacroX + i, kMacroTop + kMacroH + 2, 'R', C_WHITE);
  }
  int cv = macroCursor_ < (int)m.values.size()
             ? (macroKind_ == MACRO_PAN ? (int)(uint8_t)m.values[macroCursor_] : m.values[macroCursor_]) : 0;
  if (macroCursor_ < (int)m.values.size()) snprintf(b, sizeof b, "STEP %02X  VALUE %d", macroCursor_, cv);
  else snprintf(b, sizeof b, "STEP %02X  (past the end)", macroCursor_);
  screen_.text(kMacroX + kMaxMacroLength - (int)strlen(b), kMacroTop + kMacroH + 1, b, C_WHITE);
  snprintf(b, sizeof b, "%d", hi);
  screen_.text(kMacroX - 1 - (int)strlen(b), kMacroTop, b, C_DIM);
  snprintf(b, sizeof b, "%d", lo);
  screen_.text(kMacroX - 1 - (int)strlen(b), kMacroTop + kMacroH - 1, b, C_DIM);
}

} // namespace pce
