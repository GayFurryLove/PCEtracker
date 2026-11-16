// PCEtracker - file browser: open, save as, WAV/HES/ROM export, sample import.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// One modal window for every file operation. It starts in the folder of the
// current song, or in song_dir (pcetracker.cfg) for a new song, and shows
// that folder at the top - so it is always visible where a file goes.

#include "ui/app.h"

#include <stdio.h>
#include <string.h>

namespace pce {

namespace {

const int kBrX = 8, kBrY = 4, kBrW = kCols - 16, kBrH = kRows - 8;
const int kBrListTop = kBrY + 5;                    // first list row
const int kBrListRows = kBrH - 9;                   // visible entries

struct Guard {
  SDL_AudioDeviceID d;
  explicit Guard(SDL_AudioDeviceID dev) : d(dev) { if (d) SDL_LockAudioDevice(d); }
  ~Guard() { if (d) SDL_UnlockAudioDevice(d); }
};

const char* title(int mode) {
  switch (mode) {
    case 1: return "LOAD SONG (.PCET)";
    case 2: return "SAVE SONG (.PCET)";
    case 3: return "EXPORT WAV";
    case 4: return "EXPORT HES (PC ENGINE SOUND FILE)";
    case 5: return "EXPORT HUCARD ROM (.PCE)";
    case 6: return "IMPORT .WAV AS DDA SAMPLE (CURRENT INSTRUMENT)";
    default: return "";
  }
}

const char* ext(int mode) {
  switch (mode) {
    case 1: case 2: return ".pcet";
    case 3: case 6: return ".wav";
    case 4: return ".hes";
    case 5: return ".pce";
    default: return "";
  }
}

bool saving(int mode) { return mode >= 2 && mode <= 5; }

int listTop(int cursor, int count, int visible) {
  if (count <= visible) return 0;
  int t = cursor - visible / 2;
  if (t < 0) t = 0;
  if (t > count - visible) t = count - visible;
  return t;
}

std::string replaceExt(const std::string& name, const char* e) {
  std::string n = name;
  size_t dot = n.rfind('.');
  if (dot != std::string::npos && dot > 0) n.erase(dot);
  return n + e;
}

} // namespace

void App::openBrowser(int mode) {
  browse_ = mode;
  brArmed_.clear();
  brList_.clear();
  brDir_.clear();
  brNameFocus_ = saving(mode);
  // Folder of the current song, else song_dir, else the start folder.
  std::string dir = named_ && !path_.empty() ? fs::dirOf(fs::absolute(path_)) : std::string();
  if (dir.empty() || !fs::isDir(dir)) dir = cfg_.startDir();
  if (!cfg_.songDir.empty() && !fs::isDir(fs::absolute(cfg_.songDir)) && !named_)
    message("song_dir not found: " + cfg_.songDir + " (" + cfg_.path + ")");
  brName_ = saving(mode) ? replaceExt(songFileName(), ext(mode)) : std::string();
  browserLoad(dir);
  // Put the cursor on the current song in the list, if it is there.
  const std::string cur = named_ ? fs::fileOf(path_) : std::string();
  for (size_t i = 0; i < brList_.size(); ++i) if (!brList_[i].dir && brList_[i].name == cur) brCursor_ = (int)i;
  SDL_StartTextInput();
  redraw_ = true;
}

void App::browserLoad(const std::string& dir) {
  std::vector<std::string> exts(1, ext(browse_));
  std::vector<fs::Entry> l;
  if (!dir.empty() && !fs::list(dir, exts, &l)) {
    message("Cannot read folder " + dir);
    if (!brDir_.empty() || !brList_.empty()) return; // stay where we are
  }
  brList_.clear();
  brCursor_ = 0;
  if (dir.empty()) {
    // Windows "above the drive": the list of drives.
    std::vector<std::string> r = fs::roots();
    for (size_t i = 0; i < r.size(); ++i) { fs::Entry e; e.name = r[i]; e.dir = true; brList_.push_back(e); }
    brDir_.clear();
    return;
  }
  brDir_ = dir;
  fs::Entry up;
  up.name = "..";
  up.dir = true;
  if (!fs::parent(dir).empty() || !fs::roots().empty()) brList_.push_back(up);
  brList_.insert(brList_.end(), l.begin(), l.end());
}

void App::browserAccept() {
  const bool sv = saving(browse_);
  std::string path;
  if (sv && brNameFocus_) {
    if (brName_.empty()) return;
    std::string n = brName_;
    if (!fs::hasExt(n, ext(browse_))) n += ext(browse_);
    path = fs::isAbsolute(n) ? n : fs::join(brDir_, n);
    if (brDir_.empty() && !fs::isAbsolute(n)) { message("Pick a folder first"); return; }
    if (fs::exists(path) && brArmed_ != path) {
      brArmed_ = path;
      message("File exists: ENTER again to overwrite, ESC to cancel");
      return;
    }
  } else {
    if (brList_.empty()) return;
    const fs::Entry& e = brList_[(size_t)brCursor_];
    if (e.dir) {
      std::string from = brDir_;
      std::string to = e.name == ".." ? fs::parent(brDir_) : (brDir_.empty() ? e.name : fs::join(brDir_, e.name));
      browserLoad(to);
      if (e.name == "..") // keep the cursor on the folder we came from
        for (size_t i = 0; i < brList_.size(); ++i)
          if (brList_[i].dir && (fs::join(brDir_, brList_[i].name) == from || brList_[i].name == from)) brCursor_ = (int)i;
      brArmed_.clear();
      return;
    }
    if (sv) { brName_ = e.name; brNameFocus_ = true; brArmed_.clear(); return; } // pick the name, ENTER again saves
    path = fs::join(brDir_, e.name);
  }
  const int mode = browse_;
  browse_ = BR_NONE;
  SDL_StopTextInput();
  message_.clear();
  switch (mode) {
    case BR_OPEN: openSong(path); break;
    case BR_SAVE: saveAs(path); break;
    case BR_WAV: exportWav(path); break;
    case BR_HES: exportHesFileAction(path); break;
    case BR_ROM: exportRomFileAction(path); break;
    case BR_SAMPLE: importSample(path.c_str()); break;
    default: break;
  }
}

bool App::handleBrowserKey(const SDL_KeyboardEvent& k) {
  const SDL_Scancode sc = k.keysym.scancode;
  const int n = (int)brList_.size();
  const bool sv = saving(browse_);
  int move = 0;
  switch (sc) {
    case SDL_SCANCODE_ESCAPE:
      browse_ = BR_NONE;
      SDL_StopTextInput();
      message_.clear();
      return true;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: browserAccept(); return true;
    case SDL_SCANCODE_TAB: if (sv) { brNameFocus_ = !brNameFocus_; brArmed_.clear(); } return true;
    case SDL_SCANCODE_UP: move = -1; break;
    case SDL_SCANCODE_DOWN: move = 1; break;
    case SDL_SCANCODE_PAGEUP: move = -kBrListRows; break;
    case SDL_SCANCODE_PAGEDOWN: move = kBrListRows; break;
    case SDL_SCANCODE_HOME: if (!brNameFocus_) { brCursor_ = 0; return true; } break;
    case SDL_SCANCODE_END: if (!brNameFocus_) { brCursor_ = n ? n - 1 : 0; return true; } break;
    case SDL_SCANCODE_BACKSPACE:
      if (sv && brNameFocus_) {
        while (!brName_.empty()) {
          unsigned char last = (unsigned char)brName_[brName_.size() - 1];
          brName_.erase(brName_.size() - 1);
          if ((last & 0xC0) != 0x80) break;
        }
        brArmed_.clear();
      } else if (!brDir_.empty()) {
        std::string from = brDir_;
        browserLoad(fs::parent(brDir_));
        for (size_t i = 0; i < brList_.size(); ++i)
          if (fs::join(brDir_, brList_[i].name) == from || brList_[i].name == from) brCursor_ = (int)i;
      }
      return true;
    default: return true; // letters arrive as text input
  }
  if (move && n) {
    brNameFocus_ = false;
    brCursor_ += move;
    if (brCursor_ < 0) brCursor_ = 0;
    if (brCursor_ >= n) brCursor_ = n - 1;
  }
  return true;
}

void App::handleBrowserText(const char* text) {
  redraw_ = true;
  if (!saving(browse_)) {
    // Load / import: a letter jumps to the next entry starting with it.
    unsigned char c = (unsigned char)text[0];
    if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
    const int n = (int)brList_.size();
    for (int k = 1; k <= n; ++k) {
      int i = (brCursor_ + k) % n;
      unsigned char f = (unsigned char)brList_[(size_t)i].name[0];
      if (f >= 'A' && f <= 'Z') f = (unsigned char)(f - 'A' + 'a');
      if (f == c) { brCursor_ = i; break; }
    }
    return;
  }
  brNameFocus_ = true;
  brArmed_.clear();
  for (const char* p = text; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (c >= 32 && brName_.size() < 200 && !strchr("<>:\"|?*", c)) brName_ += (char)c;
  }
}

void App::handleBrowserMouse(const SDL_Event& e) {
  if (e.type == SDL_MOUSEWHEEL) {
    int n = (int)brList_.size();
    brCursor_ += e.wheel.y > 0 ? -3 : 3;
    if (brCursor_ >= n) brCursor_ = n - 1;
    if (brCursor_ < 0) brCursor_ = 0;
    return;
  }
  if (e.type != SDL_MOUSEBUTTONDOWN) return;
  int cx = e.button.x / kCell, cy = e.button.y / kCell;
  if (cx < kBrX || cx >= kBrX + kBrW || cy < kBrY || cy >= kBrY + kBrH) {
    browse_ = BR_NONE; // click outside: cancel
    SDL_StopTextInput();
    return;
  }
  if (cy == kBrY + kBrH - 3 && saving(browse_)) { brNameFocus_ = true; return; }
  if (cy < kBrListTop || cy >= kBrListTop + kBrListRows) return;
  int i = listTop(brCursor_, (int)brList_.size(), kBrListRows) + (cy - kBrListTop);
  if (i < 0 || i >= (int)brList_.size()) return;
  const Uint32 now = SDL_GetTicks();
  const bool dbl = i == brClickItem_ && now - brClickTime_ < 400;
  brClickItem_ = i;
  brClickTime_ = now;
  brCursor_ = i;
  brNameFocus_ = false;
  if (dbl || brList_[(size_t)i].dir) {
    brClickItem_ = -1;
    browserAccept();
    if (browse_ && saving(browse_) && brNameFocus_ && dbl) browserAccept();
  } else if (saving(browse_)) {
    brName_ = brList_[(size_t)i].name;
  }
}

void App::drawBrowser() {
  screen_.fill(kBrX, kBrY, kBrW, kBrH, C_BLACK);
  screen_.frame(kBrX, kBrY, kBrW, kBrH, C_ORANGE);
  screen_.fill(kBrX, kBrY, kBrW, 1, C_ORANGE);
  screen_.text(kBrX + 1, kBrY, title(browse_), C_BLACK);
  const char* keys = "ENTER ok  ESC cancel  BKSP up";
  screen_.text(kBrX + kBrW - 1 - (int)strlen(keys), kBrY, keys, C_BLACK);

  std::string dir = brDir_.empty() ? std::string("(drives)") : brDir_;
  if ((int)dir.size() > kBrW - 9) dir = "..." + dir.substr(dir.size() - (kBrW - 12));
  screen_.text(kBrX + 2, kBrY + 2, "FOLDER", C_ORANGE);
  screen_.text(kBrX + 9, kBrY + 2, dir.c_str(), C_WHITE);
  char b[160];
  snprintf(b, sizeof b, "%s files and folders. Default folder: song_dir in %s", ext(browse_), cfg_.path.c_str());
  std::string hint = b;
  if ((int)hint.size() > kBrW - 4) hint = hint.substr(0, kBrW - 7) + "...";
  screen_.text(kBrX + 2, kBrY + 3, hint.c_str(), C_DIM);

  const int n = (int)brList_.size();
  const int first = listTop(brCursor_, n, kBrListRows);
  for (int r = 0; r < kBrListRows; ++r) {
    int i = first + r;
    if (i >= n) break;
    const fs::Entry& e = brList_[(size_t)i];
    std::string s = e.dir ? (e.name == ".." ? std::string("..  (parent folder)") : "[" + e.name + "]") : e.name;
    if ((int)s.size() > kBrW - 6) s = s.substr(0, kBrW - 9) + "...";
    const int y = kBrListTop + r;
    const bool cur = i == brCursor_;
    if (cur) screen_.fill(kBrX + 1, y, kBrW - 2, 1, brNameFocus_ ? C_ORANGE_DK : C_ORANGE);
    screen_.text(kBrX + 3, y, s.c_str(), cur && !brNameFocus_ ? C_BLACK : (e.dir ? C_ORANGE : C_WHITE));
  }
  if (n == 0) screen_.text(kBrX + 3, kBrListTop, "(empty)", C_DIM);

  const int fy = kBrY + kBrH - 3;
  if (saving(browse_)) {
    screen_.text(kBrX + 2, fy, "FILE", C_ORANGE);
    std::string nm = brName_ + (brNameFocus_ ? "_" : "");
    if ((int)nm.size() > kBrW - 10) nm = "..." + nm.substr(nm.size() - (kBrW - 13));
    if (brNameFocus_) screen_.fill(kBrX + 8, fy, kBrW - 10, 1, C_ORANGE_DK);
    screen_.text(kBrX + 9, fy, nm.c_str(), C_WHITE);
    screen_.text(kBrX + 2, fy + 1, "Type a name, ENTER saves. TAB / ARROWS: pick a folder or file in the list.", C_DIM);
  } else {
    screen_.text(kBrX + 2, fy + 1, "ARROWS / mouse pick, ENTER or double click opens, a letter jumps to it.", C_DIM);
  }
}

} // namespace pce
