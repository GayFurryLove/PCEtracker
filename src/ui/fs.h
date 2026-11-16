// PCEtracker - small file system helpers for the file browser (UTF-8 paths).
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#ifndef PCETRACKER_FS_H
#define PCETRACKER_FS_H

#include <string>
#include <vector>

namespace pce {
namespace fs {

struct Entry {
  std::string name;
  bool dir;
};

const char kSep =
#ifdef _WIN32
  '\\';
#else
  '/';
#endif

bool isAbsolute(const std::string& p);
std::string currentDir();                       // absolute, without a trailing separator (except roots)
std::string join(const std::string& dir, const std::string& name);
std::string parent(const std::string& dir);     // "" when dir is a root ("/" or "C:\")
std::string dirOf(const std::string& path);     // folder part of a file path ("" if none)
std::string fileOf(const std::string& path);    // name part of a file path
std::string absolute(const std::string& p);     // relative -> under currentDir(); "~" -> home
std::string expandHome(const std::string& p);   // "~/x" -> $HOME/x
bool isDir(const std::string& p);
bool exists(const std::string& p);
// Folders first, then files whose name ends with one of `exts` (lower case,
// e.g. ".pcet"; empty = all files). Sorted case-insensitively, hidden files skipped.
bool list(const std::string& dir, const std::vector<std::string>& exts, std::vector<Entry>* out);
// Windows: "C:\", "D:\" ... ; elsewhere empty.
std::vector<std::string> roots();
bool hasExt(const std::string& name, const std::string& ext);

} // namespace fs
} // namespace pce

#endif
