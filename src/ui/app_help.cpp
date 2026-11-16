// PCEtracker - F12 help (one page per topic, short but complete) and the
// status-line hint for the effect under the cursor.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "ui/app.h"
#include "version.h"

#include <stdio.h>
#include <string.h>

namespace pce {

namespace {

struct Page { const char* name; const char* const* lines; int count; };

const char* const kPattern[] = {
  "NOTES",
  "Z S X D C V G B H N J M , L . ; /   lower octave      Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P   upper octave",
  "1 (or `)            note off (OFF): release macros, or silence",
  "Keys work by position on the keyboard: the layout (also Russian) does not matter.",
  "",
  "EDITING  (SPACE = edit mode on/off; without it keys only preview, nothing is written)",
  "0-9 A-F             instrument, volume and effect parameter digits",
  "letter / digit      effect command, in the command column; the cursor then goes to the 2 digits,",
  "                    after the second digit it comes back to the command column, one step down",
  "DEL                 empty ONLY the field under the cursor (note, instrument, volume or effect);",
  "                    with a marked block: empty the block",
  "BACKSPACE           delete this channel's row, rows below move up",
  "INS                 insert an empty row in this channel, rows below move down",
  "F5 / F6             edit step -/+ (rows the cursor advances after an entry)",
  "",
  "MOVING",
  "ARROWS              move   (LEFT/RIGHT go through every column of every channel)",
  "PGUP PGDN           16 rows up / down        HOME END    first / last row",
  "TAB / SHIFT+TAB     next / previous channel  SHIFT+LEFT/RIGHT  channel, same column",
  "mouse               click a cell, wheel scrolls",
  "",
  "PLAYING",
  "ENTER               play the song from the current order / stop",
  "F7                  play the song from the very beginning (order 00, row 00)",
  "CTRL+ENTER          play from the cursor row        SHIFT+ENTER  loop the current pattern",
  "ESC                 stop (or unmark the block)",
  "CTRL+1..6           mute channel        CTRL+SHIFT+1..6  solo channel",
  "The SONG panel shows the speed and tempo the player uses right now and a stopwatch.",
  "",
  "SETTINGS",
  "F1 / F2 octave      F3 / F4 instrument     CTRL+T song title (also names a new song's file)",
};

const char* const kBlocks[] = {
  "BLOCKS",
  "SHIFT+ARROWS / PGUP / PGDN / HOME / END     mark a block (whole channels, from row to row)",
  "CTRL+A              mark the channel, press again: the whole pattern",
  "CTRL+C / X / V      copy / cut / paste at the cursor (top left of the block)",
  "DEL                 empty the marked block",
  "CTRL+UP / DOWN      transpose the block (or the note under the cursor) a semitone; +SHIFT: an octave",
  "CTRL+Z / CTRL+Y     undo / redo (also CTRL+SHIFT+Z)",
  "",
  "ORDERS AND PATTERNS (also on the order screen, F8)",
  "CTRL+LEFT / RIGHT   previous / next order position",
  "CTRL+PGUP / PGDN    pattern number at this order position +1 / -1",
  "CTRL+INS / CTRL+DEL insert (a copy of this entry) / delete an order position",
  "CTRL+N              new empty pattern, placed after this position",
  "CTRL+D              clone this pattern, placed after this position",
  "ALT+UP / DOWN       pattern length -1 / +1        ALT+PGUP / PGDN  length -16 / +16",
  "ALT+LEFT / RIGHT    song speed -/+ (ticks per row)  ALT+SHIFT+LEFT / RIGHT  song tempo -/+",
  "",
  "Rows past the end of a pattern show the next order's pattern, dimmed.",
  "",
  "SCREENS",
  "F9 patterns         F10 instrument          F8 or F11 order list + pattern list",
  "F12 this help: LEFT / RIGHT, PGUP / PGDN or 1-7 change the page, ESC or F12 closes",
};

const char* const kEffects[] = {
  "EFFECTS (all numbers hex)                       S = stays on until changed, R = this row only",
  "0xy  S arpeggio: note, +x, +y semitones, one step per tick; 0x0 = two notes. Stops only with 000",
  "Exx  S arpeggio speed: xx ticks per arpeggio step (E01 = every tick, default)",
  "1xx  R slide up: period -xx per tick          2xx  R slide down: period +xx per tick",
  "3xx  R portamento to the note of this row, speed xx (00 = the previous speed)",
  "4xy  R vibrato: x speed, y depth (32-step sine)",
  "8xy  S panning: x left, y right volume (0-F)",
  "Axy  R volume slide: +x or -y per tick",
  "Bxx    jump to order position xx",
  "Cxx  R cut the note at tick xx",
  "Dxx    end of pattern, continue at row xx of the next one",
  "Fxx  S 01-1F: speed (ticks per row)   20-FE: tempo, F96 (=150) normal: row = speed x 150 / tempo ticks",
  "       F00 or FFF: STOP the song (silence; WAV/HES/ROM end there)",
  "Gxy  S main volume: x left, y right",
  "Lxx  S LFO (ch2 modulates ch1): 00 off, 01-03 depth, +80 reset          Mxx  S LFO frequency",
  "Nxx  S noise, channels 5-6 only: 00 off, 01-20 on with frequency xx-1",
  "Pxx  S detune: period + (xx - 80)",
  "Rxx  R retrigger the note every xx ticks",
  "Wxx    wave xx of the song's wave bank for the current note. On a row with a note it replaces",
  "       the instrument's wave for that note; the next note loads its instrument's wave again.",
  "       A wave macro in the instrument overrides Wxx. CTRL+W (instrument screen) adds waves.",
  "",
  "A tick is one 59.826 Hz frame. Row = speed ticks at tempo 150. Tick 0 reads the row, slides work",
  "on the other ticks. Pitch effects work in period units (lower period = higher note), like the chip.",
  "",
  "WHY SOME THINGS ARE MISSING: the console's driver runs once per frame; there is no BPM clock,",
  "no noise on channels 1-4, wave RAM can only change with the channel off (a new wave clicks).",
  "The status line shows what the effect under the cursor does.   Full text: docs/EFFECTS.md",
};

const char* const kInstrument[] = {
  "INSTRUMENT SCREEN (F10)            TAB: parameters -> wave -> macro       note keys: preview",
  "PARAMETERS          UP/DOWN field, LEFT/RIGHT value (SHIFT x16), ENTER name / toggle",
  "CTRL+N new instrument   CTRL+D clone   CTRL+W new wave (copy of this one)   all undoable",
  "",
  "WAVE (32 x 5 bit)   LEFT/RIGHT sample, UP/DOWN value (SHIFT 4), PGUP/PGDN other wave, mouse draws",
  "ALT+1..9            sine square saw triangle noise clear invert shift half",
  "",
  "MACROS              one value per step; a step lasts SPD ticks (ALT+LEFT/RIGHT, 1-64)",
  "PGUP/PGDN           choose the macro: VOLUME ARPEGGIO PITCH WAVE NOISE PAN",
  "LEFT/RIGHT          step        UP/DOWN value (SHIFT 8)       mouse: draw the values",
  "INS / DEL           insert / delete a step     ALT+C clear the macro",
  "",
  "LOOP - how to:      1. draw or type the steps   2. put the cursor on the step where the",
  "                    loop should start   3. ALT+L. An 'L' appears under that step, the looped",
  "                    part turns bright. From the last step the macro jumps back to L, for as",
  "                    long as the note sounds. ALT+L on the same step again removes the loop.",
  "RELEASE:            ALT+R on a step: the macro waits there (or loops L..R) until the note gets",
  "                    OFF, then plays the steps after R. Example volume: 31 28 24 [R] 20 12 6 0",
  "Without a loop the macro stops on its last value.",
  "",
  "WAVE MACRO = wave modulation: the values are wave numbers. Each step switches the channel to",
  "that wave; SPD sets how many ticks each wave lasts. Example: values 0 1 2 3, SPD 4, loop at 0:",
  "four waves, 4 ticks each, over and over. Every switch rewrites wave RAM (as on the console).",
  "",
  "DDA SAMPLES         drop a .wav on the window or CTRL+I: 5-bit, ~6991 Hz, C-4 = original pitch",
};

const char* const kOrders[] = {
  "ORDER SCREEN (F8 or F11)      left: order list   middle: pattern list   right: pattern preview",
  "Works without edit mode. TAB switches between the order list and the pattern list.",
  "",
  "ORDER LIST",
  "UP / DOWN, PGUP / PGDN, HOME / END     choose a position (also the mouse and the wheel)",
  "0-9 A-F             type the pattern number of this position (digits shift in: 0 then 5 = 05)",
  "+ / -               pattern number +1 / -1",
  "SHIFT+UP / DOWN     move this entry up / down (reorder the song)",
  "INS                 insert a copy of this entry after it",
  "DEL / BACKSPACE     delete this position",
  "CTRL+N / CTRL+D     new empty pattern / clone of this pattern, after this position",
  "F9 (or double click) edit the pattern of this position       ENTER  play from here / stop",
  "",
  "PATTERN LIST        every pattern: rows, how often the order list uses it (x02), filled cells",
  "UP / DOWN ...       choose a pattern (unused patterns are dim)",
  "ENTER               put it at the current order position",
  "INS                 add it as a new order position after the current one",
  "",
  "Patterns are shared: the same pattern number at two positions plays the same data,",
  "an edit changes both. CTRL+D first if one of them should differ.",
};

const char* const kFiles[] = {
  "FILES",
  "CTRL+O              load a song           CTRL+S  save (a new song asks for folder and name)",
  "CTRL+SHIFT+S        save as               CTRL+Q  quit (asks again if there are unsaved changes)",
  "CTRL+E              export WAV            CTRL+H  export HES (PC Engine sound file)",
  "CTRL+R              export HuCard ROM     CTRL+I  import a .wav as DDA sample",
  "",
  "THE FILE WINDOW     shows the folder at the top. ARROWS / mouse choose, ENTER opens a folder or",
  "                    a file, BACKSPACE = parent folder, a letter jumps to it (load). Saving and",
  "                    exports: type the name (it starts as the song's name), ENTER writes it,",
  "                    TAB / ARROWS go to the list. An existing file needs ENTER twice.",
  "                    New songs are named after the song title (CTRL+T): 'My Song' -> my_song.pcet",
  "                    HES and ROM are checked on a built-in HuC6280 before they are written.",
  "",
  "CONFIG FILE pcetracker.cfg  (key=value, # comment; new keys are added to an old file by themselves)",
  "  found: next to the program, else in the start folder, else Linux ~/.local/share/pcetracker/",
  "         pcetracker/, Windows %APPDATA%\\pcetracker\\pcetracker\\",
  "  song_dir=PATH      folder the file window starts in for a new song (~ = home folder)",
  "  demo=1 / 0         startup logo with the mascot on / off",
  "  scale fullscreen software_render vsync fps audio_rate audio_buffer revision   see docs/MANUAL.md",
};

const Page kPages[] = {
  { "PATTERN", kPattern, (int)(sizeof kPattern / sizeof kPattern[0]) },
  { "BLOCKS+ORDERS", kBlocks, (int)(sizeof kBlocks / sizeof kBlocks[0]) },
  { "EFFECTS", kEffects, (int)(sizeof kEffects / sizeof kEffects[0]) },
  { "FILES+CONFIG", kFiles, (int)(sizeof kFiles / sizeof kFiles[0]) },
  { "INSTRUMENT", kInstrument, (int)(sizeof kInstrument / sizeof kInstrument[0]) },
  { "ORDER SCREEN", kOrders, (int)(sizeof kOrders / sizeof kOrders[0]) },
  { "ABOUT", 0, 0 },
};
const int kPageCount = (int)(sizeof kPages / sizeof kPages[0]);

bool isHeading(const char* s) {
  if (!*s) return false;
  for (const char* p = s; *p && *p != ' ' && *p != '('; ++p) if (*p >= 'a' && *p <= 'z') return false;
  return s[0] >= 'A' && s[0] <= 'Z' && (strlen(s) < 2 || (s[1] >= 'A' && s[1] <= 'Z'));
}

} // namespace

void App::drawHelp() {
  if (helpPage_ < 0 || helpPage_ >= kPageCount) helpPage_ = 0;
  const int x = 1, y = 1, w = kCols - 2, h = kRows - 2;
  screen_.fill(0, 0, kCols, kRows, C_BLACK);
  screen_.fill(x, y, w, h, C_BLACK);
  screen_.frame(x, y, w, h, C_ORANGE);
  screen_.fill(x, y, w, 1, C_ORANGE);
  screen_.text(x + 1, y, "HELP", C_BLACK);
  // Page tabs.
  int tx = x + 7;
  for (int i = 0; i < kPageCount; ++i) {
    char b[32];
    snprintf(b, sizeof b, " %d %s ", i + 1, kPages[i].name);
    if (i == helpPage_) screen_.textBg(tx, y, b, C_ORANGE, C_BLACK);
    else screen_.text(tx, y, b, C_BLACK);
    tx += (int)strlen(b) + 1;
  }
  const char* keys = "LEFT/RIGHT page  ESC close";
  screen_.text(x + w - 1 - (int)strlen(keys), y + h - 1, keys, C_DIM);

  const Page& pg = kPages[helpPage_];
  if (pg.lines) {
    for (int i = 0; i < pg.count && i < h - 4; ++i) {
      const char* s = pg.lines[i];
      screen_.text(x + 2, y + 2 + i, s, isHeading(s) ? C_ORANGE : C_WHITE);
    }
    return;
  }
  // About: version and the config file actually in use.
  char b[200];
  int ry = y + 3;
  screen_.text(x + 2, ry++, "PCETRACKER " PCE_VERSION, C_ORANGE);
  screen_.text(x + 2, ry++, "PC Engine / TurboGrafx-16 music tracker: 6 channels of the HuC6280 PSG, 60 Hz driver.", C_WHITE);
  ++ry;
  snprintf(b, sizeof b, "Config file:  %.105s", cfg_.path.c_str());
  screen_.text(x + 2, ry++, b, C_WHITE);
  snprintf(b, sizeof b, "song_dir:     %.105s", cfg_.songDir.empty() ? "(empty: the start folder)" : cfg_.songDir.c_str());
  screen_.text(x + 2, ry++, b, C_WHITE);
  snprintf(b, sizeof b, "Start folder: %.105s", fs::currentDir().c_str());
  screen_.text(x + 2, ry++, b, C_WHITE);
  snprintf(b, sizeof b, "Startup logo: %s (demo=%d)", cfg_.splash ? "on" : "off", cfg_.splash ? 1 : 0);
  screen_.text(x + 2, ry++, b, C_WHITE);
  ++ry;
  screen_.text(x + 2, ry++, "Manual: docs/MANUAL.md   Effects: docs/EFFECTS.md   Instruments: docs/INSTRUMENTS.md", C_DIM);
  screen_.text(x + 2, ry++, "Export to the console: docs/EXPORT.md   Song file format: docs/FORMAT.md", C_DIM);
}

// What the effect under the cursor does, for the status line.
std::string App::effectHint() const {
  const int col = editor_.column();
  if (col < COL_FX1_CMD || view_ != VIEW_PATTERN || showHelp_) return std::string();
  const Effect& f = editor_.pattern().at(editor_.row(), editor_.channel()).fx[col >= COL_FX2_CMD ? 1 : 0];
  if (!f.cmd) return "Effect column: type a command letter, then 2 hex digits. F12 page 3 lists all effects";
  const int p = f.param, x = p >> 4, y = p & 15;
  char b[160];
  switch (f.cmd) {
    case '0':
      if (!p) snprintf(b, sizeof b, "000: arpeggio OFF");
      else snprintf(b, sizeof b, "0%X%X: arpeggio note +%d +%d, stays on until 000 (Exx = speed)", x, y, x, y);
      break;
    case '1': snprintf(b, sizeof b, "1%02X: slide up, period -%d per tick (this row)", p, p); break;
    case '2': snprintf(b, sizeof b, "2%02X: slide down, period +%d per tick (this row)", p, p); break;
    case '3': snprintf(b, sizeof b, "3%02X: portamento to this row's note, speed %d", p, p); break;
    case '4': snprintf(b, sizeof b, "4%X%X: vibrato speed %d depth %d (this row)", x, y, x, y); break;
    case '8': snprintf(b, sizeof b, "8%X%X: panning left %d right %d", x, y, x, y); break;
    case 'A': snprintf(b, sizeof b, "A%X%X: volume %s%d per tick", x, y, x ? "+" : "-", x ? x : y); break;
    case 'B': snprintf(b, sizeof b, "B%02X: jump to order %02X", p, p); break;
    case 'C': snprintf(b, sizeof b, "C%02X: cut the note at tick %d", p, p); break;
    case 'D': snprintf(b, sizeof b, "D%02X: next pattern from row %02X", p, p); break;
    case 'E': snprintf(b, sizeof b, "E%02X: arpeggio speed, %d tick%s per step", p, p ? p : 1, (p ? p : 1) == 1 ? "" : "s"); break;
    case 'F':
      if (p == 0 || p == 0xFF) snprintf(b, sizeof b, "F%02X: STOP the song here", p);
      else if (p <= 0x1F) snprintf(b, sizeof b, "F%02X: speed %d ticks per row", p, p);
      else snprintf(b, sizeof b, "F%02X: tempo %d (150 = normal), a row = speed x 150 / %d ticks", p, p, p);
      break;
    case 'G': snprintf(b, sizeof b, "G%X%X: main volume left %d right %d", x, y, x, y); break;
    case 'L': snprintf(b, sizeof b, "L%02X: LFO ch2 -> ch1, %s", p, (p & 3) ? "on" : "off"); break;
    case 'M': snprintf(b, sizeof b, "M%02X: LFO frequency %d", p, p); break;
    case 'N': snprintf(b, sizeof b, "N%02X: noise %s (channels 5-6 only)", p, p ? "on" : "off"); break;
    case 'P': snprintf(b, sizeof b, "P%02X: detune, period %+d", p, p - 0x80); break;
    case 'R': snprintf(b, sizeof b, "R%02X: retrigger every %d ticks", p, p); break;
    case 'W':
      if (p < (int)song_.waves.size()) snprintf(b, sizeof b, "W%02X: wave %02X for this note (the song has %d waves)", p, p, (int)song_.waves.size());
      else snprintf(b, sizeof b, "W%02X: NO SUCH WAVE - the song has waves 00-%02X (CTRL+W on F10 adds one)", p, (int)song_.waves.size() - 1);
      break;
    default: snprintf(b, sizeof b, "%c%02X: unknown command, does nothing", f.cmd, p); break;
  }
  return b;
}

} // namespace pce
