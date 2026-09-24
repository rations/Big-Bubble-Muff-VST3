// BigBubbleMuff — small TPT one-pole filters used between WDF stages.
// Copyright (C) 2026  BigBubbleMuff contributors. GPL-3.0-or-later (see COPYING).
//
// Topology-preserving-transform (TPT) one-pole sections: numerically robust,
// per-sample, no allocation. Used for the AC-coupling high-passes and the
// high-frequency rolloffs that surround the nonlinear clipping cores.
#pragma once

#include <algorithm>
#include <cmath>

namespace bbm {

// Shared TPT one-pole core. g = tan(pi*fc/fs); lowpass output = the integrator
// state, highpass output = input - lowpass.
class OnePole {
public:
  void prepare(double sampleRate, float cutoffHz) {
    setCutoff(sampleRate, cutoffHz);
    reset();
  }

  void setCutoff(double sampleRate, float cutoffHz) {
    const float fs = static_cast<float>(sampleRate);
    const float fc = std::clamp(cutoffHz, 1.0f, 0.45f * fs);
    const float g = std::tan(3.14159265358979323846f * fc / fs);
    g_ = g / (1.0f + g);
  }

  void reset() { z_ = 0.0f; }

  // Returns {lowpass, highpass} for one input sample.
  struct Out {
    float lp;
    float hp;
  };
  Out process(float x) {
    const float v = (x - z_) * g_;
    const float lp = v + z_;
    z_ = lp + v;
    return {lp, x - lp};
  }

  float processLowpass(float x) { return process(x).lp; }
  float processHighpass(float x) { return process(x).hp; }

private:
  float g_ = 0.0f;
  float z_ = 0.0f;
};

} // namespace bbm
