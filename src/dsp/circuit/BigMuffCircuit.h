// BigBubbleMuff — the whole pedal as one nodal DK state-space model.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Every element of docs/netlist.md (circuit/Netlist.h), solved together at one rate:
// the Nodal DK method (Yeh, "Digital Implementation of Musical Distortion Circuits by
// Analysis and Simulation", Stanford 2009, sec. 4.5; Holters & Zolzer, EUSIPCO 2015)
// with trapezoidal capacitor companions, and the three pots as variable resistors
// folded in by a rank-6 Woodbury update (Holters & Zolzer, DAFx-11), so the large
// matrices are built once per sample rate.
//
// The nonlinear part is 10 ports: each BJT's base-emitter and base-collector
// junctions (Ebers-Moll, the BC547C core of docs/spice/models-em.lib) and the two
// antiparallel 1N914 pairs. Per sample those 10 port equations are solved by Newton
// with SPICE's junction limiting; everything else is a matrix-vector product.
//
// Real-time contract: prepare() is message-thread only (it factorises 25x25
// matrices). setKnobs(), process() and settleToDc() never allocate, lock or make
// system calls; settleToDc() is bounded but costs about a millisecond, so it belongs
// in prepare/reset, not in every block.
#pragma once

#include "dsp/circuit/Dense.h"
#include "dsp/circuit/Netlist.h"

#include <array>
#include <cstddef>

namespace bbm::circuit {

// Knob positions, 0 = fully counter-clockwise (docs/netlist.md "Pot conventions").
struct Knobs {
  double sustain = 0.75;
  double tone = 0.5;
  double volume = 0.5;
};

class BigMuffCircuit {
public:
  // Rows of the output equations: the capacitor states, the port voltages, OUT.
  static constexpr std::size_t kRows = kStates + kPorts + 1;
  // Columns: the capacitor states, the two sources, the port currents.
  static constexpr std::size_t kCols = kStates + kInputs + kPorts;

  BigMuffCircuit() noexcept { setKnobs(Knobs{}); }

  // Builds the rate-dependent matrices for a circuit running at `fs` Hz, applies the
  // current knobs and settles to the DC operating point. Returns false if a matrix
  // is singular or the operating point is not found (neither happens for this
  // netlist; the tests check it).
  bool prepare(double fs) noexcept;

  // Re-derives the knob-dependent matrices. Real-time-safe; about 6k flops.
  void setKnobs(const Knobs &k) noexcept;

  // Solves the DC operating point for the current knobs and makes it the state, as
  // if the pedal had been powered up long ago with no input. Real-time-safe and
  // bounded. Returns false (leaving the state untouched) if Newton fails.
  bool settleToDc() noexcept;

  // One sample: the input jack voltage in, the Volume wiper voltage out, both in
  // volts. Always returns a finite value.
  double process(double vin) noexcept;

  // The node voltages of the last DC solve, in Node order.
  const Vec<kNodes> &dcNodes() const noexcept { return mDcNodes; }

  // Solver statistics since prepare(), for the tests and the CPU report.
  struct Stats {
    long samples = 0;
    long iterations = 0;
    long factorisations = 0; // fresh Jacobians (the rest were chord steps)
    long unconverged = 0;    // hit the iteration cap (the result is still finite)
    long rejected = 0;       // non-finite result, previous state kept
  };
  const Stats &stats() const noexcept { return mStats; }

private:
  // dI/dV of the ports: a 2x2 block per BJT, a scalar per diode pair.
  struct PortJacobian {
    std::array<std::array<double, 4>, 4>
        bjt{}; // [dIbe/dVbe, dIbe/dVbc, dIbc/dVbe, dIbc/dVbc]
    std::array<double, 2> diode{};
  };

  static void evalPorts(const Vec<kPorts> &v, Vec<kPorts> &i, PortJacobian &j) noexcept;
  static bool limitPorts(Vec<kPorts> &vnew, const Vec<kPorts> &vold) noexcept;
  void updateSystem() noexcept;
  bool solveDc(Vec<kNodes> &v, bool ramp) const noexcept;

  bool mReady = false;
  bool mDcValid = false;
  std::array<double, kStates> mCapG{}; // 2C/T of each capacitor

  // Knob-independent: Big0 = L M0^-1 R, P = L M0^-1 Q, Qr = Q' M0^-1 R, S = Q' M0^-1 Q.
  Mat<kRows, kCols> mBig0{};
  Mat<kRows, kVariable> mP{};
  Mat<kVariable, kCols> mQr{};
  Mat<kVariable, kVariable> mS{};

  // Knob-dependent: the system matrix, stored transposed (one row per input column,
  // so the per-sample products sweep contiguous columns and vectorise without
  // reassociating any sum), and the pot-half resistances behind it.
  Mat<kCols, kRows> mBigT{};
  std::array<double, kVariable> mPotOhms{};

  // State: capacitor histories, last port voltages / currents, last output.
  Vec<kStates> mX{};
  Vec<kPorts> mVn{};
  Vec<kPorts> mPrevP{};
  Mat<kPorts, kPorts> mJinvT{}; // the last Newton Jacobian's inverse, transposed
  bool mLuValid = false;
  double mOut = 0.0;

  Vec<kNodes> mDcNodes{};
  Stats mStats{};
};

} // namespace bbm::circuit
