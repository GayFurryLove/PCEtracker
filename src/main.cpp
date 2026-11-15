// PCEtracker - entry point.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// Usage: pcetracker [song.pcet] | --version

#define SDL_MAIN_HANDLED
#include "ui/app.h"
#include "version.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i)
    if (!strcmp(argv[i], "--version")) { printf("PCEtracker %s\n", PCE_VERSION); return 0; }
  pce::App app;
  return app.run(argc, argv);
}
