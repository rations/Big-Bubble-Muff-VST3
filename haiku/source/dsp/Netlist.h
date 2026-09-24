// BigBubbleMuff — circuit constants derived from docs/netlist.md.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// These values come ONLY from the transcribed schematic (docs/netlist.md). Do not
// invent values here. Corner frequencies are computed from the component R/C
// products so the model's voicing follows the hardware.
#pragma once

namespace bbm::netlist {

// --- Diodes: D1/D2 antiparallel pairs (KD522B ~= 1N914/1N4148) -------------
inline constexpr float kDiodeIs = 2.52e-9f;  // 1N914 reverse saturation current [A]
inline constexpr float kDiodeVt = 25.85e-3f; // thermal voltage [V]
inline constexpr float kDiodeN = 1.0f;       // one diode per direction

// --- Clipping-stage feedback network (the "muff" voicing) ------------------
// Feedback R = 470k (R17/R15), feedback C = 470pF (C12/C11). Their product sets
// the HF rolloff inside the clipping feedback loop: ~720 Hz. (Bubble-Font 470pF.)
inline constexpr float kFbR = 470.0e3f;   // R17 / R15
inline constexpr float kFbC = 470.0e-12f; // C12 / C11 (Bubble-Font single cap)

// --- Inter-stage AC coupling high-pass -------------------------------------
// C6/C7 = 0.047uF into ~100k (R20/R16) base bias: ~34 Hz high-pass.
inline constexpr float kCouplingC = 0.047e-6f; // C6 / C7
inline constexpr float kCouplingR = 100.0e3f;  // R20 / R16

// --- Clipping-stage DC network (Phase B nodal transistor model) ------------
// Common-emitter sustain stage: collector load R18/R12, emitter R22/R21,
// base bias R20/R16 to ground, collector-to-base feedback R17/R15 (= kFbR) with
// the 470pF feedback cap (= kFbC) and the antiparallel diode pair across it.
inline constexpr float kStageRc = 12.0e3f;  // R18 / R12 collector load
inline constexpr float kStageRe = 390.0f;   // R22 / R21 emitter (Bubble-Font)
inline constexpr float kStageRb = 100.0e3f; // R20 / R16 base bias to ground
inline constexpr float kSupplyV = 9.0f;     // 9 V rail

// --- Bipolar transistor (3102EM / 2N549C / 547C) Ebers-Moll parameters -----
// Silicon small-signal NPN. High beta (549C/3102EM are high-gain types).
inline constexpr float kBjtIs = 1.0e-14f;  // transport saturation current [A]
inline constexpr float kBjtBetaF = 300.0f; // forward current gain
inline constexpr float kBjtBetaR = 3.0f;   // reverse current gain
inline constexpr float kBjtVt = 25.85e-3f; // thermal voltage [V]

// --- Input coupling: C1 1uF into ~100k -------------------------------------
inline constexpr float kInputC = 1.0e-6f; // C1
inline constexpr float kInputR = 100.0e3f;

// --- Output coupling: C2/C3 ------------------------------------------------
inline constexpr float kOutputC = 1.0e-6f; // C2
inline constexpr float kOutputR = 100.0e3f;

// --- Tone stack (R23 100k, C8 0.0039uF treble, R5 22k + C9 bass) -----------
// Treble (high-pass) and bass (low-pass) branches summed at the wiper, giving
// the signature mid-scoop. Corners derived from the documented Big Muff stack.
inline constexpr float kToneTrebleC = 0.0039e-6f; // C8
inline constexpr float kTonePot = 100.0e3f;       // R23
inline constexpr float kToneBassR = 22.0e3f;      // R5
inline constexpr float kToneBassC = 0.0039e-6f;   // bass branch cap (~C8/C9 region)

} // namespace bbm::netlist
