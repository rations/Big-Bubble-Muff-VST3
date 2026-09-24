// BigBubbleMuff — top-level DSP engine (JUCE-free build).
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Models the four-stage Russian "Bubble Font" Big Muff Pi as a chain of nodal
// sub-circuits, oversampled around the nonlinear clipping core. The public
// interface is host-facing and real-time-safe: prepare() does all allocation,
// process() and setControls() never allocate, lock, or perform I/O.
//
// This is the Linux engine with the framework taken out: same circuit files, same
// chain, same constants. Keep it a recognisable transliteration of its Linux
// counterpart so a fix to either can be moved across by hand.
#pragma once

#include "dsp/AudioMath.h"

#include <memory>

namespace bbmh {

// Smoothed, normalised control values handed from the message thread to audio.
struct Controls {
  float sustain = 0.75f; // 0..1, R24 drive into the clippers
  float tone = 0.5f;     // 0..1, R23 bass<->treble blend
  float volume = 0.5f;   // 0..1, R26 output divider
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

  // Allocates and sizes all state. Call from setupProcessing (message thread).
  void prepare(const ProcessSpec &spec);

  // Resets filter/oversampler state without reallocating.
  void reset();

  // Processes one block in place. The pedal is mono: the input is downmixed to a
  // single channel, the circuit is solved once, and the result is fanned out to
  // every output channel (dual-mono on a stereo bus). Real-time-safe.
  void process(float *const *channels, std::size_t numChannels, std::size_t numSamples);

  // Same, reading from a separate input (hosts may hand out-of-place buffers).
  void process(const float *const *in, float *const *out, std::size_t numChannels,
               std::size_t numSamples);

  // Pushes new control values (real-time-safe; smoothed internally).
  void setControls(const Controls &c);

  int getOversamplingLatencySamples() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace bbmh
