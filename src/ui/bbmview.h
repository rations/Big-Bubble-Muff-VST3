// BigBubbleMuff — the editor: faceplate, five knobs, lamp, footswitch, wordmark
// and the preset bar.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Derives from X11PlugView and is expressed entirely through its hooks (onDraw,
// onMouse*, constrainSize), so nothing here includes a windowing header. The knob
// and footswitch interaction follows the owner's rations-amp / rations-pedals
// editors.
//
// THREADING: every method runs on the host's UI/run-loop thread, which is also the
// thread the controller calls paramChanged from. Nothing is shared with the audio
// thread, so nothing is locked or atomic.
#pragma once

#include "gfx/canvas.h"
#include "gfx/fontstack.h"
#include "gfx/image.h"
#include "platform/x11plugview.h"
#include "plugin/ids.h"

#include <array>

namespace bbm {

class BbmView : public X11PlugView {
public:
  explicit BbmView(Steinberg::Vst::EditController *editController);

  // Every route into a parameter passes through the controller's
  // setParamNormalized, which forwards here: automation, a generic UI, a state
  // load, and this editor's own edits.
  void paramChanged(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

  // Geometry the tests check without a display.
  static void constrain(int &w, int &h);

protected:
  void onAttached() override;
  void onRemoved() override;
  void onDraw(cairo_t *cr) override;
  void onMouseDown(int x, int y, int button) override;
  void onMouseMove(int x, int y) override;
  void onMouseUp(int x, int y, int button) override;
  void onMouseWheel(int x, int y, int delta) override;
  bool isResizable() const override { return true; }
  void constrainSize(int &w, int &h) const override { constrain(w, h); }
  void onResized(int w, int h) override;

private:
  // Device pixels to logical units. False outside the drawn area (the letterbox
  // a host that ignored constrainSize leaves behind).
  bool toLogical(int x, int y, float &lx, float &ly) const;
  // Index into layout::kKnobs under a logical point, or -1.
  static int hitKnob(float lx, float ly);
  static bool hitFootswitch(float lx, float ly);

  bool engaged() const;
  bool fine() const;
  // begin / setParamNormalized / performEdit / end, as one host gesture.
  void editParam(Steinberg::Vst::ParamID id, double norm);
  // Inside an open gesture: setParamNormalized + performEdit.
  void dragTo(double norm);
  void endDrag();

  void drawFace(Canvas &c);
  void drawWordmark(Canvas &c);
  void drawKnobs(Canvas &c);
  void drawLamp(Canvas &c);
  void drawFootswitch(Canvas &c);
  void drawBar(Canvas &c);

  // Device-pixel size of a logical length at the current scale.
  int devicePx(int logical) const;

  FontStack mFonts;
  ImageCache mImages;

  std::array<double, kParamCount> mNorm{};
  double mHostBypass = 0.0;

  double mScale = 1.0, mOffX = 0.0, mOffY = 0.0;

  int mDragKnob = -1; // index into layout::kKnobs
  float mDragStartY = 0.0f;
  double mDragStartNorm = 0.0;
  bool mDragFine = false;
};

} // namespace bbm
