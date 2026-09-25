// BigBubbleMuff — the plug-in state blob: one writer, one validating reader.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// setState, setComponentState and the preset loader all receive UNTRUSTED data (a
// project or file authored by anyone), and all three go through readState, so the
// panel and the audio can never disagree about what loaded.
//
// Layout (little-endian), version 1:
//   int32  version            1
//   int32  count              number of table values that follow (0..kMaxStateValues)
//   double norm[count]        normalised values, in kParams (ID) order
//   double hostBypass         normalised
//
// A blob written by a newer build with more parameters keeps the ones this build
// knows; one with fewer leaves the rest at their defaults. A malformed blob applies
// NOTHING: every read is checked, a non-finite value fails the read, and only a
// complete, well-formed blob is returned.
#pragma once

#include "plugin/ids.h"

#include "pluginterfaces/base/ibstream.h"

#include <array>

namespace bbm {

inline constexpr Steinberg::int32 kStateVersion = 1;
inline constexpr Steinberg::int32 kMaxStateValues = 64;

struct StateValues {
  std::array<double, kParamCount> norm{}; // clamped to [0, 1]
  double hostBypass = 0.0;                // clamped to [0, 1]
};

// Table defaults, host bypass off.
StateValues defaultState();

// Reads and validates a blob. Returns false (and leaves `out` untouched) on any
// malformed, truncated, non-finite or future-version input.
bool readState(Steinberg::IBStream *stream, StateValues &out);

// Writes `v` in the layout above. Returns false if the stream refuses a write.
bool writeState(Steinberg::IBStream *stream, const StateValues &v);

} // namespace bbm
