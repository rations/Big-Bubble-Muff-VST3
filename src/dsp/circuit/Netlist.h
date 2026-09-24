// BigBubbleMuff — the circuit, as data: every element of docs/netlist.md.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// docs/netlist.md is the single source of truth; this file is its table, row for
// row, with the same designators and node names, and docs/spice/bigmuff.cir is the
// same table for ngspice. Change all three together.
//
// Node names: Kit Rae's numbering runs output -> input, so Q4 is the input booster
// and Q1 the output stage. VCC (+9 V) and IN (the input jack) are ideal sources and
// GND is 0 V; the other 25 nodes are the solver's unknowns.
#pragma once

#include <array>
#include <cstddef>

namespace bbm::circuit {

// Unknown node voltages, in solver order.
enum Node : int {
  N1,
  B4,
  C4,
  E4,
  S3,
  S2,
  S1,
  N5,
  B3,
  C3,
  E3,
  D3N,
  N13,
  B2,
  C2,
  E2,
  D2N,
  T3,
  T2,
  T1,
  B1,
  C1,
  E1,
  V3,
  OUT,
  kNodeCount,
};
// Known potentials (not unknowns), numbered below zero.
enum KnownNode : int {
  GND = -1,
  VCC = -2,
  IN = -3,
};
inline constexpr std::size_t kNodes = kNodeCount;

// The two sources, in input-vector order: u = [v(IN), v(VCC)].
inline constexpr std::size_t kInputs = 2;
inline constexpr double kSupplyVolts = 9.0;

struct Resistor {
  const char *ref;
  int a, b;
  double ohms;
};
struct Capacitor {
  const char *ref;
  int a, b;
  double farads;
};
// A potentiometer: pin 3, wiper, pin 1, total resistance. Knob k in [0, 1] (0 =
// fully CCW) splits it as pin3-wiper = (1 - k) P and wiper-pin1 = k P, each at
// least kPotMinOhms (docs/netlist.md "Pot conventions").
struct Pot {
  const char *ref;
  int pin3, wiper, pin1;
  double ohms;
};
inline constexpr double kPotMinOhms = 1.0;

struct Bjt { // NPN
  const char *ref;
  int c, b, e;
};
// An antiparallel diode pair: one diode anode->cathode, its partner the other way.
struct DiodePair {
  const char *ref;
  int anode, cathode; // of the first-named diode
};

// clang-format off
inline constexpr std::array<Resistor, 22> kResistors{{
    {"R2",  IN,  N1,  39e3},    // input series
    {"R14", B4,  GND, 100e3},   // Q4 base bias
    {"R9",  C4,  B4,  470e3},   // Q4 collector->base feedback
    {"R13", VCC, C4,  12e3},    // Q4 collector load
    {"R22", E4,  GND, 390.0},   // Q4 emitter
    {"R23", S1,  GND, 1e3},     // Sustain pot ground leg
    {"R19", N5,  B3,  10e3},    // series input into Q3
    {"R20", B3,  GND, 100e3},   // Q3 base bias
    {"R17", C3,  B3,  470e3},   // Q3 feedback
    {"R18", VCC, C3,  12e3},    // Q3 collector load
    {"R21", E3,  GND, 390.0},   // Q3 emitter
    {"R12", N13, B2,  10e3},    // series input into Q2
    {"R16", B2,  GND, 100e3},   // Q2 base bias
    {"R15", C2,  B2,  470e3},   // Q2 feedback
    {"R11", VCC, C2,  12e3},    // Q2 collector load
    {"R10", E2,  GND, 390.0},   // Q2 emitter
    {"R5",  T3,  GND, 22e3},    // tone high-pass shunt
    {"R8",  C2,  T1,  20e3},    // tone low-pass series
    {"R7",  VCC, B1,  470e3},   // Q1 base bias from the rail
    {"R3",  B1,  GND, 100e3},   // Q1 base bias to ground
    {"R6",  VCC, C1,  10e3},    // Q1 collector load
    {"R4",  E1,  GND, 2.7e3},   // Q1 emitter
}};

// The amp input the pedal drives (docs/netlist.md "Model boundary conditions").
inline constexpr Resistor kLoad{"RLOAD", OUT, GND, 1e6};

inline constexpr std::array<Capacitor, 13> kCapacitors{{
    {"C1",  N1,  B4,  0.1e-6},  // input coupling
    {"C10", C4,  B4,  470e-12}, // Q4 feedback (Bubble Font)
    {"C4",  C4,  S3,  0.1e-6},  // Q4 -> Sustain
    {"C5",  S2,  N5,  0.1e-6},  // Sustain wiper -> R19
    {"C12", C3,  B3,  470e-12}, // Q3 feedback (Bubble Font)
    {"C6",  B3,  D3N, 0.047e-6},// in series with D3/D4
    {"C13", C3,  N13, 0.1e-6},  // interstage
    {"C11", C2,  B2,  470e-12}, // Q2 feedback (Bubble Font)
    {"C7",  B2,  D2N, 0.047e-6},// in series with D1/D2
    {"C9",  C2,  T3,  0.0039e-6}, // tone high-pass series
    {"C8",  T1,  GND, 0.01e-6}, // tone low-pass shunt
    {"C3",  T2,  B1,  0.1e-6},  // tone wiper -> Q1
    {"C2",  C1,  V3,  0.1e-6},  // output coupling -> Volume
}};

enum PotIndex : std::size_t { kSustain, kTone, kVolume, kPotCount };
inline constexpr std::array<Pot, kPotCount> kPots{{
    {"R24", S3, S2,  S1,  100e3}, // SUSTAIN
    {"R25", T3, T2,  T1,  100e3}, // TONE
    {"R26", V3, OUT, GND, 100e3}, // VOLUME
}};

inline constexpr std::array<Bjt, 4> kBjts{{
    {"Q4", C4, B4, E4}, // input booster
    {"Q3", C3, B3, E3}, // clip stage 1
    {"Q2", C2, B2, E2}, // clip stage 2
    {"Q1", C1, B1, E1}, // output stage
}};

inline constexpr std::array<DiodePair, 2> kDiodePairs{{
    {"D3/D4", D3N, C3}, // Q3 feedback loop (D3 anode D3N; D4 the other way)
    {"D2/D1", D2N, C2}, // Q2 feedback loop (D2 anode D2N; D1 the other way)
}};
// clang-format on

// Device models (docs/netlist.md "Device models"): the Ebers-Moll core of the
// Philips BC547C card and the Shockley core of the onsemi 1N914 card, exactly the
// .model lines of docs/spice/models-em.lib.
struct BjtModel {
  double is = 4.679e-14;
  double nf = 1.01;
  double nr = 1.019;
  double bf = 458.7;
  double br = 11.57;
};
struct DiodeModel {
  double is = 2.52e-9;
  double n = 1.752;
};
inline constexpr BjtModel kBjtModel{};
inline constexpr DiodeModel kDiodeModel{};

// kT/q at 300.15 K with ngspice-47's constants (docs/netlist.md "Thermal voltage").
inline constexpr double kThermalVolts = 1.38064852e-23 * 300.15 / 1.6021766208e-19;

// A digital sample of 1.0 is 1 V at IN, and 1 V at OUT is a sample of 1.0.
inline constexpr double kVoltsPerFullScale = 1.0;

// Solver dimensions.
inline constexpr std::size_t kStates = kCapacitors.size();
inline constexpr std::size_t kPorts = 2 * kBjts.size() + kDiodePairs.size();
inline constexpr std::size_t kVariable = 2 * kPots.size(); // pot halves

// Every capacitor sits between unknown nodes or an unknown node and ground (none
// touches a source), which the state formulation relies on.
constexpr bool capsAvoidSources() {
  for (const Capacitor &c : kCapacitors)
    if (c.a < GND || c.b < GND)
      return false;
  return true;
}
static_assert(capsAvoidSources());

} // namespace bbm::circuit
