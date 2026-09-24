// BigBubbleMuff — plug-in identity: class UIDs, parameter IDs and the parameter table.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// The table below is the one description of every parameter. The controller
// registers from it, the processor denormalises through it, the state blob and the
// preset files are written in its order, and the editor draws from it, so none of
// them can disagree about a range or a default.
#pragma once

#include "dsp/Checked.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <array>
#include <cmath>

namespace bbm {

// Class UIDs (uuidgen, 2026-09-24). Deliberately NOT the earlier framework-based
// build's IDs: this plug-in is a clean break, and a project saved with that build
// keeps opening that build.
static DECLARE_UID(kProcessorUID, 0xCCA2A445, 0xD6B14FF4, 0xB5D425C4, 0xDA71F390);
static DECLARE_UID(kControllerUID, 0x8489E881, 0xC15C416B, 0xB6954F76, 0x83A3387D);

// Parameter IDs. Never renumber after a release — projects and automation store
// them.
enum ParamId : Steinberg::Vst::ParamID {
  kSustainId = 0, // R24 100k SUSTAIN pot
  kToneId = 1,    // R25 100k TONE pot
  kVolumeId = 2,  // R26 100k VOLUME pot
  kOutputId = 3,  // post-model output trim, dB
  kGateId = 4,    // pre-gain noise gate threshold (0 = off)
  kSwitchId = 5,  // the pedal's own footswitch (true bypass on the hardware)
  kBypassId = 6,  // the HOST's bypass (kIsBypass)
};

enum class ParamKind { Range, Toggle };

struct ParamSpec {
  Steinberg::Vst::ParamID id;
  const char *title;
  const char *units; // nullptr = none
  ParamKind kind;
  double min, max, def; // plain units
  int precision;        // digits shown by the host
};

// In ID order. The footswitch defaults ON: a fresh instance is a pedal that is
// switched in, as the earlier build was.
inline constexpr std::array<ParamSpec, 6> kParams{{
    {kSustainId, "Sustain", nullptr, ParamKind::Range, 0.0, 1.0, 0.75, 2},
    {kToneId, "Tone", nullptr, ParamKind::Range, 0.0, 1.0, 0.5, 2},
    {kVolumeId, "Volume", nullptr, ParamKind::Range, 0.0, 1.0, 0.5, 2},
    {kOutputId, "Output", "dB", ParamKind::Range, -24.0, 24.0, 0.0, 1},
    {kGateId, "Gate", nullptr, ParamKind::Range, 0.0, 1.0, 0.12, 2},
    {kSwitchId, "Footswitch", nullptr, ParamKind::Toggle, 0.0, 1.0, 1.0, 0},
}};
inline constexpr int kParamCount = static_cast<int>(kParams.size());

constexpr bool idsMatchIndices() {
  for (int i = 0; i < kParamCount; ++i)
    if (at(kParams, i).id != static_cast<Steinberg::Vst::ParamID>(i))
      return false;
  return true;
}
static_assert(idsMatchIndices(), "kParams must be in ID order with IDs 0..N-1");
static_assert(kBypassId == kParamCount, "host bypass follows the table");

// Clamps a normalised value to [0, 1]; a non-finite value becomes 0. Total on
// every double a host, a project or a preset file can supply: a plain two-sided
// clamp lets NaN straight through.
inline double clampNorm(double v) {
  if (!std::isfinite(v))
    return 0.0;
  return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

inline double toPlain(const ParamSpec &p, double norm) {
  norm = clampNorm(norm);
  if (p.kind == ParamKind::Toggle)
    return norm >= 0.5 ? p.max : p.min;
  return p.min + norm * (p.max - p.min);
}

inline double toNorm(const ParamSpec &p, double plain) {
  const double span = p.max - p.min;
  if (span <= 0.0)
    return 0.0;
  if (!std::isfinite(plain))
    plain = p.def;
  return clampNorm((plain - p.min) / span);
}

} // namespace bbm
