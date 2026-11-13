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
  void exportWav();
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
};

} // namespace pce

#endif
