// BigBubbleMuff — the LV2 build's shared vocabulary: URIs, the port table, the
// version mapping.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// WHAT THE LV2 BUILD IS. Not a second plug-in: bigbubblemuff.so instantiates the
// SAME bbm::Processor a VST3 host does and drives it through IAudioProcessor, and
// bigbubblemuff_ui.so instantiates the SAME bbm::Controller and BbmView. This
// directory is only the adapter between LV2's callbacks and those objects, as the
// owner's rations-amp products/rations/lv2 is. One DSP path, one editor, one state
// format.
//
// Much smaller than rations-amp's, because this plug-in has less to carry: the
// processor and controller exchange no IMessages, there is no MIDI, and there are
// no file paths in the state. Every parameter is a control port, so the host
// carries everything the UI needs, and the state is the VST3 blob, whole.
#pragma once

#include "plugin/ids.h"
#include "version.h"

#include <cstdint>

namespace bbm::lv2 {

// The plug-in's identity is the project page (stringCompanyWeb); the UI is a
// fragment of it, the convention rations-amp and lvtuner use.
inline constexpr const char *kPluginUri = stringCompanyWeb;
inline constexpr const char *kUiUri = stringCompanyWeb "#ui";
inline constexpr const char *kStateBlobUri = stringCompanyWeb "#state";

inline constexpr const char *kDspBinary = "bigbubblemuff.so";
inline constexpr const char *kUiBinary = "bigbubblemuff_ui.so";

// lv2core.ttl: lv2:DistortionPlugin, "A plugin that adds distortion to its input".
// The VST3 files the plug-in under "Fx|Distortion"; this is the same category.
inline constexpr const char *kPluginClass = "lv2:DistortionPlugin";
inline constexpr const char *kPluginClassUri =
    "http://lv2plug.in/ns/lv2core#DistortionPlugin";

// THE VERSION. lv2core.ttl: "an odd minor or micro version, or minor version zero,
// indicates that the resource is a development version ... hosts SHOULD NOT expose
// such plugins to users by default", and LV2 has no major version. rations-amp maps
// by doubling (always even) and asserts MAJOR == 0; this project is already at 1.x,
// so MAJOR is folded into the minor number: minor = 2 * (1000 * MAJOR + MINOR),
// micro = 2 * PATCH. Even by construction, never zero after 0.0, and monotonic as long
// as MINOR stays below 1000.
inline constexpr int kMinorVersion = 2 * (1000 * MAJOR_VERSION_INT + SUB_VERSION_INT);
inline constexpr int kMicroVersion = 2 * RELEASE_NUMBER_INT;
static_assert(SUB_VERSION_INT < 1000, "the LV2 version mapping assumes MINOR < 1000");
static_assert(kMinorVersion != 0, "lv2:minorVersion 0 marks a development build");
static_assert(kMinorVersion % 2 == 0 && kMicroVersion % 2 == 0,
              "odd LV2 versions mark a development build");

// --- ports -----------------------------------------------------------------
//
// Audio is mono in, stereo out: a guitar pedal's input, and the VST3's own
// mono->stereo arrangement (the processor accepts it).
enum PortIndex : std::uint32_t {
  kPortAudioIn = 0,
  kPortAudioOutL = 1,
  kPortAudioOutR = 2,
  kPortControlFirst = 3, // kParams, in ID order
};
inline constexpr std::uint32_t kPortEnabled =
    kPortControlFirst + static_cast<std::uint32_t>(kParamCount);
inline constexpr std::uint32_t kPortLatency = kPortEnabled + 1;
inline constexpr std::uint32_t kPortCount = kPortLatency + 1;

// Control ports carry PLAIN values (an LV2 host draws its control from the port's
// lv2:minimum / lv2:maximum and prints the number), taken straight from kParams, so
// there is no second copy of a range. A port is host input: clamped, and a
// non-finite value becomes the default (toNorm).
inline constexpr ParamId controlParam(std::uint32_t port) {
  return static_cast<ParamId>(port - kPortControlFirst);
}
inline constexpr bool isControlPort(std::uint32_t port) {
  return port >= kPortControlFirst && port < kPortEnabled;
}

// lv2:enabled (lv2core.ttl: "Whether processing is currently enabled (not
// bypassed)", xsd:int) is the host-bypass parameter with its sense inverted, so a
// host's own bypass button drives it. 1 = enabled.
inline constexpr double bypassFromEnabled(float enabled) {
  // NaN compares false and reads as "enabled", the safe default.
  return enabled < 0.5f ? 1.0 : 0.0;
}
inline constexpr float enabledFromBypass(double bypassNorm) {
  return bypassNorm >= 0.5 ? 0.0f : 1.0f;
}

} // namespace bbm::lv2
