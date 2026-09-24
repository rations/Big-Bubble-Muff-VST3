// BigBubbleMuff — edit controller.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Registers the parameters from the kParams table (ids.h) plus the host bypass,
// mirrors the processor's state through the shared reader (state.h), and hands out
// the editor.
#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace bbm {

class Controller : public Steinberg::Vst::EditController {
public:
  static Steinberg::FUnknown *createInstance(void *) {
    return static_cast<Steinberg::Vst::IEditController *>(new Controller());
  }

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) override;
  Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream *state) override;
  Steinberg::IPlugView *PLUGIN_API createView(Steinberg::FIDString name) override;

  OBJ_METHODS(Controller, EditController)
  DEFINE_INTERFACES
  END_DEFINE_INTERFACES(EditController)
  REFCOUNT_METHODS(EditController)
};

} // namespace bbm
