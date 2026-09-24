// BigBubbleMuff (Haiku) — native editor (IPlugView / kPlatformTypeHaikuBView).
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// BigMuffEditorView is the IPlugView the controller hands out from createView();
// the BView it builds (MuffPanelView, defined in bbmview.cpp) lives on the host
// window's looper thread. The controller pushes value changes in through
// paramChanged() on that same thread (see BigMuffController::setParamNormalized),
// so nothing here needs locking. User edits go out through the controller's
// beginEdit/performEdit/endEdit, which reach the host's IComponentHandler.
//
// The constructor stays inert -- no app_server calls -- so createView() is
// harmless in a headless host (the validator opens and closes the editor without
// ever attaching it). All bitmap loading happens in createHaikuView().
#pragma once

#include "haikuplugview.h"

namespace bbmh {

class BigMuffController;
class MuffPanelView;

//------------------------------------------------------------------------
class BigMuffEditorView : public Steinberg::HaikuPlugView {
public:
  explicit BigMuffEditorView(BigMuffController *controller);

  // Called by the controller on the host window's looper thread whenever a
  // parameter changes (user gesture, automation, generic UI or state load).
  void paramChanged(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

protected:
  BView *createHaikuView(BRect frame) SMTG_OVERRIDE;
  void removedFromParent() SMTG_OVERRIDE;

private:
  MuffPanelView *mPanel = nullptr; // owned by HaikuPlugView (fView), cached here
};

} // namespace bbmh
