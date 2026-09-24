// BigBubbleMuff (Haiku) — edit controller implementation.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#include "bbmcontroller.h"
#include "bbmids.h"
#include "bbmview.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstring>

using namespace Steinberg;

namespace {

inline double clamp01(double v) {
  return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

} // namespace

namespace bbmh {

//------------------------------------------------------------------------
tresult PLUGIN_API BigMuffController::initialize(FUnknown *context) {
  const tresult result = EditController::initialize(context);
  if (result != kResultOk)
    return result;

  // The three pedal controls are plain 0 .. 1 pots, exactly as on the hardware.
  parameters.addParameter(STR16("Sustain"), nullptr, 0, ranges::kSustainDefault,
                          Vst::ParameterInfo::kCanAutomate, kSustainId);
  parameters.addParameter(STR16("Tone"), nullptr, 0, ranges::kToneDefault,
                          Vst::ParameterInfo::kCanAutomate, kToneId);
  parameters.addParameter(STR16("Volume"), nullptr, 0, ranges::kVolumeDefault,
                          Vst::ParameterInfo::kCanAutomate, kVolumeId);

  auto *output =
      new Vst::RangeParameter(STR16("Output"), kOutputId, STR16("dB"), ranges::kOutputMin,
                              ranges::kOutputMax, ranges::kOutputDefault);
  output->setPrecision(1);
  parameters.addParameter(output);

  parameters.addParameter(STR16("Gate"), nullptr, 0, ranges::kGateDefault,
                          Vst::ParameterInfo::kCanAutomate, kGateId);

  parameters.addParameter(
      STR16("Bypass"), nullptr, 1, 0.0,
      Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsBypass, kBypassId);
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API BigMuffController::setComponentState(IBStream *state) {
  // Same untrusted-input rules as the processor's setState: version-checked,
  // read-checked, and clamped before anything is displayed or forwarded.
  if (!state)
    return kResultFalse;
  IBStreamer streamer(state, kLittleEndian);

  int32 version = 0;
  if (!streamer.readInt32(version) || version < 1 || version > kStateVersion)
    return kResultFalse;

  double values[6] = {0.0};
  for (double &v : values)
    if (!streamer.readDouble(v))
      return kResultFalse;

  setParamNormalized(kSustainId, clamp01(values[0]));
  setParamNormalized(kToneId, clamp01(values[1]));
  setParamNormalized(kVolumeId, clamp01(values[2]));
  setParamNormalized(kOutputId, clamp01(values[3]));
  setParamNormalized(kGateId, clamp01(values[4]));
  setParamNormalized(kBypassId, clamp01(values[5]) >= 0.5 ? 1.0 : 0.0);
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API BigMuffController::setParamNormalized(Vst::ParamID tag,
                                                         Vst::ParamValue value) {
  const tresult result = EditController::setParamNormalized(tag, value);
  if (result == kResultOk && mView != nullptr)
    mView->paramChanged(tag, value);
  return result;
}

//------------------------------------------------------------------------
IPlugView *PLUGIN_API BigMuffController::createView(FIDString name) {
  if (name && std::strcmp(name, Vst::ViewType::kEditor) == 0)
    return new BigMuffEditorView(this);
  return nullptr;
}

//------------------------------------------------------------------------
void BigMuffController::editorAttached(Vst::EditorView *editor) {
  mView = static_cast<BigMuffEditorView *>(editor);
}

//------------------------------------------------------------------------
void BigMuffController::editorRemoved(Vst::EditorView *editor) {
  if (mView == static_cast<BigMuffEditorView *>(editor))
    mView = nullptr;
}

} // namespace bbmh
