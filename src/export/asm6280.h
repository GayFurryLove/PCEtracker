// PCEtracker - tiny in-memory HuC6280 assembler used to build the sound driver.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// No external assembler is needed to build or export: the driver source is
// the C++ code in driver6280.cpp, written with these mnemonics.

#ifndef PCETRACKER_ASM6280_H
#define PCETRACKER_ASM6280_H

#include <stdint.h>
#include <string>
#include <vector>

namespace pce {

namespace op6280 {
enum {
  // flags / misc
  SEI = 0x78, CLI = 0x58, CLD = 0xD8, CSH = 0xD4, CLC = 0x18, SEC = 0x38, NOP = 0xEA,
  // load / store
  LDA_IMM = 0xA9, LDA_ZP = 0xA5, LDA_ZPX = 0xB5, LDA_ABS = 0xAD, LDA_ABSX = 0xBD, LDA_ABSY = 0xB9,
  LDA_IND = 0xB2, LDA_INDY = 0xB1,
  LDX_IMM = 0xA2, LDX_ZP = 0xA6, LDY_IMM = 0xA0, LDY_ZPX = 0xB4,
  STA_ZP = 0x85, STA_ZPX = 0x95, STA_ABS = 0x8D, STA_ABSX = 0x9D,
  STX_ZP = 0x86, STX_ABS = 0x8E,
  STZ_ZP = 0x64, STZ_ZPX = 0x74, STZ_ABS = 0x9C,
  // transfers / stack
  TAX = 0xAA, TAY = 0xA8, TXA = 0x8A, TYA = 0x98, TXS = 0x9A,
  PHA = 0x48, PLA = 0x68, PHX = 0xDA, PLX = 0xFA, PHY = 0x5A, PLY = 0x7A,
  // arithmetic
  INC_A = 0x1A, INC_ZP = 0xE6, INC_ZPX = 0xF6, DEC_ZP = 0xC6, INX = 0xE8, INY = 0xC8, DEX = 0xCA, DEY = 0x88,
  ADC_IMM = 0x69, ADC_ZP = 0x65, ADC_ZPX = 0x75, SBC_IMM = 0xE9,
  CMP_IMM = 0xC9, CMP_ZP = 0xC5, CMP_ABSY = 0xD9, CPX_IMM = 0xE0, CPY_IMM = 0xC0,
  AND_IMM = 0x29, ORA_IMM = 0x09, ASL_A = 0x0A, LSR_A = 0x4A,
  // flow
  BCC = 0x90, BCS = 0xB0, BEQ = 0xF0, BNE = 0xD0, BPL = 0x10, BMI = 0x30, BRA = 0x80,
  JMP = 0x4C, JSR = 0x20, RTS = 0x60, RTI = 0x40,
  // HuC6280 only
  TAM = 0x53, ST0 = 0x03, ST1 = 0x13, ST2 = 0x23
};
}

class Asm6280 {
public:
  typedef int Label;
  explicit Asm6280(uint16_t org) : org_(org) {}

  Label label() { labels_.push_back(-1); return (Label)labels_.size() - 1; }
  void bind(Label l) { labels_[(size_t)l] = here(); }
  Label hereLabel() { Label l = label(); bind(l); return l; }
  uint16_t here() const { return (uint16_t)(org_ + code_.size()); }

  void db(uint8_t v) { code_.push_back(v); }
  void i(uint8_t op) { db(op); }                                   // implied
  void b(uint8_t op, uint8_t v) { db(op); db(v); }                 // immediate / zero page
  void w(uint8_t op, uint16_t a) { db(op); db((uint8_t)a); db((uint8_t)(a >> 8)); } // absolute
  void wl(uint8_t op, Label l) { db(op); fix(l, false); db(0); db(0); }             // absolute -> label
  void br(uint8_t op, Label l) { db(op); fix(l, true); db(0); }                     // relative branch
  void dwl(Label l) { fix(l, false); db(0); db(0); }                                // .dw label
  void lo(uint8_t op, Label l) { db(op); fix(l, false, 1); db(0); }                 // #<label
  void hi(uint8_t op, Label l) { db(op); fix(l, false, 2); db(0); }                 // #>label
  void padTo(uint16_t addr) { while (here() != addr && code_.size() < 0x10000) db(0xFF); }

  // Resolves labels. Fails on unbound labels or branches out of range.
  bool finish(std::string* err) {
    for (size_t k = 0; k < fixes_.size(); ++k) {
      const Fix& f = fixes_[k];
      int target = labels_[(size_t)f.label];
      if (target < 0) { *err = "driver: unbound label"; return false; }
      if (f.part == 1) { code_[f.at] = (uint8_t)target; continue; }
      if (f.part == 2) { code_[f.at] = (uint8_t)(target >> 8); continue; }
      if (f.relative) {
        int from = org_ + (int)f.at + 1;
        int d = target - from;
        if (d < -128 || d > 127) { *err = "driver: branch out of range"; return false; }
        code_[f.at] = (uint8_t)d;
      } else {
        code_[f.at] = (uint8_t)target;
        code_[f.at + 1] = (uint8_t)(target >> 8);
      }
    }
    return true;
  }

  uint16_t addressOf(Label l) const { return (uint16_t)labels_[(size_t)l]; }
  const std::vector<uint8_t>& code() const { return code_; }

private:
  struct Fix { size_t at; Label label; bool relative; int part; }; // part: 0 word, 1 low, 2 high
  void fix(Label l, bool rel, int part = 0) { Fix f = { code_.size(), l, rel, part }; fixes_.push_back(f); }

  uint16_t org_;
  std::vector<uint8_t> code_;
  std::vector<int> labels_;
  std::vector<Fix> fixes_;
};

} // namespace pce

#endif
