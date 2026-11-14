// PCEtracker - minimal PC Engine machine for running HES files in tests.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "export/hes_machine.h"

#include <stdio.h>
#include <string.h>

namespace pce {

namespace {
const uint32_t kFrameCycles = 119665; // 263 lines * 1365 master clocks / 3 (59.826 Hz)
const uint32_t kTimerCycles = 1024;
enum { FC = 0x01, FZ = 0x02, FI = 0x04, FD = 0x08, FB = 0x10, FT = 0x20, FV = 0x40, FN = 0x80 };
uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
}

HesMachine::HesMachine()
  : romBase_(0), a_(0), x_(0), y_(0), s_(0xFF), p_(FI), pc_(0), cycles_(0), frameStart_(0), frame_(0),
    timerOn_(false), timerIrq_(false), timerReload_(0), timerCount_(0), prescale_(0),
    irqMask_(0), irq1_(false), vdcReg_(0), vdcControl_(0), out_(0) {
  memset(ram_, 0, sizeof ram_);
  memset(mpr_, 0, sizeof mpr_);
}

bool HesMachine::load(const std::vector<uint8_t>& hes, std::string* err) {
  if (hes.size() < 0x20 || memcmp(&hes[0], "HESM", 4) || memcmp(&hes[0x10], "DATA", 4)) {
    *err = "not a HES file";
    return false;
  }
  uint32_t size = rd32(&hes[0x14]);
  romBase_ = rd32(&hes[0x18]);
  if (size > hes.size() - 0x20) size = (uint32_t)(hes.size() - 0x20);
  rom_.assign(hes.begin() + 0x20, hes.begin() + 0x20 + size);
  for (int i = 0; i < 8; ++i) mpr_[i] = hes[8 + (size_t)i];
  pc_ = (uint16_t)(hes[6] | (hes[7] << 8));
  a_ = hes[5]; // song number
  s_ = 0xFF;
  p_ = FI;
  return true;
}

uint8_t HesMachine::rd(uint16_t a) {
  const uint8_t bank = mpr_[a >> 13];
  const uint32_t off = a & 0x1FFF;
  if (bank == 0xFF) {
    switch (off & 0x1C00) {
      case 0x0000: // VDC: reading the status acknowledges its interrupt
        if ((off & 3) == 0) { irq1_ = false; return 0x20; }
        return 0;
      case 0x0C00: return timerCount_;
      case 0x1400:
        if ((off & 3) == 2) return irqMask_;
        if ((off & 3) == 3) return (uint8_t)((timerIrq_ ? 4 : 0) | (irq1_ ? 2 : 0));
        return 0;
      default: return 0xFF; // PSG is write-only
    }
  }
  if (bank >= 0xF8 && bank <= 0xFB) return ram_[off];
  const uint32_t addr = (uint32_t)bank * 0x2000 + off;
  if (addr >= romBase_ && addr - romBase_ < rom_.size()) return rom_[addr - romBase_];
  return 0xFF;
}

void HesMachine::wr(uint16_t a, uint8_t v) {
  const uint8_t bank = mpr_[a >> 13];
  const uint32_t off = a & 0x1FFF;
  if (bank == 0xFF) {
    switch (off & 0x1C00) {
      case 0x0000:
        if ((off & 3) == 0) vdcReg_ = v & 0x1F;
        else if ((off & 3) == 2 && vdcReg_ == 5) vdcControl_ = v;
        break;
      case 0x0800:
        if (out_) {
          PsgWrite w;
          w.frame = frame_;
          w.cycle = (uint32_t)(cycles_ - frameStart_);
          w.reg = (uint8_t)(off & 0x0F);
          w.value = v;
          w.timer = !isr_.empty() && isr_.back() == 1;
          out_->push_back(w);
        }
        break;
      case 0x0C00:
        if ((off & 1) == 0) {
          timerReload_ = v & 0x7F;
        } else {
          const bool on = (v & 1) != 0;
          if (on && !timerOn_) { timerCount_ = timerReload_; prescale_ = 0; }
          timerOn_ = on;
        }
        break;
      case 0x1400:
        if ((off & 3) == 2) irqMask_ = v & 7;
        else if ((off & 3) == 3) timerIrq_ = false;
        break;
      default: break;
    }
    return;
  }
  if (bank >= 0xF8 && bank <= 0xFB) ram_[off] = v;
  // writes to ROM are ignored
}

void HesMachine::setNZ(uint8_t v) {
  p_ = (uint8_t)((p_ & ~(FN | FZ)) | (v & 0x80) | (v ? 0 : FZ));
}

void HesMachine::compare(uint8_t r, uint8_t v) {
  p_ = (uint8_t)((p_ & ~FC) | (r >= v ? FC : 0));
  setNZ((uint8_t)(r - v));
}

void HesMachine::adc(uint8_t v) {
  const unsigned sum = (unsigned)a_ + v + (p_ & FC);
  p_ = (uint8_t)(p_ & ~(FC | FV));
  if (sum > 0xFF) p_ |= FC;
  if (~(a_ ^ v) & (a_ ^ sum) & 0x80) p_ |= FV;
  a_ = (uint8_t)sum;
  setNZ(a_);
}

void HesMachine::branch(bool taken) {
  const int8_t d = (int8_t)fetch();
  addCycles(2);
  if (taken) { pc_ = (uint16_t)(pc_ + d); addCycles(2); }
}

void HesMachine::addCycles(uint32_t n) {
  cycles_ += n;
  if (!timerOn_) return;
  prescale_ += n;
  while (prescale_ >= kTimerCycles) {
    prescale_ -= kTimerCycles;
    if (timerCount_ == 0) { timerCount_ = timerReload_; timerIrq_ = true; }
    else --timerCount_;
  }
}

void HesMachine::interrupt(uint16_t vector, bool isTimer) {
  push((uint8_t)(pc_ >> 8));
  push((uint8_t)pc_);
  push((uint8_t)(p_ & ~FB));
  p_ = (uint8_t)((p_ | FI) & ~FD);
  pc_ = (uint16_t)(rd(vector) | (rd((uint16_t)(vector + 1)) << 8));
  isr_.push_back(isTimer ? 1 : 0);
  addCycles(8);
}

bool HesMachine::runFrame(std::vector<PsgWrite>* out) {
  if (!error_.empty()) return false;
  out_ = out;
  frameStart_ = (uint64_t)frame_ * kFrameCycles;
  const uint64_t end = frameStart_ + kFrameCycles;
  if (vdcControl_ & 0x08) irq1_ = true; // VBlank
  while (cycles_ < end) {
    const bool timerReady = timerIrq_ && !(irqMask_ & 4);
    const bool irq1Ready = irq1_ && !(irqMask_ & 2);
    if (!(p_ & FI)) {
      if (timerReady) { interrupt(0xFFFA, true); continue; }
      if (irq1Ready) { interrupt(0xFFF8, false); continue; }
    }
    // Idle loop ("bra *"): skip ahead to the next event instead of spinning.
    if (rd(pc_) == 0x80 && rd((uint16_t)(pc_ + 1)) == 0xFE) {
      uint64_t n = end - cycles_;
      if (timerOn_ && kTimerCycles - prescale_ < n) n = kTimerCycles - prescale_;
      if (n < 4) n = 4;
      addCycles((uint32_t)n);
      continue;
    }
    if (!step()) { out_ = 0; return false; }
  }
  ++frame_;
  out_ = 0;
  return true;
}

bool HesMachine::step() {
  const uint16_t at = pc_;
  const uint8_t op = fetch();
  #define ZP() ((uint16_t)(0x2000 | fetch()))
  #define ZPX() ((uint16_t)(0x2000 | ((fetch() + x_) & 0xFF)))
  switch (op) {
    // --- flags ---
    case 0x78: p_ |= FI; addCycles(2); break;                         // SEI
    case 0x58: p_ = (uint8_t)(p_ & ~FI); addCycles(2); break;         // CLI
    case 0xD8: p_ = (uint8_t)(p_ & ~FD); addCycles(2); break;         // CLD
    case 0x18: p_ = (uint8_t)(p_ & ~FC); addCycles(2); break;         // CLC
    case 0x38: p_ |= FC; addCycles(2); break;                         // SEC
    case 0xD4: addCycles(3); break;                                   // CSH
    case 0xEA: addCycles(2); break;                                   // NOP
    // --- LDA ---
    case 0xA9: a_ = fetch(); setNZ(a_); addCycles(2); break;
    case 0xA5: a_ = rd(ZP()); setNZ(a_); addCycles(4); break;
    case 0xB5: a_ = rd(ZPX()); setNZ(a_); addCycles(4); break;
    case 0xAD: a_ = rd(fetch16()); setNZ(a_); addCycles(5); break;
    case 0xBD: a_ = rd((uint16_t)(fetch16() + x_)); setNZ(a_); addCycles(5); break;
    case 0xB9: a_ = rd((uint16_t)(fetch16() + y_)); setNZ(a_); addCycles(5); break;
    case 0xB2: case 0xB1: {                                           // LDA (zp) / (zp),y
      const uint8_t z = fetch();
      uint16_t p = (uint16_t)(rd((uint16_t)(0x2000 | z)) | (rd((uint16_t)(0x2000 | ((z + 1) & 0xFF))) << 8));
      if (op == 0xB1) p = (uint16_t)(p + y_);
      a_ = rd(p); setNZ(a_); addCycles(7); break;
    }
    // --- LDX / LDY ---
    case 0xA2: x_ = fetch(); setNZ(x_); addCycles(2); break;
    case 0xA6: x_ = rd(ZP()); setNZ(x_); addCycles(4); break;
    case 0xA0: y_ = fetch(); setNZ(y_); addCycles(2); break;
    case 0xB4: y_ = rd(ZPX()); setNZ(y_); addCycles(4); break;
    // --- stores ---
    case 0x85: wr(ZP(), a_); addCycles(4); break;
    case 0x95: wr(ZPX(), a_); addCycles(4); break;
    case 0x8D: wr(fetch16(), a_); addCycles(5); break;
    case 0x9D: wr((uint16_t)(fetch16() + x_), a_); addCycles(5); break;
    case 0x86: wr(ZP(), x_); addCycles(4); break;
    case 0x8E: wr(fetch16(), x_); addCycles(5); break;
    case 0x64: wr(ZP(), 0); addCycles(4); break;
    case 0x74: wr(ZPX(), 0); addCycles(4); break;
    case 0x9C: wr(fetch16(), 0); addCycles(5); break;
    // --- transfers, stack ---
    case 0xAA: x_ = a_; setNZ(x_); addCycles(2); break;
    case 0xA8: y_ = a_; setNZ(y_); addCycles(2); break;
    case 0x8A: a_ = x_; setNZ(a_); addCycles(2); break;
    case 0x98: a_ = y_; setNZ(a_); addCycles(2); break;
    case 0x9A: s_ = x_; addCycles(2); break;
    case 0x48: push(a_); addCycles(3); break;
    case 0xDA: push(x_); addCycles(3); break;
    case 0x5A: push(y_); addCycles(3); break;
    case 0x68: a_ = pop(); setNZ(a_); addCycles(4); break;
    case 0xFA: x_ = pop(); setNZ(x_); addCycles(4); break;
    case 0x7A: y_ = pop(); setNZ(y_); addCycles(4); break;
    // --- inc / dec ---
    case 0x1A: ++a_; setNZ(a_); addCycles(2); break;
    case 0xE8: ++x_; setNZ(x_); addCycles(2); break;
    case 0xC8: ++y_; setNZ(y_); addCycles(2); break;
    case 0xCA: --x_; setNZ(x_); addCycles(2); break;
    case 0x88: --y_; setNZ(y_); addCycles(2); break;
    case 0xE6: case 0xF6: case 0xC6: {
      const uint16_t p = op == 0xF6 ? ZPX() : ZP();
      const uint8_t v = (uint8_t)(rd(p) + (op == 0xC6 ? -1 : 1));
      wr(p, v); setNZ(v); addCycles(6); break;
    }
    // --- arithmetic, logic ---
    case 0x69: adc(fetch()); addCycles(2); break;
    case 0x65: adc(rd(ZP())); addCycles(4); break;
    case 0x75: adc(rd(ZPX())); addCycles(4); break;
    case 0xE9: adc((uint8_t)~fetch()); addCycles(2); break;           // SBC #
    case 0xC9: compare(a_, fetch()); addCycles(2); break;
    case 0xC5: compare(a_, rd(ZP())); addCycles(4); break;
    case 0xD9: compare(a_, rd((uint16_t)(fetch16() + y_))); addCycles(5); break;
    case 0xE0: compare(x_, fetch()); addCycles(2); break;
    case 0xC0: compare(y_, fetch()); addCycles(2); break;
    case 0x29: a_ &= fetch(); setNZ(a_); addCycles(2); break;
    case 0x09: a_ |= fetch(); setNZ(a_); addCycles(2); break;
    case 0x0A: p_ = (uint8_t)((p_ & ~FC) | (a_ >> 7)); a_ = (uint8_t)(a_ << 1); setNZ(a_); addCycles(2); break;
    case 0x4A: p_ = (uint8_t)((p_ & ~FC) | (a_ & 1)); a_ >>= 1; setNZ(a_); addCycles(2); break;
    // --- flow ---
    case 0x90: branch(!(p_ & FC)); break;
    case 0xB0: branch((p_ & FC) != 0); break;
    case 0xF0: branch((p_ & FZ) != 0); break;
    case 0xD0: branch(!(p_ & FZ)); break;
    case 0x10: branch(!(p_ & FN)); break;
    case 0x30: branch((p_ & FN) != 0); break;
    case 0x80: branch(true); break;
    case 0x4C: pc_ = fetch16(); addCycles(4); break;
    case 0x20: {
      const uint16_t target = fetch16();
      const uint16_t ret = (uint16_t)(pc_ - 1);
      push((uint8_t)(ret >> 8)); push((uint8_t)ret);
      pc_ = target; addCycles(7); break;
    }
    case 0x60: { const uint8_t lo = pop(); pc_ = (uint16_t)((lo | (pop() << 8)) + 1); addCycles(7); break; }
    case 0x40: {
      p_ = pop();
      const uint8_t lo = pop();
      pc_ = (uint16_t)(lo | (pop() << 8));
      if (!isr_.empty()) isr_.pop_back();
      addCycles(7); break;
    }
    // --- HuC6280 ---
    case 0x53: {                                                      // TAM
      const uint8_t mask = fetch();
      for (int i = 0; i < 8; ++i) if (mask & (1 << i)) mpr_[i] = a_;
      addCycles(5); break;
    }
    case 0x03: vdcReg_ = fetch() & 0x1F; addCycles(4); break;         // ST0
    case 0x13: { const uint8_t v = fetch(); if (vdcReg_ == 5) vdcControl_ = v; addCycles(4); break; } // ST1
    case 0x23: fetch(); addCycles(4); break;                          // ST2
    default: {
      char b[80];
      snprintf(b, sizeof b, "CPU: unknown opcode $%02X at $%04X (frame %u)", op, at, frame_);
      error_ = b;
      return false;
    }
  }
  #undef ZP
  #undef ZPX
  return true;
}

} // namespace pce
