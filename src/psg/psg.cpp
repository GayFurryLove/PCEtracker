// PCEtracker - HuC6280 PSG module.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "psg.h"
#include "huc6280_psg_core.h"

#include <cstddef>
#include <math.h>
#include <string.h>

namespace pce {

// 315/88 MHz * 6 = 21.477272 MHz master clock; the PSG core counts at master / 3.
const double Psg::kClockHz = 315000000.0 / 88.0 * 2.0;

namespace {
// Emulate in slices short enough that blip_buf never overflows and int32
// timestamps never wrap: 8192 clocks is about 1.1 ms.
const int32_t kSliceClocks = 8192;
// blip_buf only has to hold one slice (~1.1 ms); it is drained on every flush.
int bufferFramesFor(int sampleRate) { return sampleRate / 100 + 64; }
}

Psg::Psg(PsgRevision revision, int sampleRate)
  : core_(0), revision_(revision), sampleRate_(sampleRate), time_(0), pendingHead_(0) {
  pending_.reserve((size_t)sampleRate * 2);
  core_ = new detail::PCE_PSG(revision == PSG_REV_HUC6280A
                                ? detail::PCE_PSG::REVISION_HUC6280A
                                : detail::PCE_PSG::REVISION_HUC6280);
  for (int i = 0; i < 2; ++i) {
    core_->bb[i] = blip_new(bufferFramesFor(sampleRate));
    blip_set_rates(core_->bb[i], kClockHz, sampleRate);
  }
  setGain(0.5);
  reset();
}

Psg::~Psg() {
  blip_delete(core_->bb[0]);
  blip_delete(core_->bb[1]);
  delete core_;
}

void Psg::reset() {
  core_->Power(time_);
  flush();
  blip_clear(core_->bb[0]);
  blip_clear(core_->bb[1]);
  pending_.clear();
  pendingHead_ = 0;
}

void Psg::setGain(double gain) { core_->SetVolume(gain); }

void Psg::write(uint8_t reg, uint8_t value) {
  core_->Write(time_, reg, value);
}

void Psg::writeChannel(int ch, uint8_t reg, uint8_t value) {
  core_->Write(time_, REG_SELECT, (uint8_t)ch);
  core_->Write(time_, reg, value);
}

void Psg::flush() {
  core_->Update(time_);
  blip_end_frame(core_->bb[0], (unsigned)time_);
  blip_end_frame(core_->bb[1], (unsigned)time_);
  core_->ResetTS(0);
  time_ = 0;

  // Move everything blip_buf produced into pending_.
  int n = blip_samples_avail(core_->bb[0]);
  if (n <= 0) return;
  if (pendingHead_ > 0 && pendingHead_ * 2 >= pending_.size()) {
    pending_.erase(pending_.begin(), pending_.begin() + (std::ptrdiff_t)pendingHead_);
    pendingHead_ = 0;
  }
  size_t at = pending_.size();
  pending_.resize(at + (size_t)n * 2);
  blip_read_samples(core_->bb[0], &pending_[at], n, 1);
  blip_read_samples(core_->bb[1], &pending_[at + 1], n, 1);
}

void Psg::advance(uint32_t clocks) {
  while (clocks > 0) {
    int32_t room = kSliceClocks - time_;
    int32_t step = (clocks < (uint32_t)room) ? (int32_t)clocks : room;
    time_ += step;
    clocks -= (uint32_t)step;
    core_->Update(time_);
    if (time_ >= kSliceClocks) flush();
  }
}

size_t Psg::available() const {
  return (pending_.size() - pendingHead_) / 2;
}

size_t Psg::read(int16_t* out, size_t maxFrames) {
  size_t n = available();
  if (n > maxFrames) n = maxFrames;
  if (n == 0) return 0;
  memcpy(out, &pending_[pendingHead_], n * 2 * sizeof(int16_t));
  pendingHead_ += n * 2;
  if (pendingHead_ == pending_.size()) {
    pending_.clear();
    pendingHead_ = 0;
  }
  return n;
}

size_t Psg::render(int16_t* out, size_t frames) {
  size_t done = 0;
  while (done < frames) {
    done += read(out + done * 2, frames - done);
    if (done >= frames) break;
    // Run one slice, then read what it produced.
    advance((uint32_t)(kSliceClocks - time_));
  }
  return done;
}

uint16_t Psg::channelPeriod(int ch) const { return core_->channel[ch].frequency; }
uint8_t Psg::channelControl(int ch) const { return core_->channel[ch].control; }

void Psg::peekWave(int ch, uint8_t out[32]) const {
  core_->PeekWave((unsigned)ch, 0, 32, out);
}

// Wave mode: one wavetable step every 2*period PSG clocks, 32 steps per cycle.
double psgHzForPeriod(uint16_t period) {
  unsigned p = period ? period : 4096;
  return Psg::kClockHz / (2.0 * 32.0 * p);
}

uint16_t psgPeriodForHz(double hz) {
  if (hz <= 0) return 4095;
  double p = floor(Psg::kClockHz / (2.0 * 32.0 * hz) + 0.5);
  if (p < 1) p = 1;
  if (p > 4095) p = 4095;
  return (uint16_t)p;
}

} // namespace pce
