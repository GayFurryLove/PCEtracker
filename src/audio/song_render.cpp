// PCEtracker - audio renderer.
// Copyright (C) 2026 PCEtracker contributors. GPL-2.0-or-later, see LICENSE.

#include "audio/song_render.h"
#include "audio/wav_writer.h"

namespace pce {

SongRenderer::SongRenderer(const Song& song, PsgRevision rev, int sampleRate)
  : player_(song), psg_(rev, sampleRate) {}

void SongRenderer::write(uint8_t reg, uint8_t value) { psg_.write(reg, value); }

bool SongRenderer::renderTick(std::vector<int16_t>& out) {
  // All register writes of a tick happen at the start of the frame (the driver
  // runs in the VBlank interrupt), then the chip plays for one frame.
  bool more = player_.tick(*this);
  psg_.advance(kFrameClocks);
  size_t n = psg_.available();
  size_t at = out.size();
  out.resize(at + n * 2);
  if (n) psg_.read(&out[at], n);
  return more;
}

bool renderSongToWav(const Song& song, const RenderOptions& opt, const char* path,
                     std::string* error, double* seconds) {
  if (opt.pattern >= (int)song.patterns.size()) { *error = "no such pattern"; return false; }
  SongRenderer r(song, opt.revision, opt.sampleRate);
  WavWriter wav;
  if (!wav.open(path, opt.sampleRate, opt.bits)) { *error = std::string("cannot write ") + path; return false; }

  Player& p = r.player();
  if (opt.pattern >= 0) p.playPattern(opt.pattern);
  else p.playSong();

  // Hard cap: one hour of 60 Hz ticks, in case a song never ends (e.g. B00 on order 0 with loops).
  const uint64_t maxTicks = 60ull * 60 * 60;
  std::vector<int16_t> buf;
  buf.reserve((size_t)opt.sampleRate / 30 * 2);
  uint64_t frames = 0;
  int passes = 0;
  // A pattern renders once; a song renders once plus `loops` extra passes.
  const int maxPasses = opt.pattern >= 0 ? 0 : opt.loops;

  while (p.ticksPlayed() < maxTicks) {
    buf.clear();
    r.renderTick(buf);
    wav.write(buf.empty() ? 0 : &buf[0], buf.size() / 2);
    frames += buf.size() / 2;
    if (p.songLooped()) {
      p.clearLooped();
      if (++passes > maxPasses) break;
    }
  }

  // Release: silence the channels and let the tail ring out.
  p.stop();
  const int tailTicks = (int)(opt.fadeSeconds * 60.0);
  for (int i = 0; i < tailTicks || i == 0; ++i) {
    buf.clear();
    r.renderTick(buf);
    wav.write(buf.empty() ? 0 : &buf[0], buf.size() / 2);
    frames += buf.size() / 2;
  }

  if (!wav.close()) { *error = std::string("write error: ") + path; return false; }
  if (seconds) *seconds = (double)frames / opt.sampleRate;
  return true;
}

} // namespace pce
