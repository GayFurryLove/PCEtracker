// PCEtracker - file system helpers.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "ui/fs.h"
#include "util/file_utf8.h"

#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

namespace pce {
namespace fs {

namespace {

bool isSep(char c) {
#ifdef _WIN32
  return c == '\\' || c == '/';
#else
  return c == '/';
#endif
}

bool isRoot(const std::string& p) {
#ifdef _WIN32
  return (p.size() == 3 && p[1] == ':' && isSep(p[2])) || (p.size() == 2 && p[1] == ':');
#else
  return p == "/";
#endif
}

std::string lower(const std::string& s) {
  std::string r = s;
  for (size_t i = 0; i < r.size(); ++i) if (r[i] >= 'A' && r[i] <= 'Z') r[i] = (char)(r[i] - 'A' + 'a');
  return r;
}

bool entryLess(const Entry& a, const Entry& b) {
  if (a.dir != b.dir) return a.dir;
  return lower(a.name) < lower(b.name);
}

std::string stripSep(std::string p) {
  while (p.size() > 1 && isSep(p[p.size() - 1]) && !isRoot(p)) p.erase(p.size() - 1);
#ifdef _WIN32
  if (p.size() == 2 && p[1] == ':') p += '\\';
#endif
  return p;
}

} // namespace

bool hasExt(const std::string& name, const std::string& ext) {
  return name.size() > ext.size() && lower(name.substr(name.size() - ext.size())) == ext;
}

bool isAbsolute(const std::string& p) {
  if (!p.empty() && isSep(p[0])) return true;
#ifdef _WIN32
  if (p.size() >= 2 && p[1] == ':') return true;
#endif
  return false;
}

std::string currentDir() {
#ifdef _WIN32
  wchar_t* w = _wgetcwd(0, 0);
  std::string r = w ? narrow(w) : std::string(".");
  free(w);
#else
  char buf[4096];
  std::string r = getcwd(buf, sizeof buf) ? std::string(buf) : std::string(".");
#endif
  return stripSep(r);
}

std::string join(const std::string& dir, const std::string& name) {
  if (dir.empty()) return name;
  if (isSep(dir[dir.size() - 1])) return dir + name;
  return dir + kSep + name;
}

std::string parent(const std::string& dir) {
  std::string d = stripSep(dir);
  if (isRoot(d)) return "";
  size_t s = d.find_last_of(
#ifdef _WIN32
    "\\/"
#else
    "/"
#endif
  );
  if (s == std::string::npos) return "";
  std::string p = d.substr(0, s);
  if (p.empty()) return std::string(1, kSep);
  return stripSep(p);
}

std::string dirOf(const std::string& path) {
  size_t s = path.find_last_of(
#ifdef _WIN32
    "\\/"
#else
    "/"
#endif
  );
  if (s == std::string::npos) return "";
  if (s == 0) return std::string(1, kSep);
  return stripSep(path.substr(0, s + 1));
}

std::string fileOf(const std::string& path) {
  size_t s = path.find_last_of(
#ifdef _WIN32
    "\\/"
#else
    "/"
#endif
  );
  return s == std::string::npos ? path : path.substr(s + 1);
}

std::string expandHome(const std::string& p) {
  if (p.empty() || p[0] != '~' || (p.size() > 1 && !isSep(p[1]))) return p;
  const char* home = getenv("HOME");
#ifdef _WIN32
  if (!home) home = getenv("USERPROFILE");
#endif
  if (!home) return p;
  return std::string(home) + p.substr(1);
}

std::string absolute(const std::string& p0) {
  std::string p = expandHome(p0);
  if (p.empty()) return currentDir();
  if (isAbsolute(p)) return stripSep(p);
  return stripSep(join(currentDir(), p));
}

bool isDir(const std::string& p) {
#ifdef _WIN32
  struct _stat st;
  return _wstat(widen(p).c_str(), &st) == 0 && (st.st_mode & _S_IFDIR);
#else
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool exists(const std::string& p) {
#ifdef _WIN32
  struct _stat st;
  return _wstat(widen(p).c_str(), &st) == 0;
#else
  struct stat st;
  return stat(p.c_str(), &st) == 0;
#endif
}

bool list(const std::string& dir, const std::vector<std::string>& exts, std::vector<Entry>* out) {
  out->clear();
  std::vector<std::string> names;
  std::vector<bool> dirs;
#ifdef _WIN32
  struct _wfinddata_t fd;
  intptr_t h = _wfindfirst(widen(join(dir, "*")).c_str(), &fd);
  if (h == -1) return isDir(dir);
  do {
    std::string n = narrow(fd.name);
    if (fd.attrib & (_A_HIDDEN | _A_SYSTEM)) continue;
    names.push_back(n);
    dirs.push_back((fd.attrib & _A_SUBDIR) != 0);
  } while (_wfindnext(h, &fd) == 0);
  _findclose(h);
#else
  DIR* d = opendir(dir.c_str());
  if (!d) return false;
  while (struct dirent* e = readdir(d)) {
    std::string n = e->d_name;
    if (n.empty() || n[0] == '.') continue;
    names.push_back(n);
    dirs.push_back(isDir(join(dir, n)));
  }
  closedir(d);
#endif
  for (size_t i = 0; i < names.size(); ++i) {
    const std::string& n = names[i];
    if (n == "." || n == "..") continue;
    bool keep = dirs[i] || exts.empty();
    for (size_t k = 0; !keep && k < exts.size(); ++k) keep = hasExt(n, exts[k]);
    if (!keep) continue;
    Entry e;
    e.name = n;
    e.dir = dirs[i];
    out->push_back(e);
  }
  std::sort(out->begin(), out->end(), entryLess);
  return true;
}

std::vector<std::string> roots() {
  std::vector<std::string> r;
#ifdef _WIN32
  unsigned long mask = _getdrives();
  for (int i = 0; i < 26; ++i)
    if (mask & (1ul << i)) r.push_back(std::string(1, (char)('A' + i)) + ":\\");
#endif
  return r;
}

} // namespace fs
} // namespace pce
