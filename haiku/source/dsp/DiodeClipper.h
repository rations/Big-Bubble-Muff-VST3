// BigBubbleMuff — antiparallel diode-pair feedback clipper.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Models the Big Muff clipping diodes (D1/D2, 1N914 antiparallel pairs across the
// collector->base feedback of Q3/Q2). Per the ElectroSmash Big Muff Pi analysis,
// these diodes "limit the signal peaks so the output will never exceed ~+/-0.6 V"
// — they clamp the high-gain stage's collector swing, they do NOT set its bias or
// its small-signal gain. So they are applied to the transistor stage's output.
//
// Behaviour: a series source resistance Rs (the collector load) feeding an
// antiparallel diode pair to ground forms a soft clipper. For a drive voltage u
// the clamped output y solves the implicit Shockley relation
//     u = y + Rs * 2*Is*sinh(y / (n*Vt))
// (current through Rs equals the antiparallel-pair current). Monotonic in y, so a
// warm-started Newton iteration converges in a few steps and is real-time-safe.
#pragma once

#include "dsp/Netlist.h"

#include <algorithm>
#include <cmath>

namespace bbm {

class DiodeClipper {
public:
  // `seriesR` sets the clip threshold/hardness (larger -> harder, lower knee).
  // Defaults to the collector load the diodes clamp against.
  void prepare(float seriesR = netlist::kStageRc) {
    rs_ = seriesR;
    reset();
  }

  void reset() {} // stateless: nothing to clear

  inline float process(float u) noexcept {
    using namespace netlist;
    constexpr float invVt = 1.0f / kDiodeVt;
    const float twoIsRs = 2.0f * kDiodeIs * rs_;

    // Initial guess: the binding one of the two limits — near-transparent (y ~= u,
    // below the diode knee) versus fully clamped (y ~= Vt*asinh(u/2IsRs), all the
    // series current diverted into the diode pair). asinh is overflow-free, so
    // this lands close to the root for any drive, large or small. Newton then
    // converges in 1-2 steps. Stateless by design: there is no warm-start that a
    // fast transient could poison (a far/wrong-side start makes the stiff
    // exponential creep and miss within the iteration cap).
    const float yClamp = kDiodeVt * std::asinh(u / twoIsRs);
    float y = (std::abs(u) < std::abs(yClamp)) ? u : yClamp;

    for (int iter = 0; iter < kMaxIters; ++iter) {
      const float z = y * invVt;
      // Bounded sinh/cosh via a limited exponential so a transient drive can
      // never overflow the junction term (it would poison the whole signal).
      const Exp ep = lexp(z);
      const Exp en = lexp(-z);
      const float sinh = 0.5f * (ep.v - en.v);
      const float cosh = 0.5f * (ep.v + en.v);

      const float f = y + twoIsRs * sinh - u;
      const float df = 1.0f + twoIsRs * invVt * cosh;
      const float step = f / df;
      y -= step;
      if (std::abs(step) < kTol)
        break;
    }

    return std::isfinite(y) ? y : 0.0f;
  }

private:
  struct Exp {
    float v;
    float d;
  };
  static inline Exp lexp(float x) noexcept {
    constexpr float xmax = 30.0f;
    if (x < xmax) {
      const float e = std::exp(x);
      return {e, e};
    }
    const float e = std::exp(xmax);
    return {e * (1.0f + (x - xmax)), e};
  }

  static constexpr int kMaxIters = 8;
  static constexpr float kTol = 1.0e-6f; // volts

  float rs_ = netlist::kStageRc;
};

} // namespace bbm
