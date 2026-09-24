// BigBubbleMuff (Haiku) — parameter IDs, ranges and class UIDs.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// The controls are the three on the pedal (Sustain, Tone, Volume) plus the two
// the Linux build adds (an output trim and a pre-gain noise gate) and host
// bypass. Ranges and defaults mirror the Linux plug-in's parameter layout so a
// setting means the same thing on either platform.
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace bbmh {

// Parameter IDs. Never change these after a release — projects embed them.
enum ParamIDs : Steinberg::Vst::ParamID {
  kSustainId = 0, // R24 100k SUSTAIN pot, 0 .. 1, default 0.75
  kToneId = 1,    // R23 100k TONE pot, 0 .. 1, default 0.5
  kVolumeId = 2,  // R26 100k VOLUME pot, 0 .. 1, default 0.5
  kOutputId = 3,  // post-model trim, -24 .. +24 dB, default 0
  kGateId = 4,    // pre-gain noise gate, 0 .. 1, default 0.12 (0 = off)
  kBypassId = 5,  // host bypass
};

// Plain-value ranges shared by the processor (denormalisation) and the
// controller (RangeParameter setup). Keep the two sides in sync through these.
namespace ranges {
inline constexpr double kSustainDefault = 0.75;
inline constexpr double kToneDefault = 0.5;
inline constexpr double kVolumeDefault = 0.5;
inline constexpr double kOutputMin = -24.0, kOutputMax = 24.0, kOutputDefault = 0.0;
inline constexpr double kGateDefault = 0.12;
} // namespace ranges

// State layout version written by getState and checked by setState.
inline constexpr Steinberg::int32 kStateVersion = 1;

static DECLARE_UID(BigMuffProcessorUID, 0x06CF17DB, 0xAAA4690B, 0x43D52575, 0x74552B8D);
static DECLARE_UID(BigMuffControllerUID, 0x5186EEDC, 0x8769B68F, 0xE0AE5C23, 0xAFA690E5);

} // namespace bbmh
