// PCEtracker - application: window, audio device, input, views.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#ifndef PCETRACKER_APP_H
#define PCETRACKER_APP_H

#include "audio/song_render.h"
#include "tracker/editor.h"
#include "ui/config.h"
#include "ui/fs.h"
#include "ui/screen.h"

#include <SDL.h>
#include <string>
#include <vector>

namespace pce {

class App {
public:
  App();
  ~App();
  // Returns a process exit code.
  int run(int argc, char** argv);

private:
  bool initVideo();
  void updateScaling();
  bool initAudio();
  static void audioCallback(void* user, Uint8* stream, int len);
  void fillAudio(int16_t* out, int frames);

  void handleKey(const SDL_KeyboardEvent& k);
  void handleKeyUp(const SDL_KeyboardEvent& k);
  void handleMouse(const SDL_Event& e);
  bool noteKey(SDL_Scancode sc, int* note) const;
  int hexKey(SDL_Scancode sc) const;
  char commandKey(SDL_Scancode sc) const;

  void playSong(bool fromCursor);
  void playFromStart();
  void playPattern();
  void stopPlayback();
  void save();
  void saveAs(const std::string& path);
  void openSong(const std::string& path);
  void exportWav(const std::string& out);
  void exportHesFileAction(const std::string& out);
  void exportRomFileAction(const std::string& out);
  void message(const std::string& m);
  std::string songFileName() const;      // current file name, or one made from the song title

  // File browser (app_files.cpp): open, save as, exports, sample import.
  void openBrowser(int mode);
  void browserLoad(const std::string& dir);
  bool handleBrowserKey(const SDL_KeyboardEvent& k);
  void handleBrowserText(const char* text);
  void handleBrowserMouse(const SDL_Event& e);
  void browserAccept();
  void drawBrowser();

  // Order screen (app_orders.cpp).
  bool handleOrderKey(const SDL_KeyboardEvent& k);
  void handleOrderMouse(const SDL_Event& e);
  void drawOrderView(int top, int bottom);

  // Help pages and status hints (app_help.cpp).
  void drawHelp();
  std::string effectHint() const;

  void draw();
  void drawHeader();
  void drawOrders(int col, int row, int h);
  void drawInstruments(int col, int row, int h);
  void drawSongInfo(int col, int row);
  void drawChannels(int col, int row);
  void drawPattern(int top, int bottom);
  void drawStatus();
  void drawSplash();
  void closeSplash();
  bool splashInputReady(const SDL_Event& e) const;
  int runBenchmark();

  // Instrument screen (app_instrument.cpp)
  bool handleInstrumentKey(const SDL_KeyboardEvent& k);
  void handleInstrumentMouse(const SDL_Event& e);
  void drawInstrumentView(int top, int bottom);
  void jamInstrument(SDL_Scancode sc, bool repeat);
  void importSample(const char* path);
  void startTextEdit(int what);
  void handleTextInput(const char* text);
  bool handleTextKey(const SDL_KeyboardEvent& k);

  Config cfg_;
  Song song_;
  Editor editor_;
  SongRenderer* renderer_;   // owned; used by the audio thread under the device lock
  SDL_Window* window_;
  SDL_Renderer* sdlRenderer_;
  Screen screen_;
  SDL_AudioDeviceID audioDev_;
  std::vector<int16_t> fifo_;
  size_t fifoHead_;

  std::string path_;
  std::string message_;
  Uint32 messageTime_;
  bool showHelp_;
  int helpPage_;
  bool splash_;          // startup logo: shown until a key, a click or the timeout
  bool splashDrawn_;     // the timeout runs from the first frame actually shown
  Uint32 splashStart_;
  std::vector<Uint8> splashHeld_; // keys already down at start (ignored: X11 replays them on focus)
  bool named_;           // path_ is a real file name (loaded, saved or given on the command line)
  bool quit_, quitArmed_;
  bool redraw_;
  bool followPlay_;
  int jamCh_;               // channel of the held preview note, -1 = none
  SDL_Scancode jamKey_;
  // Snapshot of player position for drawing (taken under the audio lock).
  bool playing_;
  int playOrder_, playRow_;
  uint64_t playTicks_;
  int playSpeed_, playTempo_;
  Player::ChannelView views_[kChannels];
  bool muted_[kChannels];

  enum View { VIEW_PATTERN = 0, VIEW_INSTRUMENT, VIEW_ORDER };
  enum Focus { FOCUS_PARAMS = 0, FOCUS_WAVE, FOCUS_MACRO, FOCUS_COUNT };
  enum TextEdit { TEXT_NONE = 0, TEXT_INST_NAME, TEXT_SONG_TITLE };
  enum Browse { BR_NONE = 0, BR_OPEN, BR_SAVE, BR_WAV, BR_HES, BR_ROM, BR_SAMPLE };
  int view_;
  int focus_;
  int paramRow_;      // 0 = name, 1.. = InstField + 1
  int waveCursor_;
  int macroKind_;
  int macroCursor_;
  int drag_;          // 0 none, 1 wave, 2 macro (mouse drawing)
  int textEdit_;
  std::string textBuf_;
  // file browser
  int browse_;
  std::string brDir_;          // "" = list of drives (Windows)
  std::vector<fs::Entry> brList_;
  int brCursor_;
  std::string brName_;
  bool brNameFocus_;
  std::string brArmed_;        // existing file: ENTER again overwrites it
  Uint32 brClickTime_;
  int brClickItem_;
  // order screen
  int orderFocus_;             // 0 order list, 1 pattern list
  int patCursor_;
  SDL_Texture* emptyCell_;   // "--- .. .. ... ..." pre-rendered (most cells are empty)
};

} // namespace pce

#endif
