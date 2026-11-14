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

bool buildDriverBank(uint8_t streamBank, std::vector<uint8_t>* bank, uint16_t* resetAddr,
                     std::string* err) {
  using namespace op6280;
  Asm6280 a(kDriverOrg);
  typedef Asm6280::Label L;
  L reset = a.label(), irq1 = a.label(), timer = a.label(), stub = a.label();
  L getb = a.label(), mapstream = a.label();

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
  a.b(ST0, 0x05); a.b(ST1, 0x08); a.b(ST2, 0x00);   // VDC control: VBlank interrupt on
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
