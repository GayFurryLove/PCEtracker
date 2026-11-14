// PCEtracker - application: window, audio device, input, views.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#ifndef PCETRACKER_APP_H
#define PCETRACKER_APP_H

#include "audio/song_render.h"
#include "tracker/editor.h"
#include "ui/config.h"
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
  void playPattern();
  void stopPlayback();
  void save();
  void openSong(const std::string& path);
  void exportWav();
  void exportHesFileAction();
  void exportRomFileAction();
  void message(const std::string& m);

  void draw();
  void drawHeader();
  void drawOrders(int col, int row, int h);
  void drawInstruments(int col, int row, int h);
  void drawSongInfo(int col, int row);
  void drawChannels(int col, int row);
  void drawPattern(int top, int bottom);
  void drawStatus();
  void drawHelp();
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
  bool quit_, quitArmed_;
  bool redraw_;
  bool followPlay_;
  int jamCh_;               // channel of the held preview note, -1 = none
  SDL_Scancode jamKey_;
  // Snapshot of player position for drawing (taken under the audio lock).
  bool playing_;
  int playOrder_, playRow_;
  Player::ChannelView views_[kChannels];
  bool muted_[kChannels];

  enum View { VIEW_PATTERN = 0, VIEW_INSTRUMENT };
  enum Focus { FOCUS_PARAMS = 0, FOCUS_WAVE, FOCUS_MACRO, FOCUS_COUNT };
  enum TextEdit { TEXT_NONE = 0, TEXT_INST_NAME, TEXT_SONG_TITLE, TEXT_OPEN, TEXT_SAVE_AS };
  int view_;
  int focus_;
  int paramRow_;      // 0 = name, 1.. = InstField + 1
  int waveCursor_;
  int macroKind_;
  int macroCursor_;
  int drag_;          // 0 none, 1 wave, 2 macro (mouse drawing)
  int textEdit_;
  std::string textBuf_;
  SDL_Texture* emptyCell_;   // "--- .. .. ... ..." pre-rendered (most cells are empty)
};

} // namespace pce

#endif
