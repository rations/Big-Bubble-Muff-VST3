// BigBubbleMuff — top-level DSP engine.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Models the four-stage Russian "Bubble Font" Big Muff Pi, oversampled around the
// nonlinear clipping core. The engine is mono, like the pedal: one input jack, one
// circuit, one output jack. Down-mixing and fanning out to a host's bus is the
// plug-in's job (src/plugin/processor.cpp).
//
// Real-time contract: prepare() does every allocation; process(), setControls()
// and reset() never allocate, lock, or perform I/O. Depends on the standard
// library only.
#pragma once

#include <cstddef>
#include <memory>

namespace bbm {

// Normalised control values handed from the plug-in to the engine.
struct Controls {
  float sustain = 0.75f; // 0..1, R24 SUSTAIN pot
  float tone = 0.5f;     // 0..1, R25 TONE pot
  float volume = 0.5f;   // 0..1, R26 VOLUME pot
  float outputTrimDb = 0.0f;
  float gate = 0.4f; // 0..1 pre-gain noise gate threshold (0 = off)
};

class BigMuffPi {
public:
  BigMuffPi();
  ~BigMuffPi();

  BigMuffPi(const BigMuffPi &) = delete;
  BigMuffPi &operator=(const BigMuffPi &) = delete;
  BigMuffPi(BigMuffPi &&) = delete;
  BigMuffPi &operator=(BigMuffPi &&) = delete;

  // Allocates and sizes all state for blocks of up to maxBlock samples, then
  // settles the circuit. Message thread only.
  void prepare(double sampleRate, int maxBlock);

  // Resets filter/oversampler state without reallocating. Real-time-safe.
  void reset();

  // Processes n mono samples. `in` and `out` may alias. Blocks longer than the
  // prepared size are walked in prepared-size chunks. Real-time-safe.
  void process(const float *in, float *out, std::size_t n) noexcept;

  // Pushes new control values (real-time-safe; smoothed internally).
  void setControls(const Controls &c);

  // Oversampler group delay at DC, in base-rate samples (fractional).
  static double latencySamples();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace bbm
