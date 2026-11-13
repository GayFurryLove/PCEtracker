// PCEtracker - text song format (.pcet), module 10: project files.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// Human-readable, versioned, diff-friendly. Format reference: docs/FORMAT.md.

#ifndef PCETRACKER_TEXT_FORMAT_H
#define PCETRACKER_TEXT_FORMAT_H

#include "tracker/song.h"

#include <string>

namespace pce {

const int kFormatVersion = 1;

// Returns true on success; on failure `error` holds "line N: message".
bool loadSongText(const std::string& text, Song* song, std::string* error);
std::string saveSongText(const Song& song);

bool loadSongFile(const char* path, Song* song, std::string* error);
bool saveSongFile(const char* path, const Song& song, std::string* error);

} // namespace pce

#endif
