// BigBubbleMuff — pre-gain input noise gate (downward expander / hard gate).
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// A real Big Muff has enormous gain (four cascaded common-emitter stages,
// ~19x each), so it amplifies the noise floor of whatever feeds it by +45..60 dB
// — that is the characteristic idle hiss. A gate placed AFTER the pedal cannot
// tame it: the fuzz compresses the idle hiss and a real note's sustain tail to
// nearly the same level, so no threshold separates them and the gate chatters.
//
// The fix is to gate the CLEAN input BEFORE the gain. The dry guitar has a wide
// dynamic window (playing ~ -20 dBFS vs idle ~ -60 dBFS), so the gate opens
// cleanly when you play and shuts to true silence when you don't. Sustain is
// preserved because the fuzz tail is *driven* by the still-ringing dry string:
// as long as the dry input stays above threshold, the fuzz keeps going.
//
// Detector: a slow mean-square (RMS) follower so it tracks sustained energy, not
// individual broadband-noise peaks. Open/close thresholds give hysteresis; a hold
// counter and a release one-pole avoid chatter. Because the downstream gain is so
// large, even a small residual gate gain leaks audible hiss, so the gain snaps to
// a true mute once it has mostly closed. Stateless of any allocation; RT-safe.
#pragma once

#include <algorithm>
#include <cmath>

namespace bbm {

class NoiseGate {
public:
  void prepare(double sampleRate) {
    const auto onePole = [sampleRate](float ms) {
      return std::exp(-1.0f / static_cast<float>(sampleRate * ms / 1000.0));
    };
    aEnv_ = onePole(kEnvMs);
    aAtk_ = onePole(kAtkMs);
    aRel_ = onePole(kRelMs);
    holdN_ = static_cast<int>(sampleRate * kHoldMs / 1000.0);
    reset();
  }

  void reset() {
    ms_ = 0.0f;
    gain_ = 1.0f; // start open so an immediate first note is never clipped
    holdCtr_ = 0;
  }

  // `amount` in 0..1: 0 disables the gate (raw, authentically hissy Muff); higher
  // raises the threshold. Cheap — call once per block, not per sample.
  void setAmount(float amount) {
    const float a = std::clamp(amount, 0.0f, 1.0f);
    enabled_ = a > 1.0e-3f;
    // Threshold relative to the dry input: ~-70 dBFS (gentle) .. -20 dBFS (tight).
    const float openDb = -70.0f + 50.0f * a;
    openTh_ = std::pow(10.0f, openDb / 20.0f);
    closeTh_ = std::pow(10.0f, (openDb - kHysteresisDb) / 20.0f); // hysteresis
  }

  inline float process(float x) noexcept {
    if (!enabled_) {
      gain_ = 1.0f;
      return x;
    }
    // Slow RMS detector on the dry input.
    const float sq = x * x;
    ms_ = sq + aEnv_ * (ms_ - sq);
    const float env = std::sqrt(ms_);

    // Hysteresis + hold: open above openTh, stay put between thresholds (or while
    // holding), close below closeTh.
    float target = 0.0f;
    if (env > openTh_) {
      target = 1.0f;
      holdCtr_ = holdN_;
    } else if (env > closeTh_ || holdCtr_ > 0) {
      target = gain_;
      if (holdCtr_ > 0)
        --holdCtr_;
    } else {
      target = 0.0f;
    }

    const float coeff = (target > gain_) ? aAtk_ : aRel_;
    gain_ = target + coeff * (gain_ - target);
    if (target <= 0.0f && gain_ < kMuteSnap)
      gain_ = 0.0f; // huge downstream gain: snap to true mute once mostly closed

    return x * gain_;
  }

private:
  // Fixed musical envelope times (gate "feel"); only the threshold is user-facing.
  static constexpr float kEnvMs = 12.0f;       // RMS detector window
  static constexpr float kAtkMs = 2.0f;        // open time
  static constexpr float kRelMs = 80.0f;       // close time
  static constexpr float kHoldMs = 150.0f;     // hold-open after last trigger
  static constexpr float kHysteresisDb = 6.0f; // open - close gap
  static constexpr float kMuteSnap = 3.0e-4f;  // gain below which we hard-mute

  float aEnv_ = 0.0f, aAtk_ = 0.0f, aRel_ = 0.0f;
  int holdN_ = 0, holdCtr_ = 0;
  float openTh_ = 0.0f, closeTh_ = 0.0f;
  float ms_ = 0.0f, gain_ = 1.0f;
  bool enabled_ = false;
};

} // namespace bbm
