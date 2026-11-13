// PCEtracker - application.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "ui/app.h"
#include "project/text_format.h"

#include <stdio.h>
#include <string.h>

namespace pce {

namespace {

// Layout (in 8x8 cells, 128 x 75).
const int kTopRow = 2;          // panels
const int kPanelRows = 11;
const int kPatHeader = 15;      // pattern channel header row
const int kPatTop = 16;         // first pattern row on screen
const int kPatBottom = 73;      // last pattern row on screen
const int kChanX0 = 4;          // first channel column
const int kChanW = 20;          // cells per channel (17 used + gap)

// Column offsets inside "C-4 00 1F 422 ..." for each EditColumn.
const int kColOff[COL_COUNT] = { 0, 4, 5, 7, 8, 10, 11, 12, 14, 15, 16 };
const int kColW[COL_COUNT]   = { 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

struct Guard {
  SDL_AudioDeviceID d;
  explicit Guard(SDL_AudioDeviceID dev) : d(dev) { if (d) SDL_LockAudioDevice(d); }
  ~Guard() { if (d) SDL_UnlockAudioDevice(d); }
};

std::string hex2(int v) {
  char b[4];
  snprintf(b, sizeof b, "%02X", v & 0xFF);
  return b;
}

} // namespace

App::App()
  : editor_(song_), renderer_(0), window_(0), sdlRenderer_(0), audioDev_(0), fifoHead_(0),
    messageTime_(0), showHelp_(false), quit_(false), quitArmed_(false), redraw_(true),
    followPlay_(true), jamCh_(-1), jamKey_(SDL_SCANCODE_UNKNOWN), playing_(false),
    playOrder_(0), playRow_(0) {
  for (int c = 0; c < kChannels; ++c) { muted_[c] = false; views_[c] = Player::ChannelView(); }
}

App::~App() {
  if (audioDev_) SDL_CloseAudioDevice(audioDev_);
  delete renderer_;
  if (sdlRenderer_) SDL_DestroyRenderer(sdlRenderer_);
  if (window_) SDL_DestroyWindow(window_);
  SDL_Quit();
}

// --- setup ------------------------------------------------------------------

bool App::initVideo() {
  int w = kScreenW * cfg_.scale, h = kScreenH * cfg_.scale;
  Uint32 flags = cfg_.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
  window_ = SDL_CreateWindow("PCEtracker", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
  if (!window_) return false;
  Uint32 rflags = cfg_.softwareRender ? SDL_RENDERER_SOFTWARE : 0;
  if (cfg_.vsync && !cfg_.softwareRender) rflags |= SDL_RENDERER_PRESENTVSYNC;
  sdlRenderer_ = SDL_CreateRenderer(window_, -1, rflags);
  if (!sdlRenderer_) sdlRenderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
  if (!sdlRenderer_) return false;
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  SDL_RenderSetLogicalSize(sdlRenderer_, kScreenW, kScreenH);
  SDL_RenderSetIntegerScale(sdlRenderer_, SDL_TRUE);
  return screen_.init(sdlRenderer_);
}

bool App::initAudio() {
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = cfg_.audioRate;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = (Uint16)cfg_.audioBuffer;
  want.callback = &App::audioCallback;
  want.userdata = this;
  audioDev_ = SDL_OpenAudioDevice(0, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (!audioDev_) return false;
  renderer_ = new SongRenderer(song_, cfg_.revisionA ? PSG_REV_HUC6280A : PSG_REV_HUC6280, have.freq);
  fifo_.reserve((size_t)have.freq); // 0.5 s stereo, never reallocated in practice
  SDL_PauseAudioDevice(audioDev_, 0);
  return true;
}

void App::audioCallback(void* user, Uint8* stream, int len) {
  static_cast<App*>(user)->fillAudio((int16_t*)stream, len / 4);
}

// Runs on SDL's audio thread with the device lock held.
void App::fillAudio(int16_t* out, int frames) {
  size_t need = (size_t)frames * 2;
  while (fifo_.size() - fifoHead_ < need) {
    if (fifoHead_ > 0) {
      fifo_.erase(fifo_.begin(), fifo_.begin() + (std::ptrdiff_t)fifoHead_);
      fifoHead_ = 0;
    }
    renderer_->renderTick(fifo_);
  }
  memcpy(out, &fifo_[fifoHead_], need * sizeof(int16_t));
  fifoHead_ += need;
}

int App::run(int argc, char** argv) {
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  std::string cfgPath = Config::defaultPath();
  cfg_.load(cfgPath);

  // Options: [song.pcet] [--screenshot out.bmp] [--edit] [--help-screen]
  const char* shot = 0;
  std::vector<const char*> args;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
    else if (!strcmp(argv[i], "--edit")) editor_.editMode = true;
    else if (!strcmp(argv[i], "--help-screen")) showHelp_ = true;
    else args.push_back(argv[i]);
  }
  if (!args.empty()) {
    path_ = args[0];
    std::string err;
    FILE* f = fopen(path_.c_str(), "rb");
    if (f) {
      fclose(f);
      if (!loadSongFile(path_.c_str(), &song_, &err)) {
        fprintf(stderr, "%s\n", err.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PCEtracker", err.c_str(), 0);
        return 1;
      }
      message("Loaded " + path_);
    } else {
      message("New song: " + path_);
    }
  } else {
    path_ = "untitled.pcet";
    message("New song. F12 = keys help");
  }
  editor_.setOrder(0);

  if (!initVideo()) {
    fprintf(stderr, "video init failed: %s\n", SDL_GetError());
    return 1;
  }
  if (shot) {
    // Render one frame to a BMP and exit (documentation, UI checks without a display).
    draw();
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, kScreenW * cfg_.scale, kScreenH * cfg_.scale, 32,
                                                    SDL_PIXELFORMAT_ARGB8888);
    bool ok = s && SDL_RenderReadPixels(sdlRenderer_, 0, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0 &&
              SDL_SaveBMP(s, shot) == 0;
    if (s) SDL_FreeSurface(s);
    if (!ok) fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
    return ok ? 0 : 1;
  }
  if (!initAudio()) {
    fprintf(stderr, "audio init failed: %s\n", SDL_GetError());
    message(std::string("NO AUDIO: ") + SDL_GetError());
  }

  while (!quit_) {
    SDL_Event e;
    // Sleep until input or the next display refresh (~30 fps while playing).
    if (SDL_WaitEventTimeout(&e, playing_ || editor_.editMode ? 33 : 100)) {
      do {
        switch (e.type) {
          case SDL_QUIT:
            if (editor_.dirty() && !quitArmed_) { quitArmed_ = true; message("Unsaved changes! Close again to quit, Ctrl+S to save"); }
            else quit_ = true;
            break;
          case SDL_KEYDOWN: handleKey(e.key); break;
          case SDL_KEYUP: handleKeyUp(e.key); break;
          case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEWHEEL: handleMouse(e); break;
          case SDL_WINDOWEVENT: redraw_ = true; break;
          default: break;
        }
      } while (SDL_PollEvent(&e));
    }

    {
      Guard g(audioDev_);
      if (renderer_) {
        Player& p = renderer_->player();
        bool was = playing_;
        playing_ = p.playing();
        playOrder_ = p.order();
        playRow_ = p.row();
        for (int c = 0; c < kChannels; ++c) views_[c] = p.view(c);
        if (playing_ || was) redraw_ = true;
        if (playing_ && followPlay_ && !editor_.editMode) {
          editor_.setOrder(playOrder_);
          editor_.setRow(playRow_);
        }
      }
    }
    if (!message_.empty() && SDL_GetTicks() - messageTime_ > 4000) { message_.clear(); redraw_ = true; }
    if (redraw_) {
      draw();
      redraw_ = false;
    }
  }
  return 0;
}

// --- actions ----------------------------------------------------------------

void App::message(const std::string& m) {
  message_ = m;
  messageTime_ = SDL_GetTicks();
  redraw_ = true;
}

void App::playSong(bool fromCursor) {
  if (!renderer_) return;
  Guard g(audioDev_);
  renderer_->player().playSong(editor_.order(), fromCursor ? editor_.row() : 0);
  for (int c = 0; c < kChannels; ++c) renderer_->player().setMute(c, muted_[c]);
}

void App::playPattern() {
  if (!renderer_) return;
  Guard g(audioDev_);
  renderer_->player().playPattern(editor_.patternIndex(), 0);
  for (int c = 0; c < kChannels; ++c) renderer_->player().setMute(c, muted_[c]);
}

void App::stopPlayback() {
  if (!renderer_) return;
  Guard g(audioDev_);
  renderer_->player().stop();
}

void App::save() {
  std::string err;
  bool ok;
  {
    Guard g(audioDev_);
    ok = saveSongFile(path_.c_str(), song_, &err);
  }
  if (ok) { editor_.markSaved(); quitArmed_ = false; message("Saved " + path_); }
  else message("SAVE FAILED: " + err);
}

void App::exportWav() {
  std::string out = path_;
  size_t dot = out.rfind('.');
  if (dot != std::string::npos && out.find_first_of("/\\", dot) == std::string::npos) out.erase(dot);
  out += ".wav";
  RenderOptions opt;
  opt.revision = cfg_.revisionA ? PSG_REV_HUC6280A : PSG_REV_HUC6280;
  opt.sampleRate = 44100;
  std::string err;
  double sec = 0;
  bool ok;
  {
    Guard g(audioDev_);
    ok = renderSongToWav(song_, opt, out.c_str(), &err, &sec);
  }
  char b[64];
  snprintf(b, sizeof b, " (%.1f s)", sec);
  message(ok ? "Exported " + out + b : "EXPORT FAILED: " + err);
}

// --- keyboard ---------------------------------------------------------------

bool App::noteKey(SDL_Scancode sc, int* note) const {
  // Physical key positions (layout independent): two piano rows like ProTracker.
  static const SDL_Scancode lower[] = {
    SDL_SCANCODE_Z, SDL_SCANCODE_S, SDL_SCANCODE_X, SDL_SCANCODE_D, SDL_SCANCODE_C, SDL_SCANCODE_V,
    SDL_SCANCODE_G, SDL_SCANCODE_B, SDL_SCANCODE_H, SDL_SCANCODE_N, SDL_SCANCODE_J, SDL_SCANCODE_M,
    SDL_SCANCODE_COMMA, SDL_SCANCODE_L, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_SLASH };
  static const SDL_Scancode upper[] = {
    SDL_SCANCODE_Q, SDL_SCANCODE_2, SDL_SCANCODE_W, SDL_SCANCODE_3, SDL_SCANCODE_E, SDL_SCANCODE_R,
    SDL_SCANCODE_5, SDL_SCANCODE_T, SDL_SCANCODE_6, SDL_SCANCODE_Y, SDL_SCANCODE_7, SDL_SCANCODE_U,
    SDL_SCANCODE_I, SDL_SCANCODE_9, SDL_SCANCODE_O, SDL_SCANCODE_0, SDL_SCANCODE_P };
  for (int i = 0; i < (int)(sizeof lower / sizeof lower[0]); ++i)
    if (lower[i] == sc) { *note = editor_.octave * 12 + i; return *note < kNoteCount; }
  for (int i = 0; i < (int)(sizeof upper / sizeof upper[0]); ++i)
    if (upper[i] == sc) { *note = editor_.octave * 12 + 12 + i; return *note < kNoteCount; }
  return false;
}

int App::hexKey(SDL_Scancode sc) const {
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return 1 + (sc - SDL_SCANCODE_1);
  if (sc == SDL_SCANCODE_0) return 0;
  if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return 1 + (sc - SDL_SCANCODE_KP_1);
  if (sc == SDL_SCANCODE_KP_0) return 0;
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_F) return 10 + (sc - SDL_SCANCODE_A);
  return -1;
}

char App::commandKey(SDL_Scancode sc) const {
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return (char)('A' + (sc - SDL_SCANCODE_A));
  int h = hexKey(sc);
  if (h >= 0 && h <= 9) return (char)('0' + h);
  return 0;
}

void App::handleKeyUp(const SDL_KeyboardEvent& k) {
  if (jamCh_ >= 0 && k.keysym.scancode == jamKey_ && renderer_) {
    Guard g(audioDev_);
    renderer_->player().jamRelease(jamCh_);
    jamCh_ = -1;
  }
}

void App::handleKey(const SDL_KeyboardEvent& k) {
  const SDL_Scancode sc = k.keysym.scancode;
  const Uint16 mod = k.keysym.mod;
  const bool ctrl = (mod & KMOD_CTRL) != 0, shift = (mod & KMOD_SHIFT) != 0, alt = (mod & KMOD_ALT) != 0;
  redraw_ = true;
  if (sc != SDL_SCANCODE_Q || !ctrl) quitArmed_ = false; // any other key cancels "quit?"

  if (showHelp_) {
    if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_F12) showHelp_ = false;
    return;
  }

  Guard g(audioDev_); // every edit below changes data the audio thread reads

  // --- global keys ---
  if (ctrl) {
    switch (sc) {
      case SDL_SCANCODE_Q:
        if (editor_.dirty() && !quitArmed_) { quitArmed_ = true; message("Unsaved changes! Ctrl+Q again to quit, Ctrl+S to save"); }
        else quit_ = true;
        return;
      case SDL_SCANCODE_S: save(); return;
      case SDL_SCANCODE_E: exportWav(); return;
      case SDL_SCANCODE_Z: if (shift ? editor_.redo() : editor_.undo()) message(shift ? "Redo" : "Undo"); return;
      case SDL_SCANCODE_Y: if (editor_.redo()) message("Redo"); return;
      case SDL_SCANCODE_C: editor_.copy(); message("Copied"); return;
      case SDL_SCANCODE_X: editor_.cut(); return;
      case SDL_SCANCODE_V: editor_.paste(); return;
      case SDL_SCANCODE_A: editor_.selectChannel(); return;
      case SDL_SCANCODE_N: editor_.newPatternAtOrder(); return;
      case SDL_SCANCODE_D: editor_.clonePatternAtOrder(); return;
      case SDL_SCANCODE_INSERT: editor_.insertOrder(); return;
      case SDL_SCANCODE_DELETE: editor_.deleteOrder(); return;
      case SDL_SCANCODE_LEFT: editor_.setOrder(editor_.order() - 1); return;
      case SDL_SCANCODE_RIGHT: editor_.setOrder(editor_.order() + 1); return;
      case SDL_SCANCODE_PAGEUP: editor_.changeOrderPattern(1); return;
      case SDL_SCANCODE_PAGEDOWN: editor_.changeOrderPattern(-1); return;
      case SDL_SCANCODE_UP: editor_.transpose(shift ? 12 : 1); return;
      case SDL_SCANCODE_DOWN: editor_.transpose(shift ? -12 : -1); return;
      case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
        playSong(true); return;
      default: break;
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_6) {
      int c = sc - SDL_SCANCODE_1;
      if (shift) {
        bool soloed = !muted_[c];
        for (int i = 0; i < kChannels; ++i) if (i != c && !muted_[i]) soloed = false;
        for (int i = 0; i < kChannels; ++i) muted_[i] = soloed ? false : (i != c);
      } else {
        muted_[c] = !muted_[c];
      }
      if (renderer_) for (int i = 0; i < kChannels; ++i) renderer_->player().setMute(i, muted_[i]);
      return;
    }
    return;
  }
  if (alt) {
    switch (sc) {
      case SDL_SCANCODE_UP: editor_.changePatternLength(-1); return;
      case SDL_SCANCODE_DOWN: editor_.changePatternLength(1); return;
      case SDL_SCANCODE_PAGEUP: editor_.changePatternLength(-16); return;
      case SDL_SCANCODE_PAGEDOWN: editor_.changePatternLength(16); return;
      case SDL_SCANCODE_LEFT: editor_.changeSpeed(-1); return;
      case SDL_SCANCODE_RIGHT: editor_.changeSpeed(1); return;
      default: return;
    }
  }

  switch (sc) {
    case SDL_SCANCODE_ESCAPE:
      if (editor_.hasSelection()) editor_.clearSelection();
      else if (renderer_) renderer_->player().stop();
      return;
    case SDL_SCANCODE_SPACE:
      editor_.editMode = !editor_.editMode;
      return;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
      if (renderer_ && renderer_->player().playing()) renderer_->player().stop();
      else if (shift) playPattern();
      else playSong(false);
      return;
    case SDL_SCANCODE_F1: if (editor_.octave > 0) --editor_.octave; return;
    case SDL_SCANCODE_F2: if (editor_.octave < 7) ++editor_.octave; return;
    case SDL_SCANCODE_F3: if (editor_.instrument > 0) --editor_.instrument; return;
    case SDL_SCANCODE_F4: if (editor_.instrument + 1 < (int)song_.instruments.size()) ++editor_.instrument; return;
    case SDL_SCANCODE_F5: if (editor_.editStep > 0) --editor_.editStep; return;
    case SDL_SCANCODE_F6: if (editor_.editStep < 16) ++editor_.editStep; return;
    case SDL_SCANCODE_F12: showHelp_ = true; return;
    default: break;
  }

  // --- navigation (Shift extends the block) ---
  bool moved = true;
  const int rows = editor_.pattern().rows;
  if (shift && !editor_.hasSelection() &&
      (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT ||
       sc == SDL_SCANCODE_PAGEUP || sc == SDL_SCANCODE_PAGEDOWN || sc == SDL_SCANCODE_HOME || sc == SDL_SCANCODE_END))
    editor_.startSelection();
  switch (sc) {
    case SDL_SCANCODE_UP: editor_.moveRow(-1); break;
    case SDL_SCANCODE_DOWN: editor_.moveRow(1); break;
    case SDL_SCANCODE_LEFT: if (shift) editor_.moveChannel(-1); else editor_.moveColumn(-1); break;
    case SDL_SCANCODE_RIGHT: if (shift) editor_.moveChannel(1); else editor_.moveColumn(1); break;
    case SDL_SCANCODE_PAGEUP: editor_.setRow(editor_.row() - 16); break;
    case SDL_SCANCODE_PAGEDOWN: editor_.setRow(editor_.row() + 16); break;
    case SDL_SCANCODE_HOME: editor_.setRow(0); break;
    case SDL_SCANCODE_END: editor_.setRow(rows - 1); break;
    case SDL_SCANCODE_TAB:
      editor_.setCursor(editor_.row(), (editor_.channel() + (shift ? kChannels - 1 : 1)) % kChannels, COL_NOTE);
      break;
    default: moved = false; break;
  }
  if (moved) {
    if (shift && sc != SDL_SCANCODE_TAB) editor_.extendSelection();
    else if (sc != SDL_SCANCODE_TAB) editor_.clearSelection();
    return;
  }

  // --- editing ---
  switch (sc) {
    case SDL_SCANCODE_DELETE: editor_.clearAtCursor(); return;
    case SDL_SCANCODE_BACKSPACE: editor_.deleteRow(); return;
    case SDL_SCANCODE_INSERT: editor_.insertRow(); return;
    default: break;
  }

  const int col = editor_.column();
  if (col == COL_NOTE) {
    int note;
    if (sc == SDL_SCANCODE_1 || sc == SDL_SCANCODE_GRAVE) { editor_.enterNote(NOTE_OFF); return; }
    if (noteKey(sc, &note)) {
      if (!k.repeat && renderer_ && !renderer_->player().playing()) {
        renderer_->player().jamNote(editor_.channel(), (uint8_t)note, editor_.instrument);
        jamCh_ = editor_.channel();
        jamKey_ = sc;
      }
      editor_.enterNote(note);
    }
    return;
  }
  if (col == COL_FX1_CMD || col == COL_FX2_CMD) {
    char c = commandKey(sc);
    if (c) editor_.enterEffectCommand(c);
    return;
  }
  int h = hexKey(sc);
  if (h >= 0) editor_.enterHexDigit(h);
}

void App::handleMouse(const SDL_Event& e) {
  redraw_ = true;
  Guard g(audioDev_);
  if (e.type == SDL_MOUSEWHEEL) {
    editor_.moveRow(e.wheel.y > 0 ? -4 : 4);
    return;
  }
  int cx = e.button.x / kCell, cy = e.button.y / kCell;
  if (showHelp_) { showHelp_ = false; return; }
  // Pattern area
  if (cy >= kPatTop && cy <= kPatBottom && cx >= kChanX0) {
    int ch = (cx - kChanX0) / kChanW, off = (cx - kChanX0) % kChanW;
    if (ch >= kChannels) return;
    int col = -1;
    for (int c = 0; c < COL_COUNT; ++c) if (off >= kColOff[c] && off < kColOff[c] + kColW[c]) col = c;
    if (col < 0) col = COL_NOTE;
    int center = (kPatTop + kPatBottom) / 2;
    int row = editor_.row() + (cy - center);
    if (row >= 0 && row < editor_.pattern().rows) {
      editor_.clearSelection();
      editor_.setCursor(row, ch, col);
    }
    return;
  }
  // Order list
  if (cx >= 1 && cx < 13 && cy >= kTopRow + 1 && cy < kTopRow + kPanelRows) {
    int center = kTopRow + 1 + (kPanelRows - 1) / 2;
    editor_.setOrder(editor_.order() + (cy - center));
    return;
  }
  // Instrument list
  if (cx >= 15 && cx < 48 && cy >= kTopRow + 1 && cy < kTopRow + kPanelRows) {
    int center = kTopRow + 1 + (kPanelRows - 1) / 2;
    int i = editor_.instrument + (cy - center);
    if (i >= 0 && i < (int)song_.instruments.size()) editor_.instrument = i;
  }
}

// --- drawing ----------------------------------------------------------------

void App::draw() {
  screen_.clear();
  drawHeader();
  drawOrders(1, kTopRow, kPanelRows);
  drawInstruments(15, kTopRow, kPanelRows);
  drawSongInfo(50, kTopRow);
  drawChannels(80, kTopRow);
  drawPattern(kPatTop, kPatBottom);
  drawStatus();
  if (showHelp_) drawHelp();
  screen_.present();
}

void App::drawHeader() {
  screen_.fill(0, 0, kCols, 1, C_ORANGE);
  screen_.text(1, 0, "PCETRACKER", C_BLACK);
  std::string name = path_ + (editor_.dirty() ? " *" : "");
  screen_.text(13, 0, name.c_str(), C_BLACK);
  char b[64];
  snprintf(b, sizeof b, "%s  %d HZ", cfg_.revisionA ? "HUC6280A" : "HUC6280", cfg_.audioRate);
  screen_.text(kCols - 1 - (int)strlen(b), 0, b, C_BLACK);
}

void App::drawOrders(int x, int y, int h) {
  screen_.textBg(x, y, " ORDER      ", C_BLACK, C_ORANGE);
  int n = (int)song_.orders.size();
  int center = (h - 1) / 2;
  for (int i = 0; i < h - 1; ++i) {
    int o = editor_.order() + i - center;
    if (o < 0 || o >= n) continue;
    char b[16];
    snprintf(b, sizeof b, " %02X  %02X ", o, song_.orders[o]);
    bool cur = o == editor_.order();
    if (cur) screen_.textBg(x, y + 1 + i, b, C_BLACK, C_ORANGE);
    else screen_.text(x, y + 1 + i, b, C_WHITE);
    if (playing_ && o == playOrder_) screen_.glyph(x + 9, y + 1 + i, '<', cur ? C_BLACK : C_ORANGE);
  }
}

void App::drawInstruments(int x, int y, int h) {
  screen_.textBg(x, y, " INSTRUMENTS                      ", C_BLACK, C_ORANGE);
  int n = (int)song_.instruments.size();
  int center = (h - 1) / 2;
  for (int i = 0; i < h - 1; ++i) {
    int k = editor_.instrument + i - center;
    if (k < 0 || k >= n) continue;
    char b[48];
    snprintf(b, sizeof b, " %02X %-29.29s", k, song_.instruments[k].name.c_str());
    if (k == editor_.instrument) screen_.textBg(x, y + 1 + i, b, C_BLACK, C_ORANGE);
    else screen_.text(x, y + 1 + i, b, C_WHITE);
  }
}

void App::drawSongInfo(int x, int y) {
  screen_.textBg(x, y, " SONG                       ", C_BLACK, C_ORANGE);
  char b[64];
  snprintf(b, sizeof b, "%-27.27s", song_.title.empty() ? "(untitled)" : song_.title.c_str());
  screen_.text(x + 1, y + 1, b, C_WHITE);
  struct { const char* k; std::string v; } rows[] = {
    { "SPEED", hex2(song_.speed) },
    { "PATTERN", hex2(editor_.patternIndex()) },
    { "ROWS", hex2(editor_.pattern().rows) },
    { "OCTAVE", std::string(1, (char)('1' + editor_.octave)) },
    { "STEP", hex2(editor_.editStep) },
    { "EDIT", editor_.editMode ? "ON" : "OFF" },
    { "PLAY", playing_ ? "PLAYING" : "STOPPED" },
  };
  for (int i = 0; i < 7; ++i) {
    screen_.text(x + 1, y + 3 + i, rows[i].k, C_ORANGE);
    screen_.text(x + 10, y + 3 + i, rows[i].v.c_str(), C_WHITE);
  }
}

void App::drawChannels(int x, int y) {
  screen_.textBg(x, y, " CHANNELS                                     ", C_BLACK, C_ORANGE);
  for (int c = 0; c < kChannels; ++c) {
    int ry = y + 2 + c;
    char b[16];
    snprintf(b, sizeof b, "CH%d", c + 1);
    screen_.text(x + 1, ry, b, muted_[c] ? C_RED : C_WHITE);
    screen_.text(x + 5, ry, c >= 4 ? "W+N" : "WAV", C_DIM);
    // Volume bar: 31 steps -> 16 cells, one pixel row gap.
    int v = views_[c].active ? views_[c].volume : 0;
    screen_.fillPx((x + 9) * kCell, ry * kCell + 1, 16 * kCell, kCell - 2, C_ORANGE_DK);
    if (v > 0) screen_.fillPx((x + 9) * kCell, ry * kCell + 1, (v * 16 * kCell) / 31, kCell - 2, C_ORANGE);
    if (views_[c].active) {
      snprintf(b, sizeof b, "%s %03X", noteName(views_[c].note).c_str(), views_[c].period);
      screen_.text(x + 27, ry, b, C_WHITE);
    }
    if (muted_[c]) screen_.text(x + 37, ry, "MUTE", C_RED);
  }
  screen_.text(x + 1, y + 9, "CH5-6: NOISE   CH2->CH1: LFO", C_DIM);
}

void App::drawPattern(int top, int bottom) {
  // Channel header
  screen_.fill(0, kPatHeader, kCols, 1, C_ORANGE);
  screen_.text(0, kPatHeader, "ROW", C_BLACK);
  for (int c = 0; c < kChannels; ++c) {
    char b[24];
    snprintf(b, sizeof b, "CH%d%s", c + 1, muted_[c] ? " MUTE" : "");
    screen_.text(kChanX0 + c * kChanW, kPatHeader, b, C_BLACK);
  }

  const Pattern& p = editor_.pattern();
  const int center = (top + bottom) / 2;
  int r0, c0, r1, c1;
  editor_.selectionRect(&r0, &c0, &r1, &c1);
  const bool sel = editor_.hasSelection();
  const bool showPlay = playing_ && playOrder_ == editor_.order() &&
                        (editor_.editMode || !followPlay_);

  for (int y = top; y <= bottom; ++y) {
    int r = editor_.row() + (y - center);
    if (r < 0 || r >= p.rows) continue;
    // Row background: cursor row, beat rows (every 4), play position.
    if (y == center) screen_.fill(0, y, kCols, 1, editor_.editMode ? C_ORANGE_MID : C_ORANGE_DK);
    else if (showPlay && r == playRow_) screen_.fill(0, y, kCols, 1, C_ORANGE_DK);
    else if (r % 4 == 0) screen_.fill(0, y, kCols, 1, C_ORANGE_DK);
    if (sel && r >= r0 && r <= r1)
      screen_.fill(kChanX0 + c0 * kChanW, y, (c1 - c0) * kChanW + 17, 1, C_ORANGE_MID);

    screen_.text(0, y, hex2(r).c_str(), r % 4 == 0 ? C_WHITE : C_DIM);
    for (int c = 0; c < kChannels; ++c) {
      const Cell& cell = p.at(r, c);
      int x = kChanX0 + c * kChanW;
      screen_.glyph(x - 2, y, '|', C_DIM);
      std::string note = noteName(cell.note);
      screen_.text(x, y, note.c_str(), cell.note == NOTE_NONE ? C_DIM : C_WHITE);
      screen_.text(x + 4, y, cell.instrument == INST_NONE ? ".." : hex2(cell.instrument).c_str(),
                   cell.instrument == INST_NONE ? C_DIM : C_ORANGE);
      screen_.text(x + 7, y, cell.volume == VOL_NONE ? ".." : hex2(cell.volume).c_str(),
                   cell.volume == VOL_NONE ? C_DIM : C_ORANGE);
      for (int f = 0; f < kEffectColumns; ++f) {
        int fx = x + 10 + f * 4;
        if (cell.fx[f].cmd) {
          char b[4] = { (char)cell.fx[f].cmd, 0, 0, 0 };
          screen_.text(fx, y, b, C_ORANGE);
          screen_.text(fx + 1, y, hex2(cell.fx[f].param).c_str(), C_ORANGE);
        } else {
          screen_.text(fx, y, "...", C_DIM);
        }
      }
    }
  }

  // Cursor cell: orange block with black text.
  int cx = kChanX0 + editor_.channel() * kChanW + kColOff[editor_.column()];
  int w = kColW[editor_.column()];
  screen_.fill(cx, center, w, 1, C_ORANGE);
  const Cell& cur = p.at(editor_.row(), editor_.channel());
  std::string s;
  switch (editor_.column()) {
    case COL_NOTE: s = noteName(cur.note); break;
    case COL_INST_HI: case COL_INST_LO: s = cur.instrument == INST_NONE ? ".." : hex2(cur.instrument); break;
    case COL_VOL_HI: case COL_VOL_LO: s = cur.volume == VOL_NONE ? ".." : hex2(cur.volume); break;
    default: {
      const Effect& f = cur.fx[editor_.column() >= COL_FX2_CMD ? 1 : 0];
      s = f.cmd ? std::string(1, (char)f.cmd) + hex2(f.param) : "...";
      break;
    }
  }
  int idx = 0;
  switch (editor_.column()) {
    case COL_INST_LO: case COL_VOL_LO: idx = 1; break;
    case COL_FX1_HI: case COL_FX2_HI: idx = 1; break;
    case COL_FX1_LO: case COL_FX2_LO: idx = 2; break;
    default: idx = 0; break;
  }
  if (w == 3) screen_.text(cx, center, s.c_str(), C_BLACK);
  else if (idx < (int)s.size()) screen_.glyph(cx, center, (unsigned char)s[idx], C_BLACK);
}

void App::drawStatus() {
  screen_.fill(0, kRows - 1, kCols, 1, C_ORANGE);
  std::string s = message_.empty()
      ? std::string("SPACE edit  ENTER play/stop  SHIFT+ENTER loop pattern  F1/F2 octave  F3/F4 instr  F12 help")
      : message_;
  screen_.text(1, kRows - 1, s.c_str(), C_BLACK);
}

void App::drawHelp() {
  static const char* lines[] = {
    "KEYS                                         (ESC or F12 closes)",
    "",
    "Z S X D C V ... /   notes, lower octave       Q 2 W 3 E ... P   upper octave",
    "1                   note off (OFF)            DEL  clear field   INS / BKSP  insert / delete row",
    "0-9 A-F             instrument, volume, effect parameter digits",
    "letters, digits     effect command (in the command column)",
    "",
    "SPACE               edit mode on/off          ESC  stop / clear block",
    "ENTER               play song / stop          SHIFT+ENTER  loop pattern   CTRL+ENTER  play from row",
    "ARROWS PGUP PGDN    move                      TAB / SHIFT+TAB  next / previous channel",
    "HOME END            first / last row          SHIFT+LEFT/RIGHT  channel jump",
    "",
    "SHIFT+ARROWS        mark block                CTRL+A  block = channel, again = pattern",
    "CTRL+C X V          copy / cut / paste        CTRL+UP/DOWN  transpose (+SHIFT: octave)",
    "CTRL+Z / CTRL+Y     undo / redo",
    "",
    "CTRL+LEFT/RIGHT     previous / next order     CTRL+PGUP/PGDN  pattern number at order",
    "CTRL+INS / CTRL+DEL insert / delete order     CTRL+N  new pattern    CTRL+D  clone pattern",
    "ALT+UP/DOWN         pattern length -/+1       ALT+PGUP/PGDN  length -/+16",
    "ALT+LEFT/RIGHT      song speed -/+",
    "",
    "F1/F2 octave   F3/F4 instrument   F5/F6 edit step   CTRL+1..6 mute   CTRL+SHIFT+1..6 solo",
    "CTRL+S save    CTRL+E export WAV  CTRL+Q quit",
    "",
    "Effects: 0 arp  1/2 slide  3 porta  4 vibrato  8 pan  A vol slide  B jump  C cut  D break",
    "         F speed  G main vol  L/M LFO  N noise (ch5-6)  P detune  R retrig  W wave",
    "Full list: docs/EFFECTS.md",
  };
  const int n = (int)(sizeof lines / sizeof lines[0]);
  const int x = 6, y = 18, w = kCols - 12, h = n + 2;
  screen_.fill(x, y, w, h, C_BLACK);
  screen_.frame(x, y, w, h, C_ORANGE);
  for (int i = 0; i < n; ++i) screen_.text(x + 2, y + 1 + i, lines[i], i == 0 ? C_ORANGE : C_WHITE);
}

} // namespace pce
