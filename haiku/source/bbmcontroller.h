// BigBubbleMuff (Haiku) — edit controller.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Declares the six parameters and hands out the native Interface Kit editor.
// The live view is tracked through the EditorView attach hooks and fed by
// setParamNormalized; all of that runs on the host window's looper thread
// (the kPlatformTypeHaikuBView contract), so no locking is needed around it.
#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace bbmh {

class BigMuffEditorView;

//------------------------------------------------------------------------
class BigMuffController : public Steinberg::Vst::EditController {
public:
  static Steinberg::FUnknown *createInstance(void *) {
    return static_cast<Steinberg::Vst::IEditController *>(new BigMuffController());
  }

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream *state)
      SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setParamNormalized(
      Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;

  Steinberg::IPlugView *PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
  void editorAttached(Steinberg::Vst::EditorView *editor) SMTG_OVERRIDE;
  void editorRemoved(Steinberg::Vst::EditorView *editor) SMTG_OVERRIDE;

  //---Interface---------
  OBJ_METHODS(BigMuffController, EditController)
  DEFINE_INTERFACES
  END_DEFINE_INTERFACES(EditController)
  REFCOUNT_METHODS(EditController)

private:
  BigMuffEditorView *mView = nullptr; // live editor; host window looper thread only
};

} // namespace bbmh
