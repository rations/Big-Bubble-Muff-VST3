// BigBubbleMuff — edit controller implementation (see controller.h).
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#include "plugin/controller.h"
#include "dsp/Checked.h"

#include "plugin/ids.h"
#include "plugin/state.h"

#include "pluginterfaces/base/ustring.h"

using namespace Steinberg;

namespace bbm {

tresult PLUGIN_API Controller::initialize(FUnknown *context) {
  const tresult result = EditController::initialize(context);
  if (result != kResultOk)
    return result;

  for (const ParamSpec &p : kParams) {
    Vst::String128 title = {};
    UString(title, USTRINGSIZE(title)).fromAscii(p.title);
    Vst::String128 units = {};
    if (p.units != nullptr)
      UString(units, USTRINGSIZE(units)).fromAscii(p.units);

    if (p.kind == ParamKind::Toggle) {
      parameters.addParameter(title, p.units != nullptr ? units : nullptr, 1,
                              toNorm(p, p.def), Vst::ParameterInfo::kCanAutomate,
                              static_cast<int32>(p.id));
    } else {
      // The container takes ownership of the raw pointer (SDK contract).
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      auto *range = new Vst::RangeParameter(
          title, p.id, p.units != nullptr ? units : nullptr, p.min, p.max, p.def, 0,
          Vst::ParameterInfo::kCanAutomate);
      range->setPrecision(p.precision);
      parameters.addParameter(range);
    }
  }

  // kIsBypass makes a host's own bypass button drive this parameter rather than
  // adding a second one beside it.
  parameters.addParameter(
      STR16("Bypass"), nullptr, 1, 0.0,
      Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsBypass, kBypassId);
  return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState(IBStream *state) {
  // The processor's blob, through exactly the reader the processor uses.
  StateValues v;
  if (!readState(state, v))
    return kResultFalse;
  for (int i = 0; i < kParamCount; ++i)
    setParamNormalized(static_cast<Vst::ParamID>(i), at(v.norm, i));
  setParamNormalized(kBypassId, v.hostBypass);
  return kResultOk;
}

IPlugView *PLUGIN_API Controller::createView(FIDString /*name*/) {
  return nullptr; // editor: see src/ui/
}

} // namespace bbm
