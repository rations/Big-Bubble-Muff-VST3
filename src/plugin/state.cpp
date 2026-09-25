// BigBubbleMuff — the plug-in state blob (see state.h).
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#include "plugin/state.h"
#include "dsp/Checked.h"

#include "base/source/fstreamer.h"

#include <cmath>

namespace bbm {

StateValues defaultState() {
  StateValues v;
  for (int i = 0; i < kParamCount; ++i) {
    const ParamSpec &p = at(kParams, i);
    at(v.norm, i) = toNorm(p, p.def);
  }
  v.hostBypass = 0.0;
  return v;
}

bool readState(Steinberg::IBStream *stream, StateValues &out) {
  if (stream == nullptr)
    return false;
  Steinberg::IBStreamer s(stream, kLittleEndian);

  Steinberg::int32 version = 0;
  if (!s.readInt32(version) || version < 1 || version > kStateVersion)
    return false;

  Steinberg::int32 count = 0;
  if (!s.readInt32(count) || count < 0 || count > kMaxStateValues)
    return false;

  std::array<double, kMaxStateValues> values{};
  for (Steinberg::int32 i = 0; i < count; ++i) {
    double &v = at(values, i);
    if (!s.readDouble(v) || !std::isfinite(v))
      return false;
  }
  double bypass = 0.0;
  if (!s.readDouble(bypass) || !std::isfinite(bypass))
    return false;

  // Everything read, nothing failed: only now build the result.
  StateValues v = defaultState();
  for (Steinberg::int32 i = 0; i < count && i < kParamCount; ++i)
    at(v.norm, i) = clampNorm(at(values, i));
  v.hostBypass = clampNorm(bypass);
  out = v;
  return true;
}

bool writeState(Steinberg::IBStream *stream, const StateValues &v) {
  if (stream == nullptr)
    return false;
  Steinberg::IBStreamer s(stream, kLittleEndian);
  bool ok = s.writeInt32(kStateVersion) && s.writeInt32(kParamCount);
  for (int i = 0; ok && i < kParamCount; ++i)
    ok = s.writeDouble(clampNorm(at(v.norm, i)));
  return ok && s.writeDouble(clampNorm(v.hostBypass));
}

} // namespace bbm
