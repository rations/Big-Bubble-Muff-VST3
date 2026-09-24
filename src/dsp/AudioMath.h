// BigBubbleMuff — small audio utilities for the circuit engine.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// The handful of facilities the engine once took from juce_dsp, written against the
// standard library alone so the circuit model has no framework dependency.
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace bbm {

// Convert decibels to a linear gain.
inline float decibelsToGain(float dB) {
  return std::pow(10.0f, dB * 0.05f);
}

// Replace non-finite samples with silence so NaN/Inf never escapes the engine.
inline float sanitise(float x) {
  return std::isfinite(x) ? x : 0.0f;
}

// Tolerant float comparison, matching what juce::SmoothedValue uses to decide a
// new target is really new: equal within one epsilon, relative to magnitude.
inline bool approximatelyEqual(float a, float b) {
  if (!(std::isfinite(a) && std::isfinite(b)))
    return std::isnan(a) == std::isnan(b) && std::signbit(a) == std::signbit(b) &&
           std::isinf(a) == std::isinf(b);
  const float diff = std::abs(a - b);
  return diff <= std::numeric_limits<float>::min() ||
         diff <=
             std::numeric_limits<float>::epsilon() * std::max(std::abs(a), std::abs(b));
}

// Linear parameter ramp, matching juce::SmoothedValue<float> (whose default
// smoothing type is Linear): reset() fixes the ramp length in samples, a new
// target restarts the ramp from wherever the value currently is, and the value
// lands exactly on the target on the final step.
class LinearSmoother {
public:
  LinearSmoother() = default;
  explicit LinearSmoother(float initial) : current_(initial), target_(initial) {}

  // rampSeconds is measured at the rate the smoother is advanced at, so reset it
  // with the rate whose getNextValue() will be called (base or oversampled).
  void reset(double sampleRate, double rampSeconds) {
    stepsToTarget_ = static_cast<int>(std::floor(rampSeconds * sampleRate));
    setCurrentAndTargetValue(target_);
  }

  void setCurrentAndTargetValue(float value) {
    current_ = target_ = value;
    countdown_ = 0;
  }

  void setTargetValue(float value) {
    if (approximatelyEqual(value, target_))
      return;
    if (stepsToTarget_ <= 0) {
      setCurrentAndTargetValue(value);
      return;
    }
    target_ = value;
    countdown_ = stepsToTarget_;
    step_ = (target_ - current_) / static_cast<float>(countdown_);
  }

  float getTargetValue() const { return target_; }
  float getCurrentValue() const { return current_; }
  bool isSmoothing() const { return countdown_ > 0; }

  inline float getNextValue() noexcept {
    if (countdown_ <= 0)
      return target_;
    --countdown_;
    current_ = (countdown_ > 0) ? current_ + step_ : target_;
    return current_;
  }

private:
  float current_ = 0.0f;
  float target_ = 0.0f;
  float step_ = 0.0f;
  int countdown_ = 0;
  int stepsToTarget_ = 0;
};

} // namespace bbm
