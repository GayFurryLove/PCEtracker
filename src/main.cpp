// PCEtracker - entry point.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// Usage: pcetracker [song.pcet]

#define SDL_MAIN_HANDLED
#include "ui/app.h"

int main(int argc, char** argv) {
  pce::App app;
  return app.run(argc, argv);
}
