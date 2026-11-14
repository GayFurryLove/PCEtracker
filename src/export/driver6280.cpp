// PCEtracker - the PC Engine sound driver, written with the in-memory assembler.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// How it works on the console:
//   * VBlank interrupt (60 Hz): reads one frame of the stream and performs its
//     PSG register writes - the exact writes the tracker's player produced.
//   * Timer interrupt (~6991 Hz): for every channel playing a DDA sample,
//     writes the next 5-bit value and advances an 8.8 fixed-point position.
// The same code is the HES player and (stage 7) the HuCard ROM.

#include "export/driver6280.h"
#include "export/asm6280.h"
#include "font8x8_basic.h"

namespace pce {

namespace {

// Zero page ($2000-$20FF, RAM bank $F8 at MPR1).
enum {
  ZP_SPTR = 0x00,   // stream pointer (2 bytes), inside the $6000 window
  ZP_SBANK = 0x02,  // bank mapped at MPR3 (MPR4 = bank + 1)
  ZP_WAIT = 0x03,   // frames left without commands
  ZP_PSEL = 0x04,   // copy of the PSG channel select (the register is write-only)
  ZP_TMP = 0x05,    // 2 bytes, frame code only
  ZP_DPTR = 0x07,   // 2 bytes, timer code only
  // per channel, indexed by X = 0..5
  ZP_D_ON = 0x10, ZP_D_FRAC = 0x18, ZP_D_LO = 0x20, ZP_D_HI = 0x28, ZP_D_BANK = 0x30,
  ZP_D_STLO = 0x38, ZP_D_STHI = 0x40, ZP_D_SMP = 0x48
};

const uint16_t PSG = 0x0800;   // I/O page is mapped at MPR0
const uint16_t TIMER = 0x0C00;
const uint16_t IRQ_MASK = 0x1402, IRQ_ACK = 0x1403;

uint16_t table(int t) { return (uint16_t)(kSampleTables + t * 64); }

} // namespace

bool buildDriverBank(uint8_t streamBank, const DriverOptions& opt, std::vector<uint8_t>* bank,
                     uint16_t* resetAddr, std::string* err) {
  using namespace op6280;
  Asm6280 a(kDriverOrg);
  typedef Asm6280::Label L;
  L reset = a.label(), irq1 = a.label(), timer = a.label(), stub = a.label();
  L getb = a.label(), mapstream = a.label();
  L fontData = a.label(), textData = a.label();

  // ---- reset -----------------------------------------------------------------
  a.bind(reset);
  a.i(SEI); a.i(CSH); a.i(CLD);
  a.b(LDX_IMM, 0xFF); a.i(TXS);
  a.b(LDA_IMM, 0xFF); a.b(TAM, 0x01);          // MPR0 = I/O
  a.b(LDA_IMM, 0xF8); a.b(TAM, 0x02);          // MPR1 = RAM (zero page, stack)
  a.b(LDX_IMM, 0x7F);
  { L clr = a.hereLabel(); a.b(STZ_ZPX, 0x00); a.i(DEX); a.br(BPL, clr); }
  a.b(LDA_IMM, (uint8_t)(kStreamWindow >> 8)); a.b(STA_ZP, ZP_SPTR + 1);
  a.b(LDA_IMM, streamBank); a.b(STA_ZP, ZP_SBANK);
  a.wl(JSR, mapstream);
  if (opt.hucard) {
    // ---- HuCard: video chip, palette and title screen ----
    // VDC timing for a 256 x 240 display (the standard values used by HuC):
    // without valid timing the VDC never signals VBlank on real hardware.
    static const uint8_t vdcInit[][3] = {
      { 0x05, 0x00, 0x00 },   // control: display and interrupts off while we set up
      { 0x07, 0x00, 0x00 },   // BXR scroll x
      { 0x08, 0x00, 0x00 },   // BYR scroll y
      { 0x09, 0x00, 0x00 },   // MWR: 32 x 32 background map
      { 0x0A, 0x02, 0x02 },   // HSR
      { 0x0B, 0x1F, 0x03 },   // HDR: 32 tiles wide
      { 0x0C, 0x02, 0x0F },   // VPR
      { 0x0D, 0xEF, 0x00 },   // VDW: 240 lines
      { 0x0E, 0x03, 0x00 },   // VCR
    };
    a.w(LDA_ABS, 0x0000);                            // clear a pending VDC interrupt
    for (size_t k = 0; k < sizeof vdcInit / sizeof vdcInit[0]; ++k) {
      a.b(ST0, vdcInit[k][0]); a.b(ST1, vdcInit[k][1]); a.b(ST2, vdcInit[k][2]);
    }
    // VCE control: 5.37 MHz dot clock, bit 2 = 263 lines per frame (59.826 Hz, the
    // rate the tracker plays at; 0 would give 262 lines and music 0.38 % fast).
    a.b(LDA_IMM, 0x04); a.w(STA_ABS, 0x0400);
    // palette: BG palette 0 = black + orange, palette 1 = black + white (9-bit GRB)
    static const uint16_t colors[4][2] = { { 0, 0x000 }, { 1, 0x138 }, { 16, 0x000 }, { 17, 0x1FF } };
    for (int k = 0; k < 4; ++k) {
      a.b(LDA_IMM, (uint8_t)colors[k][0]); a.w(STA_ABS, 0x0402); a.w(STZ_ABS, 0x0403);
      a.b(LDA_IMM, (uint8_t)colors[k][1]); a.w(STA_ABS, 0x0404);
      a.b(LDA_IMM, (uint8_t)(colors[k][1] >> 8)); a.w(STA_ABS, 0x0405);
    }
    // font: 96 glyphs (ASCII 32..127) as 4-bit tiles 256..351 at VRAM $1000
    a.b(ST0, 0x00); a.b(ST1, 0x00); a.b(ST2, 0x10); a.b(ST0, 0x02);
    a.lo(LDA_IMM, fontData); a.b(STA_ZP, ZP_TMP);
    a.hi(LDA_IMM, fontData); a.b(STA_ZP, ZP_TMP + 1);
    a.b(LDA_IMM, 96); a.b(STA_ZP, ZP_DPTR);         // glyph counter (timer is still off)
    { L glyph = a.hereLabel(), row = a.label(), pad = a.label();
      a.b(LDY_IMM, 0x00);
      a.bind(row);                                   // planes 0/1: the glyph row, 0
      a.b(LDA_INDY, ZP_TMP); a.w(STA_ABS, 0x0002); a.w(STZ_ABS, 0x0003);
      a.i(INY); a.b(CPY_IMM, 8); a.br(BNE, row);
      a.b(LDY_IMM, 8);
      a.bind(pad);                                   // planes 2/3: empty
      a.w(STZ_ABS, 0x0002); a.w(STZ_ABS, 0x0003);
      a.i(DEY); a.br(BNE, pad);
      a.i(CLC); a.b(LDA_ZP, ZP_TMP); a.b(ADC_IMM, 8); a.b(STA_ZP, ZP_TMP);
      { L nc = a.label(); a.br(BCC, nc); a.b(INC_ZP, ZP_TMP + 1); a.bind(nc); }
      a.b(DEC_ZP, ZP_DPTR); a.br(BNE, glyph); }
    // background map: 32 x 32 cells of tile 256 (space)
    a.b(ST0, 0x00); a.b(ST1, 0x00); a.b(ST2, 0x00); a.b(ST0, 0x02);
    a.b(LDX_IMM, 4);
    { L outer = a.hereLabel(), inner = a.label();
      a.b(LDY_IMM, 0x00);
      a.bind(inner);
      a.w(STZ_ABS, 0x0002); a.b(LDA_IMM, 0x01); a.w(STA_ABS, 0x0003);
      a.i(DEY); a.br(BNE, inner);
      a.i(DEX); a.br(BNE, outer); }
    // text lines: [length][map address lo][hi][tile high byte][tile low bytes...], 0 = end
    a.lo(LDA_IMM, textData); a.b(STA_ZP, ZP_TMP);
    a.hi(LDA_IMM, textData); a.b(STA_ZP, ZP_TMP + 1);
    { L line = a.hereLabel(), chars = a.label(), end = a.label();
      a.b(LDY_IMM, 0x00);
      a.b(LDA_INDY, ZP_TMP); a.br(BEQ, end);
      a.b(STA_ZP, ZP_DPTR);                          // characters left
      a.w(STZ_ABS, 0x0000);                          // VDC register 0 (MAWR)
      a.i(INY); a.b(LDA_INDY, ZP_TMP); a.w(STA_ABS, 0x0002);
      a.i(INY); a.b(LDA_INDY, ZP_TMP); a.w(STA_ABS, 0x0003);
      a.b(LDA_IMM, 0x02); a.w(STA_ABS, 0x0000);      // VDC register 2 (VRAM data)
      a.i(INY); a.b(LDA_INDY, ZP_TMP); a.b(STA_ZP, ZP_DPTR + 1); // tile high byte (palette | 1)
      a.bind(chars);
      a.i(INY); a.b(LDA_INDY, ZP_TMP); a.w(STA_ABS, 0x0002);
      a.b(LDA_ZP, ZP_DPTR + 1); a.w(STA_ABS, 0x0003);
      a.b(DEC_ZP, ZP_DPTR); a.br(BNE, chars);
      a.i(INY);                                      // pointer += line size
      a.i(TYA); a.i(CLC); a.b(ADC_ZP, ZP_TMP); a.b(STA_ZP, ZP_TMP);
      { L nc = a.label(); a.br(BCC, nc); a.b(INC_ZP, ZP_TMP + 1); a.bind(nc); }
      a.wl(JMP, line);
      a.bind(end); }
    a.w(LDA_ABS, 0x0000);
  }
  a.b(ST0, 0x05); a.b(ST1, opt.hucard ? 0x88 : 0x08); a.b(ST2, 0x00); // VDC: (background +) VBlank interrupt
  a.w(STZ_ABS, TIMER);                               // timer reload 0 -> 7.16 MHz / 1024
  a.b(LDA_IMM, 0x01); a.w(STA_ABS, TIMER + 1);       // timer on
  a.b(LDA_IMM, 0x01); a.w(STA_ABS, IRQ_MASK);        // IRQ2 off, VBlank and timer on
  a.i(CLI);
  { L idle = a.hereLabel(); a.br(BRA, idle); }

  // ---- helpers -----------------------------------------------------------------
  // getb: A = next stream byte (flags are NOT those of A)
  a.bind(getb);
  a.b(LDA_IND, ZP_SPTR);
  a.b(INC_ZP, ZP_SPTR);
  { L r = a.label(); a.br(BNE, r); a.b(INC_ZP, ZP_SPTR + 1); a.bind(r); }
  a.i(RTS);

  a.bind(mapstream);
  a.b(LDA_ZP, ZP_SBANK); a.b(TAM, 0x08);       // MPR3
  a.i(INC_A); a.b(TAM, 0x10);                  // MPR4
  a.i(RTS);

  a.bind(stub);
  a.i(RTI);

  // ---- VBlank: one frame of the stream ----------------------------------------
  L next = a.label(), done = a.label(), notreg = a.label(), cmd = a.label(), jump = a.label();
  L wave = a.label(), dstart = a.label(), dstep = a.label(), dstop = a.label();
  a.bind(irq1);
  a.i(PHA); a.i(PHX); a.i(PHY);
  a.w(LDA_ABS, 0x0000);                        // acknowledge the VDC interrupt
  a.i(CLI);                                    // let the timer (DDA) interrupt us
  a.b(LDA_ZP, ZP_WAIT);
  { L run = a.label(); a.br(BEQ, run); a.b(DEC_ZP, ZP_WAIT); a.wl(JMP, done); a.bind(run); }

  a.bind(next);
  // Keep the pointer in the first bank of the 2-bank window (commands are <= 4 bytes).
  a.b(LDA_ZP, ZP_SPTR + 1); a.b(CMP_IMM, 0x80);
  { L ok = a.label(); a.br(BCC, ok);
    a.i(SEC); a.b(SBC_IMM, 0x20); a.b(STA_ZP, ZP_SPTR + 1);
    a.b(INC_ZP, ZP_SBANK); a.wl(JSR, mapstream);
    a.bind(ok); }
  a.wl(JSR, getb);
  a.b(CMP_IMM, 0x0A); a.br(BCS, notreg);
  // $00-$09: register write
  a.i(TAX);
  a.wl(JSR, getb);
  a.b(CPX_IMM, 0x00);
  { L ns = a.label(); a.br(BNE, ns); a.b(STA_ZP, ZP_PSEL); a.bind(ns); }
  a.w(STA_ABSX, PSG);
  a.wl(JMP, next);

  a.bind(notreg);
  a.b(CMP_IMM, 0x80); a.br(BCC, cmd);
  a.b(CMP_IMM, 0xFF); a.br(BEQ, jump);
  a.b(AND_IMM, 0x7F); a.b(STA_ZP, ZP_WAIT);    // end of frame (+ empty frames)
  a.wl(JMP, done);

  a.bind(jump);
  a.wl(JSR, getb); a.i(PHA);
  a.wl(JSR, getb); a.i(PHA);
  a.wl(JSR, getb); a.b(STA_ZP, ZP_SBANK);
  a.i(PLA); a.b(STA_ZP, ZP_SPTR + 1);
  a.i(PLA); a.b(STA_ZP, ZP_SPTR);
  a.wl(JSR, mapstream);
  a.wl(JMP, next);

  a.bind(cmd);
  a.i(PHA); a.b(AND_IMM, 0x07); a.i(TAX); a.i(PLA);   // X = channel
  a.b(AND_IMM, 0xF8);
  a.b(CMP_IMM, SC_WAVE); a.br(BEQ, wave);
  a.b(CMP_IMM, SC_DDA_START); a.br(BEQ, dstart);
  a.b(CMP_IMM, SC_DDA_STEP); a.br(BEQ, dstep);
  a.b(CMP_IMM, SC_DDA_STOP); a.br(BEQ, dstop);
  a.wl(JMP, next);

  // wave upload: pointer = kWaveTable + index * 32
  a.bind(wave);
  a.wl(JSR, getb);
  a.i(PHA);
  a.i(LSR_A); a.i(LSR_A); a.i(LSR_A);
  a.i(CLC); a.b(ADC_IMM, (uint8_t)(kWaveTable >> 8)); a.b(STA_ZP, ZP_TMP + 1);
  a.i(PLA);
  a.i(ASL_A); a.i(ASL_A); a.i(ASL_A); a.i(ASL_A); a.i(ASL_A);
  a.b(STA_ZP, ZP_TMP);
  a.b(LDA_IMM, 0x40); a.w(STA_ABS, PSG + 4);   // DDA on -> off rewinds the wave pointer
  a.w(STZ_ABS, PSG + 4);
  a.b(LDY_IMM, 0x00);
  { L wl = a.hereLabel();
    a.b(LDA_INDY, ZP_TMP); a.w(STA_ABS, PSG + 6);
    a.i(INY); a.b(CPY_IMM, 32); a.br(BNE, wl); }
  a.wl(JMP, next);

  a.bind(dstop);
  a.b(STZ_ZPX, ZP_D_ON);
  a.wl(JMP, next);

  a.bind(dstep);
  a.i(SEI);                                    // both step bytes change together
  a.wl(JSR, getb); a.b(STA_ZPX, ZP_D_STLO);
  a.wl(JSR, getb); a.b(STA_ZPX, ZP_D_STHI);
  a.i(CLI);
  a.wl(JMP, next);

  a.bind(dstart);
  a.b(STZ_ZPX, ZP_D_ON);                       // off while the position is set up
  a.wl(JSR, getb); a.b(STA_ZPX, ZP_D_SMP); a.i(TAY);
  a.w(LDA_ABSY, table(ST_START_BANK)); a.b(STA_ZPX, ZP_D_BANK);
  a.w(LDA_ABSY, table(ST_START_LO)); a.b(STA_ZPX, ZP_D_LO);
  a.w(LDA_ABSY, table(ST_START_HI)); a.b(STA_ZPX, ZP_D_HI);
  a.b(STZ_ZPX, ZP_D_FRAC);
  a.wl(JSR, getb); a.b(STA_ZPX, ZP_D_STLO);
  a.wl(JSR, getb); a.b(STA_ZPX, ZP_D_STHI);
  a.b(LDA_IMM, 0x01); a.b(STA_ZPX, ZP_D_ON);
  a.wl(JMP, next);

  a.bind(done);
  a.i(PLY); a.i(PLX); a.i(PLA);
  a.i(RTI);

  // ---- timer: DDA sample output -------------------------------------------------
  L ch = a.label(), nextch = a.label(), play = a.label(), ended = a.label(), stop = a.label();
  a.bind(timer);
  a.i(PHA); a.i(PHX); a.i(PHY);
  a.w(STA_ABS, IRQ_ACK);                       // acknowledge the timer interrupt
  a.b(LDX_IMM, 0x00);
  a.bind(ch);
  a.b(LDA_ZPX, ZP_D_ON); a.br(BEQ, nextch);
  a.b(LDY_ZPX, ZP_D_SMP);
  // position >= end of the sample?
  a.b(LDA_ZPX, ZP_D_BANK); a.w(CMP_ABSY, table(ST_END_BANK)); a.br(BCC, play); a.br(BNE, ended);
  a.b(LDA_ZPX, ZP_D_HI); a.w(CMP_ABSY, table(ST_END_HI)); a.br(BCC, play); a.br(BNE, ended);
  a.b(LDA_ZPX, ZP_D_LO); a.w(CMP_ABSY, table(ST_END_LO)); a.br(BCC, play);
  a.bind(ended);
  a.w(LDA_ABSY, table(ST_LOOP_BANK)); a.b(CMP_IMM, 0xFF); a.br(BEQ, stop);
  a.b(STA_ZPX, ZP_D_BANK);
  a.w(LDA_ABSY, table(ST_LOOP_LO)); a.b(STA_ZPX, ZP_D_LO);
  a.w(LDA_ABSY, table(ST_LOOP_HI)); a.b(STA_ZPX, ZP_D_HI);
  a.b(STZ_ZPX, ZP_D_FRAC);
  a.br(BRA, play);
  a.bind(stop);
  a.b(STZ_ZPX, ZP_D_ON);
  a.br(BRA, nextch);
  a.bind(play);
  a.b(LDA_ZPX, ZP_D_BANK); a.b(TAM, 0x20);     // sample bank at MPR5
  a.b(LDA_ZPX, ZP_D_LO); a.b(STA_ZP, ZP_DPTR);
  a.b(LDA_ZPX, ZP_D_HI); a.b(STA_ZP, ZP_DPTR + 1);
  a.w(STX_ABS, PSG);                           // select the channel
  a.b(LDA_IND, ZP_DPTR); a.w(STA_ABS, PSG + 6);
  // position += step (8.8); crossing $C000 moves to the next bank
  a.i(CLC);
  a.b(LDA_ZPX, ZP_D_FRAC); a.b(ADC_ZPX, ZP_D_STLO); a.b(STA_ZPX, ZP_D_FRAC);
  a.b(LDA_ZPX, ZP_D_LO); a.b(ADC_ZPX, ZP_D_STHI); a.b(STA_ZPX, ZP_D_LO);
  a.br(BCC, nextch);
  a.b(INC_ZPX, ZP_D_HI);
  a.b(LDA_ZPX, ZP_D_HI); a.b(CMP_IMM, 0xC0); a.br(BCC, nextch);
  a.b(LDA_IMM, (uint8_t)(kSampleWindow >> 8)); a.b(STA_ZPX, ZP_D_HI);
  a.b(INC_ZPX, ZP_D_BANK);
  a.bind(nextch);
  a.i(INX); a.b(CPX_IMM, 6); a.br(BNE, ch);
  a.b(LDA_ZP, ZP_PSEL); a.w(STA_ABS, PSG);     // restore the frame code's channel select
  a.i(PLY); a.i(PLX); a.i(PLA);
  a.i(RTI);

  // ---- HuCard data: font (bit order reversed for the VDC) and screen text ----
  a.bind(fontData);
  if (opt.hucard) {
    for (int c = 32; c < 128; ++c)
      for (int r = 0; r < 8; ++r) {
        uint8_t v = (uint8_t)font8x8_basic[c][r], o = 0;
        for (int b = 0; b < 8; ++b) if (v & (1 << b)) o |= (uint8_t)(0x80 >> b);
        a.db(o);
      }
  }
  a.bind(textData);
  for (size_t k = 0; opt.hucard && k < opt.screen.size(); ++k) {
    const ScreenText& t = opt.screen[k];
    std::string s = t.text.substr(0, t.col < 32 ? (size_t)(32 - t.col) : 0);
    if (s.empty() || t.row < 0 || t.row >= 32 || t.col < 0) continue;
    const uint16_t addr = (uint16_t)(t.row * 32 + t.col);
    a.db((uint8_t)s.size());
    a.db((uint8_t)addr); a.db((uint8_t)(addr >> 8));
    a.db(t.highlight ? 0x01 : 0x11);                 // palette 0 (orange) / 1 (white), tile 256+
    for (size_t i = 0; i < s.size(); ++i) {
      unsigned char ch = (unsigned char)s[i];
      a.db((uint8_t)((ch >= 32 && ch < 128 ? ch : '?') - 32));
    }
  }
  a.db(0);

  if (a.here() > kWaveTable) { *err = "driver: code too large"; return false; }

  // ---- data tables (filled by the exporter) and CPU vectors ---------------------
  while (a.here() != 0xFFF6) a.db(0x00);
  a.dwl(stub);   // IRQ2
  a.dwl(irq1);   // IRQ1 (VDC, VBlank)
  a.dwl(timer);  // timer
  a.dwl(stub);   // NMI
  a.dwl(reset);  // reset
  if (!a.finish(err)) return false;
  if (a.code().size() != (size_t)kBankSize) { *err = "driver: bad bank size"; return false; }
  *bank = a.code();
  *resetAddr = a.addressOf(reset);
  return true;
}

} // namespace pce
