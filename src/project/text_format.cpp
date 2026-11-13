// PCEtracker - text song format (.pcet).
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "project/text_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>

namespace pce {

namespace {

const char* kMacroNames[MACRO_COUNT] = { "vol", "arp", "pitch", "wave", "noise", "pan" };

std::string hex2(int v) {
  static const char* d = "0123456789ABCDEF";
  std::string s;
  s += d[(v >> 4) & 15];
  s += d[v & 15];
  return s;
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool parseHex2(const std::string& s, size_t at, int* out) {
  if (at + 2 > s.size()) return false;
  int a = hexDigit(s[at]), b = hexDigit(s[at + 1]);
  if (a < 0 || b < 0) return false;
  *out = a * 16 + b;
  return true;
}

std::string quote(const std::string& s) {
  std::string r = "\"";
  for (size_t i = 0; i < s.size(); ++i) r += (s[i] == '"' || s[i] == '\n') ? '\'' : s[i];
  return r + "\"";
}

// Splits on whitespace, keeping "quoted strings" as one token (without quotes).
std::vector<std::string> tokenize(const std::string& line) {
  std::vector<std::string> t;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
    if (i >= line.size()) break;
    if (line[i] == '#') break;
    if (line[i] == '"') {
      size_t j = line.find('"', i + 1);
      if (j == std::string::npos) j = line.size();
      t.push_back(line.substr(i + 1, j - i - 1));
      i = j + 1;
    } else {
      size_t j = i;
      while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r') ++j;
      t.push_back(line.substr(i, j - i));
      i = j;
    }
  }
  return t;
}

std::string cellText(const Cell& c) {
  std::string s = noteName(c.note);
  s += ' ';
  s += c.instrument == INST_NONE ? "--" : hex2(c.instrument);
  s += ' ';
  s += c.volume == VOL_NONE ? "--" : hex2(c.volume);
  for (int i = 0; i < kEffectColumns; ++i) {
    s += ' ';
    if (c.fx[i].cmd) { s += (char)c.fx[i].cmd; s += hex2(c.fx[i].param); }
    else s += "...";
  }
  return s;
}

bool isEffectCmd(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'); }

bool parseCell(const std::vector<std::string>& t, size_t at, Cell* c, std::string* err) {
  if (at + 3 + kEffectColumns > t.size()) { *err = "row has too few columns"; return false; }
  if (!parseNoteName(t[at], &c->note)) { *err = "bad note '" + t[at] + "'"; return false; }
  int v;
  if (t[at + 1] == "--" || t[at + 1] == "..") c->instrument = INST_NONE;
  else if (t[at + 1].size() == 2 && parseHex2(t[at + 1], 0, &v) && v != INST_NONE) c->instrument = (uint8_t)v;
  else { *err = "bad instrument '" + t[at + 1] + "'"; return false; }
  if (t[at + 2] == "--" || t[at + 2] == "..") c->volume = VOL_NONE;
  else if (t[at + 2].size() == 2 && parseHex2(t[at + 2], 0, &v) && v <= kVolumeMax) c->volume = (uint8_t)v;
  else { *err = "bad volume '" + t[at + 2] + "' (00-1F)"; return false; }
  for (int i = 0; i < kEffectColumns; ++i) {
    const std::string& f = t[at + 3 + i];
    if (f == "...") { c->fx[i] = Effect(); continue; }
    if (f.size() == 3 && isEffectCmd(f[0]) && parseHex2(f, 1, &v)) { c->fx[i] = Effect((uint8_t)f[0], (uint8_t)v); continue; }
    *err = "bad effect '" + f + "'";
    return false;
  }
  return true;
}

bool toInt(const std::string& s, int* out) {
  if (s.empty()) return false;
  char* end = 0;
  long v = strtol(s.c_str(), &end, 10);
  if (*end) return false;
  *out = (int)v;
  return true;
}

} // namespace

std::string saveSongText(const Song& song) {
  std::ostringstream o;
  o << "PCETRACKER " << kFormatVersion << "\n";
  o << "title " << quote(song.title) << "\n";
  o << "author " << quote(song.author) << "\n";
  o << "speed " << song.speed << "\n\n";

  for (size_t w = 0; w < song.waves.size(); ++w) {
    o << "wave " << hex2((int)w) << " ";
    for (int i = 0; i < kWaveLength; ++i) o << hex2(song.waves[w].data[i]);
    o << "\n";
  }
  o << "\n";

  for (size_t n = 0; n < song.instruments.size(); ++n) {
    const Instrument& in = song.instruments[n];
    o << "instrument " << hex2((int)n) << " " << quote(in.name) << "\n";
    o << "  wave " << hex2(in.wave) << "\n";
    o << "  volume " << hex2(in.volume) << "\n";
    if (in.resetPhase) o << "  resetphase\n";
    for (int k = 0; k < MACRO_COUNT; ++k) {
      const Macro& m = in.macros[k];
      if (m.empty()) continue;
      o << "  macro " << kMacroNames[k];
      if (m.loop >= 0) o << " loop " << m.loop;
      if (m.release >= 0) o << " release " << m.release;
      o << " :";
      for (size_t i = 0; i < m.values.size(); ++i)
        o << " " << (k == MACRO_PAN ? (int)(uint8_t)m.values[i] : (int)m.values[i]);
      o << "\n";
    }
    o << "end\n\n";
  }

  for (size_t p = 0; p < song.patterns.size(); ++p) {
    const Pattern& pat = song.patterns[p];
    o << "pattern " << hex2((int)p) << " rows " << pat.rows << "\n";
    o << "#  ";
    for (int c = 0; c < kChannels; ++c) o << (c ? " | " : "") << "ch" << (c + 1) << "               ";
    o << "\n";
    for (int r = 0; r < pat.rows; ++r) {
      o << hex2(r) << " ";
      for (int c = 0; c < kChannels; ++c) o << (c ? " | " : "") << cellText(pat.at(r, c));
      o << "\n";
    }
    o << "end\n\n";
  }

  o << "orders";
  for (size_t i = 0; i < song.orders.size(); ++i) o << " " << hex2(song.orders[i]);
  o << "\n";
  return o.str();
}

bool loadSongText(const std::string& text, Song* out, std::string* error) {
  Song s;
  s.patterns.clear();
  s.instruments.clear();
  s.waves.clear();
  s.orders.clear();

  std::istringstream in(text);
  std::string line;
  int ln = 0;
  enum { TOP, INSTRUMENT, PATTERN } mode = TOP;
  Instrument* inst = 0;
  Pattern* pat = 0;
  int patRow = 0;
  bool header = false;

#define FAIL(msg) do { std::ostringstream e_; e_ << "line " << ln << ": " << msg; *error = e_.str(); return false; } while (0)

  while (std::getline(in, line)) {
    ++ln;
    std::vector<std::string> t = tokenize(line);
    if (t.empty()) continue;

    if (!header) {
      if (t[0] != "PCETRACKER" || t.size() < 2) FAIL("not a PCEtracker song (missing 'PCETRACKER 1' header)");
      int v;
      if (!toInt(t[1], &v) || v < 1 || v > kFormatVersion) FAIL("unsupported format version " << t[1]);
      header = true;
      continue;
    }

    if (mode == PATTERN) {
      if (t[0] == "end") { mode = TOP; continue; }
      // "RR cell | cell | ..." - the row number is optional and may skip rows
      std::vector<std::string> cells;
      for (size_t i = 0; i < t.size(); ++i) if (t[i] != "|") cells.push_back(t[i]);
      size_t at = 0;
      int rowNum;
      if (cells.size() == (size_t)(kChannels * (3 + kEffectColumns) + 1) && cells[0].size() == 2 &&
          parseHex2(cells[0], 0, &rowNum)) {
        patRow = rowNum;
        at = 1;
      }
      if (cells.size() - at != (size_t)(kChannels * (3 + kEffectColumns)))
        FAIL("pattern row must have " << kChannels << " channels of 'note ins vol fx fx'");
      if (patRow >= pat->rows) FAIL("row " << patRow << " is outside the pattern (rows " << pat->rows << ")");
      for (int c = 0; c < kChannels; ++c) {
        std::string err;
        if (!parseCell(cells, at + (size_t)c * (3 + kEffectColumns), &pat->at(patRow, c), &err))
          FAIL("ch" << (c + 1) << ": " << err);
      }
      ++patRow;
      continue;
    }

    if (mode == INSTRUMENT) {
      if (t[0] == "end") { mode = TOP; continue; }
      int v;
      if (t[0] == "wave" && t.size() == 2 && parseHex2(t[1], 0, &v)) { inst->wave = (uint8_t)v; continue; }
      if (t[0] == "volume" && t.size() == 2 && parseHex2(t[1], 0, &v) && v <= kVolumeMax) { inst->volume = (uint8_t)v; continue; }
      if (t[0] == "resetphase") { inst->resetPhase = true; continue; }
      if (t[0] == "macro" && t.size() >= 3) {
        int kind = -1;
        for (int k = 0; k < MACRO_COUNT; ++k) if (t[1] == kMacroNames[k]) kind = k;
        if (kind < 0) FAIL("unknown macro '" << t[1] << "'");
        Macro m;
        size_t i = 2;
        for (; i < t.size() && t[i] != ":"; i += 2) {
          if (i + 1 >= t.size() || !toInt(t[i + 1], &v)) FAIL("bad macro option");
          if (t[i] == "loop") m.loop = v;
          else if (t[i] == "release") m.release = v;
          else FAIL("unknown macro option '" << t[i] << "'");
        }
        if (i >= t.size()) FAIL("macro values must follow ':'");
        for (++i; i < t.size(); ++i) {
          // -128..127; the pan macro also accepts 0..255 (stored as the same byte)
          if (!toInt(t[i], &v) || v < -128 || v > (kind == MACRO_PAN ? 255 : 127))
            FAIL("bad macro value '" << t[i] << "'");
          m.values.push_back((int8_t)(uint8_t)v);
        }
        if ((int)m.values.size() > kMaxMacroLength) FAIL("macro longer than " << kMaxMacroLength);
        if (m.loop >= (int)m.values.size()) m.loop = -1;
        if (m.release >= (int)m.values.size()) m.release = -1;
        inst->macros[kind] = m;
        continue;
      }
      FAIL("unknown instrument field '" << t[0] << "'");
    }

    int v;
    if (t[0] == "title" && t.size() >= 2) { s.title = t[1]; continue; }
    if (t[0] == "author" && t.size() >= 2) { s.author = t[1]; continue; }
    if (t[0] == "speed" && t.size() == 2 && toInt(t[1], &v) && v >= 1 && v <= 31) { s.speed = v; continue; }
    if (t[0] == "wave" && t.size() == 3 && parseHex2(t[1], 0, &v) && v < kMaxWaves) {
      if (t[2].size() != (size_t)kWaveLength * 2) FAIL("wave needs 32 two-digit hex samples");
      Wave w;
      for (int i = 0; i < kWaveLength; ++i) {
        int sv;
        if (!parseHex2(t[2], (size_t)i * 2, &sv) || sv > kWaveMax) FAIL("wave sample " << i << " must be 00-1F (5 bits)");
        w.data[i] = (uint8_t)sv;
      }
      if ((int)s.waves.size() <= v) s.waves.resize((size_t)v + 1);
      s.waves[v] = w;
      continue;
    }
    if (t[0] == "instrument" && t.size() >= 2 && parseHex2(t[1], 0, &v) && v < kMaxInstruments) {
      if ((int)s.instruments.size() <= v) s.instruments.resize((size_t)v + 1);
      inst = &s.instruments[v];
      *inst = Instrument();
      if (t.size() >= 3) inst->name = t[2];
      mode = INSTRUMENT;
      continue;
    }
    if (t[0] == "pattern" && t.size() >= 2 && parseHex2(t[1], 0, &v) && v < kMaxPatterns) {
      int rows = 64;
      if (t.size() >= 4 && t[2] == "rows" && (!toInt(t[3], &rows) || rows < 1 || rows > kMaxRows))
        FAIL("rows must be 1-" << kMaxRows);
      pat = &s.pattern(v);
      *pat = Pattern(rows);
      patRow = 0;
      mode = PATTERN;
      continue;
    }
    if (t[0] == "orders") {
      for (size_t i = 1; i < t.size(); ++i) {
        if (!parseHex2(t[i], 0, &v) || t[i].size() != 2) FAIL("bad order entry '" << t[i] << "'");
        s.orders.push_back((uint8_t)v);
      }
      if ((int)s.orders.size() > kMaxOrders) FAIL("too many orders");
      continue;
    }
    FAIL("unknown line '" << t[0] << "'");
  }
#undef FAIL

  if (!header) { *error = "empty file"; return false; }
  if (mode != TOP) { *error = "missing 'end' at end of file"; return false; }
  if (s.waves.empty()) s.waves.push_back(Wave());
  if (s.instruments.empty()) s.instruments.push_back(Instrument());
  if (s.patterns.empty()) s.patterns.push_back(Pattern(64));
  if (s.orders.empty()) s.orders.push_back(0);
  for (size_t i = 0; i < s.orders.size(); ++i)
    if (s.orders[i] >= s.patterns.size()) s.pattern(s.orders[i]);
  *out = s;
  return true;
}

bool loadSongFile(const char* path, Song* song, std::string* error) {
  FILE* f = fopen(path, "rb");
  if (!f) { *error = std::string("cannot open ") + path; return false; }
  std::string text;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
  fclose(f);
  return loadSongText(text, song, error);
}

bool saveSongFile(const char* path, const Song& song, std::string* error) {
  FILE* f = fopen(path, "wb");
  if (!f) { *error = std::string("cannot write ") + path; return false; }
  std::string text = saveSongText(song);
  bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
  ok = (fclose(f) == 0) && ok;
  if (!ok) *error = std::string("write error: ") + path;
  return ok;
}

} // namespace pce
