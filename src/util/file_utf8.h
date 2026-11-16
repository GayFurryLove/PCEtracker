// PCEtracker - fopen() for UTF-8 paths.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// Every path inside the program is UTF-8 (SDL hands out UTF-8, and so does
// the file browser). Linux opens those directly; Windows' fopen() would read
// them in the ANSI code page and fail on e.g. Cyrillic folder names, so there
// the path goes through _wfopen().

#ifndef PCETRACKER_FILE_UTF8_H
#define PCETRACKER_FILE_UTF8_H

#include <stdio.h>
#include <string>

namespace pce {

#ifdef _WIN32
// UTF-8 -> UTF-16 (invalid bytes become U+FFFD).
inline std::wstring widen(const std::string& s) {
  std::wstring w;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    unsigned cp = 0xFFFD;
    int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
    if (n == 0) cp = c;
    else if (n > 0 && i + (size_t)n < s.size()) {
      cp = c & (0x3F >> n);
      for (int k = 1; k <= n; ++k) cp = (cp << 6) | ((unsigned char)s[i + (size_t)k] & 0x3F);
    }
    i += (size_t)(n < 0 ? 1 : n + 1);
    if (cp >= 0x10000) {
      cp -= 0x10000;
      w += (wchar_t)(0xD800 + (cp >> 10));
      w += (wchar_t)(0xDC00 + (cp & 0x3FF));
    } else {
      w += (wchar_t)cp;
    }
  }
  return w;
}

// UTF-16 -> UTF-8.
inline std::string narrow(const std::wstring& w) {
  std::string s;
  for (size_t i = 0; i < w.size(); ++i) {
    unsigned cp = (unsigned)w[i];
    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < w.size()) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + ((unsigned)w[i + 1] - 0xDC00);
      ++i;
    }
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
      s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F));
    } else {
      s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F));
      s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F));
    }
  }
  return s;
}

// Command-line arguments arrive in the ANSI code page, not UTF-8: if the
// UTF-8 reading of the name does not open, try it as an ANSI name.
inline FILE* openFile(const char* path, const char* mode) {
  FILE* f = _wfopen(widen(path).c_str(), widen(mode).c_str());
  return (f || mode[0] != 'r') ? f : fopen(path, mode);
}
#else
inline FILE* openFile(const char* path, const char* mode) { return fopen(path, mode); }
#endif

} // namespace pce

#endif
