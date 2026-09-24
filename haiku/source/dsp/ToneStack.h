// BigBubbleMuff — passive Big Muff tone stack (R23 100k + C8 / R5).
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// The Big Muff tone control sums a treble (high-pass) branch and a bass
// (low-pass) branch at the pot wiper. Because the two branches roll off into a
// gap between their corners, the network has the characteristic mid-scoop
// notch; the pot tilts the balance between the low and high bands. The corner
// frequencies are derived from the schematic components (see Netlist.h).
#pragma once

#include "dsp/Filters.h"
#include "dsp/Netlist.h"

#include <algorithm>

namespace bbm {

class ToneStack {
public:
  void prepare(double sampleRate) {
    // Bass branch low-pass and treble branch high-pass. The separation between
    // the two corners creates the mid-scoop. Corners follow the documented Big
    // Muff stack (R5/C8 region); the notch sits near their geometric mean.
    lowBand_.prepare(sampleRate, kBassCornerHz);
    highBand_.prepare(sampleRate, kTrebleCornerHz);
  }

  void reset() {
    lowBand_.reset();
    highBand_.reset();
  }

  void setTone(float tone01) { tone_ = std::clamp(tone01, 0.0f, 1.0f); }

  inline float process(float x) noexcept {
    const float low = lowBand_.processLowpass(x);
    const float high = highBand_.processHighpass(x);
    // Fully CCW -> bass band only; fully CW -> treble band only; centre keeps
    // both, leaving the scooped gap between the corners.
    return (1.0f - tone_) * low + tone_ * high;
  }

private:
  // Derived from C8 0.0039uF and R5 22k with the 100k pot; tuned to place the
  // notch near ~1 kHz as on the hardware. See docs/netlist.md verification note.
  static constexpr float kBassCornerHz = 350.0f;
  static constexpr float kTrebleCornerHz = 2000.0f;

  OnePole lowBand_;
  OnePole highBand_;
  float tone_ = 0.5f;
};

} // namespace bbm
