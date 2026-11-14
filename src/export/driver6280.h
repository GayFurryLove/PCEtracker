// PCEtracker - the PC Engine sound driver (HuC6280 machine code) and its data layout.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#ifndef PCETRACKER_DRIVER6280_H
#define PCETRACKER_DRIVER6280_H

#include <stdint.h>
#include <string>
#include <vector>

namespace pce {

// --- ROM image layout (8 KiB banks) -------------------------------------------
//   bank 0            driver code, wave table, sample tables, CPU vectors (at $E000)
//   bank 1..S         DDA sample data (5-bit values, one per byte)
//   bank S+1..        frame stream
const int kBankSize = 0x2000;
const uint16_t kDriverOrg = 0xE000;        // bank 0 is mapped at MPR7
const uint16_t kWaveTable = 0xF000;        // 64 waves x 32 bytes
const uint16_t kSampleTables = 0xF800;     // 9 tables x 64 entries, see below
const uint16_t kStreamWindow = 0x6000;     // stream banks n, n+1 at MPR3/MPR4
const uint16_t kSampleWindow = 0xA000;     // current sample bank at MPR5
enum SampleTable { ST_START_BANK = 0, ST_START_LO, ST_START_HI, ST_END_BANK, ST_END_LO, ST_END_HI,
                   ST_LOOP_BANK, ST_LOOP_LO, ST_LOOP_HI, ST_COUNT }; // loop bank $FF = one shot

// --- frame stream commands ----------------------------------------------------
//   $00-$09 vv     write vv to PSG register $0800+n
//   $10 ww         upload wave ww to the selected channel: $0804=$40, $0804=$00, 32 x $0806
//   $20+c ss ll hh start DDA sample ss on channel c with 8.8 step hh.ll
//   $28+c ll hh    change the DDA step of channel c
//   $30+c          stop DDA on channel c
//   $80+n          end of frame, then n more frames without commands (n <= $7E)
//   $FF ll hh bb   continue at address hh.ll (in the $6000 window) of bank bb
enum StreamCmd { SC_WAVE = 0x10, SC_DDA_START = 0x20, SC_DDA_STEP = 0x28, SC_DDA_STOP = 0x30,
                 SC_END_FRAME = 0x80, SC_JUMP = 0xFF };
const int kMaxWaitFrames = 0x7E;

// Builds bank 0 (kBankSize bytes): code and vectors; the wave and sample
// tables are left zeroed for the exporter to fill. `streamBank` is the bank
// where the stream starts (at kStreamWindow).
bool buildDriverBank(uint8_t streamBank, std::vector<uint8_t>* bank, uint16_t* resetAddr,
                     std::string* err);

} // namespace pce

#endif
