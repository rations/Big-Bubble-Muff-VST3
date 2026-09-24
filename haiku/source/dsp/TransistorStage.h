// BigBubbleMuff — nodal common-emitter transistor gain stage (full circuit model).
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Models one Big Muff common-emitter NPN stage as its real circuit and solves it
// with the nodal DK-method (companion-model capacitors + Newton-Raphson) at the
// oversampled rate. All four Big Muff stages (Q4 input booster, Q3/Q2 clippers,
// Q1 recovery) share this identical topology — see the ElectroSmash Big Muff Pi
// analysis: "each of the clipping stages keeps the same topology as the input
// booster". The diode *clipping* is handled separately (DiodeClipper), applied to
// this stage's output, because in the real circuit the antiparallel feedback
// diodes only clamp the collector *swing* to ~+/-0.6 V; they do not collapse the
// stage's small-signal gain. Folding them into the DC feedback loop (an earlier
// attempt) biased the diodes on at idle and turned the pedal into a noise gate.
//
// Circuit (values from docs/netlist.md):
//   Vin --Cin(0.047uF)--> [base vb] --Rb(100k)--> gnd
//   [collector vc] --Rc(12k)--> Vcc(9V)
//   [emitter  ve] --Re(390)--> gnd
//   feedback vc<->vb: Rf(470k) || Cf(470pF)   (470pF = Bubble-Font HF rolloff)
//   Q: Ebers-Moll NPN (Vbe = vb-ve, Vbc = vb-vc)
#pragma once

#include "dsp/Netlist.h"

#include <algorithm>
#include <cmath>

namespace bbm {

class TransistorStage {
public:
  void prepare(double sampleRate) {
    const float T = 1.0f / static_cast<float>(sampleRate);
    gCin_ = netlist::kCouplingC / T; // backward-Euler companion conductances
    gCf_ = netlist::kFbC / T;
    reset();
  }

  void reset() {
    // Solve the DC operating point (capacitors open) so the stage starts settled
    // with no turn-on transient. With no diode in the feedback path the collector
    // biases near mid-rail (active region, high gain), so start Newton there.
    vb_ = 0.7f;
    vc_ = 4.5f;
    ve_ = 0.1f;
    solve(0.0f, /*dcCaps=*/true);
    vcDC_ = vc_;
    vbPrev_ = vb_;
    vcPrev_ = vc_;
    vinPrev_ = 0.0f;
  }

  // Processes one input-voltage sample and returns the AC collector voltage
  // (DC operating point removed). Real-time-safe; bounded Newton iteration.
  inline float process(float vin) noexcept {
    solve(vin, /*dcCaps=*/false);
    vbPrev_ = vb_;
    vcPrev_ = vc_;
    vinPrev_ = vin;
    const float out = vc_ - vcDC_;
    return std::isfinite(out) ? out : 0.0f;
  }

  // DC bias point (valid immediately after prepare/reset). For tests/diagnostics.
  struct OperatingPoint {
    float vb, vc, ve;
  };
  OperatingPoint operatingPoint() const { return {vb_, vc_, ve_}; }

private:
  // Limited exponential with a C1 linear extension above xmax. Beyond the
  // threshold the value grows linearly and the derivative stays constant
  // (nonzero) — this bounds the stiff junction while preserving a restoring force
  // in the Jacobian, which a hard exp() clamp would destroy (zero derivative ->
  // Newton diverges). Returns {value, d/dx}.
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

  // Newton-Raphson solve of the 3-node system for {vb, vc, ve}.
  inline void solve(float vin, bool dcCaps) noexcept {
    using namespace netlist;
    constexpr float invVt = 1.0f / kBjtVt;
    const float gCin = dcCaps ? 0.0f : gCin_; // caps are open at DC
    const float gCf = dcCaps ? 0.0f : gCf_;
    const float veqIn = vbPrev_ - vinPrev_; // previous cap voltages
    const float veqF = vbPrev_ - vcPrev_;

    constexpr float gRb = 1.0f / kStageRb;
    constexpr float gRf = 1.0f / kFbR;
    constexpr float gRc = 1.0f / kStageRc;
    constexpr float gRe = 1.0f / kStageRe;

    for (int iter = 0; iter < kMaxIters; ++iter) {
      const float Vbe = vb_ - ve_;
      const float Vbc = vb_ - vc_;

      const Exp ef = lexp(Vbe * invVt);
      const Exp er = lexp(Vbc * invVt);

      // Ebers-Moll terminal currents (use .v, the junction value).
      const float Ic = kBjtIs * ((ef.v - er.v) - (er.v - 1.0f) / kBjtBetaR);
      const float Ib = kBjtIs * ((ef.v - 1.0f) / kBjtBetaF + (er.v - 1.0f) / kBjtBetaR);
      const float Ie = Ic + Ib;

      // Conductances of the Ebers-Moll partials (.d = d(exp)/d(arg)).
      const float gef = kBjtIs * ef.d * invVt;
      const float ger = kBjtIs * er.d * invVt;
      const float dIc_dVbe = gef;
      const float dIc_dVbc = -ger * (1.0f + 1.0f / kBjtBetaR);
      const float dIb_dVbe = gef / kBjtBetaF;
      const float dIb_dVbc = ger / kBjtBetaR;
      const float dIe_dVbe = gef * (1.0f + 1.0f / kBjtBetaF);
      const float dIe_dVbc = -ger;

      // KCL residuals (currents leaving each node sum to zero).
      const float Fb = vb_ * gRb + gCin * (vb_ - vin) - gCin * veqIn + (vb_ - vc_) * gRf +
                       gCf * (vb_ - vc_) - gCf * veqF + Ib;
      const float Fc = (vc_ - kSupplyV) * gRc + (vc_ - vb_) * gRf + gCf * (vc_ - vb_) +
                       gCf * veqF + Ic;
      const float Fe = ve_ * gRe - Ie;

      // Jacobian (rows Fb,Fc,Fe; cols vb,vc,ve).
      const float Jbb = gRb + gCin + gRf + gCf + (dIb_dVbe + dIb_dVbc);
      const float Jbc = -gRf - gCf - dIb_dVbc;
      const float Jbe = -dIb_dVbe;
      const float Jcb = -gRf - gCf + (dIc_dVbe + dIc_dVbc);
      const float Jcc = gRc + gRf + gCf - dIc_dVbc;
      const float Jce = -dIc_dVbe;
      const float Jeb = -(dIe_dVbe + dIe_dVbc);
      const float Jec = dIe_dVbc;
      const float Jee = gRe + dIe_dVbe;

      float dvb = 0.0f, dvc = 0.0f, dve = 0.0f;
      if (!solve3(Jbb, Jbc, Jbe, Jcb, Jcc, Jce, Jeb, Jec, Jee, Fb, Fc, Fe, dvb, dvc, dve))
        break; // singular Jacobian — keep current estimate

      // Damp the step to keep junction voltages from jumping (SPICE-style limit).
      const float damp = limitStep(dvb, dvc, dve);
      vb_ -= damp * dvb;
      vc_ -= damp * dvc;
      ve_ -= damp * dve;

      // Clamp to the physical rails (a hair below ground to a hair above supply).
      constexpr float kLo = -0.6f;
      const float kHi = kSupplyV + 0.6f;
      vb_ = std::clamp(vb_, kLo, kHi);
      vc_ = std::clamp(vc_, kLo, kHi);
      ve_ = std::clamp(ve_, kLo, kHi);

      if (std::abs(dvb) < kTol && std::abs(dvc) < kTol && std::abs(dve) < kTol)
        break;
    }

    if (!(std::isfinite(vb_) && std::isfinite(vc_) && std::isfinite(ve_)))
      reset();
  }

  // Limits the Newton update so no node moves more than kMaxStep volts at once.
  static inline float limitStep(float dvb, float dvc, float dve) noexcept {
    const float m = std::max(std::abs(dvb), std::max(std::abs(dvc), std::abs(dve)));
    return (m > kMaxStep) ? kMaxStep / m : 1.0f;
  }

  // Solves a 3x3 system J*x = f by Cramer's rule. Returns false if (near-)singular.
  static inline bool solve3(float a11, float a12, float a13, float a21, float a22,
                            float a23, float a31, float a32, float a33, float b1,
                            float b2, float b3, float &x1, float &x2,
                            float &x3) noexcept {
    const float det = a11 * (a22 * a33 - a23 * a32) - a12 * (a21 * a33 - a23 * a31) +
                      a13 * (a21 * a32 - a22 * a31);
    if (std::abs(det) < 1.0e-20f)
      return false;
    const float invDet = 1.0f / det;
    x1 = (b1 * (a22 * a33 - a23 * a32) - a12 * (b2 * a33 - a23 * b3) +
          a13 * (b2 * a32 - a22 * b3)) *
         invDet;
    x2 = (a11 * (b2 * a33 - a23 * b3) - b1 * (a21 * a33 - a23 * a31) +
          a13 * (a21 * b3 - b2 * a31)) *
         invDet;
    x3 = (a11 * (a22 * b3 - b2 * a32) - a12 * (a21 * b3 - b2 * a31) +
          b1 * (a21 * a32 - a22 * a31)) *
         invDet;
    return std::isfinite(x1) && std::isfinite(x2) && std::isfinite(x3);
  }

  // Per-sample Newton bounds. Warm-started from the previous sample, the solve
  // converges in a few iterations; the cap keeps the audio thread bounded. kTol
  // is in volts: 1e-5 V (10 uV) is inaudible and stays above float rounding at the
  // multi-volt bias nodes so the solver declares convergence instead of spinning.
  static constexpr int kMaxIters = 16;
  static constexpr float kTol = 1.0e-5f;
  static constexpr float kMaxStep = 0.3f; // max per-iteration node move [V]

  float gCin_ = 0.0f, gCf_ = 0.0f;
  float vb_ = 0.7f, vc_ = 4.5f, ve_ = 0.1f;
  float vbPrev_ = 0.0f, vcPrev_ = 0.0f, vinPrev_ = 0.0f;
  float vcDC_ = 4.5f;
};

} // namespace bbm
