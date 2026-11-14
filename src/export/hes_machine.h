// PCEtracker - minimal PC Engine machine for running HES files in tests.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.
//
// HuC6280 CPU (the instructions the driver uses), bank mapping, RAM, the
// timer, interrupt control, the VBlank interrupt and a PSG write log.
// Any instruction it does not know stops the run with an error, so a wrong
// driver cannot pass silently. This is a verification tool, not an emulator
// for games.

#ifndef PCETRACKER_HES_MACHINE_H
#define PCETRACKER_HES_MACHINE_H

#include <stdint.h>
#include <string>
#include <vector>

namespace pce {

class HesMachine {
public:
  struct PsgWrite {
    uint32_t frame;   // video frame number
    uint32_t cycle;   // CPU cycles (7.16 MHz) since the start of that frame
    uint8_t reg, value;
    bool timer;       // written from the timer interrupt (DDA)
  };

  HesMachine();
  bool load(const std::vector<uint8_t>& hes, std::string* err);
  // Runs one video frame (119210 CPU cycles) and appends the PSG writes.
  bool runFrame(std::vector<PsgWrite>* out);
  const std::string& error() const { return error_; }
  uint32_t frame() const { return frame_; }

private:
  uint8_t rd(uint16_t a);
  void wr(uint16_t a, uint8_t v);
  uint8_t fetch() { return rd(pc_++); }
  uint16_t fetch16() { uint8_t lo = fetch(); return (uint16_t)(lo | (fetch() << 8)); }
  void push(uint8_t v) { ram_[0x100 + s_] = v; --s_; }
  uint8_t pop() { ++s_; return ram_[0x100 + s_]; }
  void setNZ(uint8_t v);
  void compare(uint8_t r, uint8_t v);
  void adc(uint8_t v);
  void branch(bool taken);
  void interrupt(uint16_t vector, bool isTimer);
  void addCycles(uint32_t n);
  bool step();

  std::vector<uint8_t> rom_;
  uint32_t romBase_;       // load address
  uint8_t ram_[0x2000];
  uint8_t mpr_[8];
  uint8_t a_, x_, y_, s_, p_;
  uint16_t pc_;
  uint64_t cycles_, frameStart_;
  uint32_t frame_;
  // timer
  bool timerOn_, timerIrq_;
  uint8_t timerReload_, timerCount_;
  uint32_t prescale_;
  // interrupts, VDC
  uint8_t irqMask_;
  bool irq1_;
  uint8_t vdcReg_, vdcControl_;
  std::vector<uint8_t> isr_; // 1 = timer, 0 = other; innermost last
  std::vector<PsgWrite>* out_;
  std::string error_;
};

} // namespace pce

#endif
