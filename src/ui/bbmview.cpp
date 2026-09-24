// BigBubbleMuff — editor implementation. See bbmview.h.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#include "ui/bbmview.h"

#include "dsp/Checked.h"
#include "ui/layout.h"

#include "pluginterfaces/base/keycodes.h"

#include <algorithm>
#include <cmath>

using namespace Steinberg;

namespace bbm {

namespace {
constexpr auto kW = static_cast<float>(layout::kWidth);
constexpr auto kH = static_cast<float>(layout::kHeight);
constexpr auto kBar = static_cast<float>(layout::kBarHeight);

// A faceplate-space centred box, pushed below the preset bar.
constexpr Rect placed(int cx, int cy, int size) {
  return Rect::centred(static_cast<float>(cx), static_cast<float>(cy) + kBar,
                       static_cast<float>(size));
}
} // namespace

BbmView::BbmView(Vst::EditController *editController) : X11PlugView(editController) {
  ViewRect size(0, 0, layout::kWidth, layout::kHeight);
  setRect(size);
  for (int i = 0; i < kParamCount; ++i)
    at(mNorm, i) = toNorm(at(kParams, i), at(kParams, i).def);
}

void BbmView::paramChanged(Vst::ParamID id, Vst::ParamValue value) {
  if (id == kBypassId)
    mHostBypass = clampNorm(value);
  else if (id < static_cast<Vst::ParamID>(kParamCount))
    at(mNorm, id) = clampNorm(value);
  else
    return;
  invalidate();
}

void BbmView::constrain(int &w, int &h) {
  // Keep the art's aspect and stay between the two scale bounds. The smaller of
  // the two ratios wins, so the result always fits what the host offered.
  const double byW = static_cast<double>(w) / layout::kWidth;
  const double byH = static_cast<double>(h) / layout::kHeight;
  const double s = std::clamp(std::min(byW, byH), layout::kMinScale, layout::kMaxScale);
  w = static_cast<int>(std::lround(layout::kWidth * s));
  h = static_cast<int>(std::lround(layout::kHeight * s));
}

//------------------------------------------------------------------------
void BbmView::onAttached() {
  mFonts.load();
  invalidate();
}

void BbmView::onRemoved() {
  endDrag();
  mImages.purgeScaled();
}

void BbmView::onResized(int w, int h) {
  // Fit and centre. constrainSize normally leaves no remainder, but a host may hand
  // over any size at all, and a letterboxed pedal beats a stretched one.
  mScale = std::clamp(std::min(w / static_cast<double>(kW), h / static_cast<double>(kH)),
                      layout::kMinScale, layout::kMaxScale);
  mOffX = (w - kW * mScale) * 0.5;
  mOffY = (h - kH * mScale) * 0.5;
  // Cached bitmaps are keyed on their pixel size, and every one just changed.
  mImages.purgeScaled();
  invalidate();
}

int BbmView::devicePx(int logical) const {
  return std::max(1, static_cast<int>(std::lround(logical * mScale)));
}

bool BbmView::toLogical(int x, int y, float &lx, float &ly) const {
  if (mScale <= 0.0)
    return false;
  lx = static_cast<float>((x - mOffX) / mScale);
  ly = static_cast<float>((y - mOffY) / mScale);
  return lx >= 0.0f && ly >= 0.0f && lx < kW && ly < kH;
}

int BbmView::hitKnob(float lx, float ly) {
  // The whole square the knob is drawn in, as the previous editor's knobs were.
  for (std::size_t k = 0; k < layout::kKnobs.size(); ++k) {
    const layout::Knob &knob = at(layout::kKnobs, k);
    if (placed(knob.cx, knob.cy, layout::kKnobBox).contains(lx, ly))
      return static_cast<int>(k);
  }
  return -1;
}

bool BbmView::hitFootswitch(float lx, float ly) {
  // Circular, so the transparent corners of the art are inert.
  const Rect r = placed(layout::kFootCx, layout::kFootCy, layout::kFootBox);
  const float dx = lx - r.centreX();
  const float dy = ly - r.centreY();
  const float radius = r.w * 0.5f;
  return dx * dx + dy * dy <= radius * radius;
}

bool BbmView::engaged() const {
  return at(mNorm, kSwitchId) >= 0.5;
}

bool BbmView::fine() const {
  return (pointerModifiers() & kShiftKey) != 0;
}

//------------------------------------------------------------------------
void BbmView::editParam(Vst::ParamID id, double norm) {
  Vst::EditController *const ctl = getController();
  if (ctl == nullptr)
    return;
  norm = clampNorm(norm);
  ctl->beginEdit(id);
  // performEdit tells the host; setParamNormalized keeps the controller's own copy
  // in step (and reaches paramChanged), which performEdit does not do.
  ctl->setParamNormalized(id, norm);
  ctl->performEdit(id, norm);
  ctl->endEdit(id);
  invalidate();
}

void BbmView::dragTo(double norm) {
  Vst::EditController *const ctl = getController();
  if (ctl == nullptr || mDragKnob < 0)
    return;
  const Vst::ParamID id = at(layout::kKnobs, mDragKnob).id;
  norm = clampNorm(norm);
  if (std::fabs(norm - at(mNorm, id)) <= 0.0)
    return;
  ctl->setParamNormalized(id, norm);
  ctl->performEdit(id, norm);
  invalidate();
}

void BbmView::endDrag() {
  if (mDragKnob < 0)
    return;
  if (Vst::EditController *const ctl = getController())
    ctl->endEdit(at(layout::kKnobs, mDragKnob).id);
  mDragKnob = -1;
  invalidate();
}

void BbmView::onMouseDown(int x, int y, int button) {
  if (button != 1 || getController() == nullptr)
    return;
  float lx = 0.0f, ly = 0.0f;
  if (!toLogical(x, y, lx, ly))
    return;

  // The footswitch acts on the press, the way a switch does.
  if (hitFootswitch(lx, ly)) {
    editParam(kSwitchId, engaged() ? 0.0 : 1.0);
    return;
  }
  const int k = hitKnob(lx, ly);
  if (k < 0)
    return;
  endDrag(); // a second button-1 press without a release: close the first gesture
  mDragKnob = k;
  mDragStartY = ly;
  mDragFine = fine();
  mDragStartNorm = at(mNorm, at(layout::kKnobs, k).id);
  getController()->beginEdit(at(layout::kKnobs, k).id);
}

void BbmView::onMouseMove(int x, int y) {
  if (mDragKnob < 0)
    return;
  float lx = 0.0f, ly = 0.0f;
  toLogical(x, y, lx, ly); // outside the window is still a valid drag position
  // Pressing or releasing Shift mid-drag re-anchors, so the knob never jumps.
  if (fine() != mDragFine) {
    mDragFine = fine();
    mDragStartY = ly;
    mDragStartNorm = at(mNorm, at(layout::kKnobs, mDragKnob).id);
  }
  // Up is more. Relative, never absolute: a click never throws the knob.
  const double scale = mDragFine ? layout::kFineFactor : 1.0;
  dragTo(mDragStartNorm + (mDragStartY - ly) / layout::kKnobDragRange * scale);
}

void BbmView::onMouseUp(int /*x*/, int /*y*/, int button) {
  if (button == 1)
    endDrag();
}

void BbmView::onMouseWheel(int x, int y, int delta) {
  // Wheel up = increase, one notch = one whole edit gesture (rations-amp's
  // nudgeParam). Ignored mid-drag: the drag owns the gesture on that parameter.
  if (delta == 0 || mDragKnob >= 0)
    return;
  float lx = 0.0f, ly = 0.0f;
  if (!toLogical(x, y, lx, ly))
    return;
  const int k = hitKnob(lx, ly);
  if (k < 0)
    return;
  const Vst::ParamID id = at(layout::kKnobs, k).id;
  editParam(id, at(mNorm, id) + delta * layout::kWheelStep);
}

//------------------------------------------------------------------------
void BbmView::onDraw(cairo_t *cr) {
  // The ground, in DEVICE space, so letterbox margins are covered too.
  {
    Canvas ground(cr, nullptr);
    ground.setColour(layout::kBackground);
    cairo_paint(cr);
  }

  // From here on everything is in logical units; this is the only transform.
  cairo_save(cr);
  cairo_translate(cr, mOffX, mOffY);
  cairo_scale(cr, mScale, mScale);
  Canvas c(cr, &mFonts);
  drawBar(c);
  drawFace(c);
  drawWordmark(c);
  drawKnobs(c);
  drawLamp(c);
  drawFootswitch(c);
  cairo_restore(cr);
}

void BbmView::drawFace(Canvas &c) {
  const Rect face(0.0f, kBar, kW, static_cast<float>(layout::kFaceHeight));
  c.drawImage(mImages.getScaled("mufffbase", devicePx(layout::kFaceWidth),
                                devicePx(layout::kFaceHeight)),
              face);
}

void BbmView::drawWordmark(Canvas &c) {
  static constexpr const char *kText = "BIG BUBBLE MUFF";
  const Rect band(static_cast<float>(layout::kWordCx) - layout::kWordWidth * 0.5f,
                  static_cast<float>(layout::kWordTop) + kBar,
                  static_cast<float>(layout::kWordWidth),
                  static_cast<float>(layout::kWordHeight));
  c.setFont(Font::Wordmark);
  // The band's size is a LINE height (ascent + descent), not an em.
  c.setFontSize(c.emForLineHeight(layout::kWordHeight * layout::kWordLineFraction));
  // A faint offset under the dark ink reads as a worn screen print. The offset is
  // two device pixels at scale 1, rounded to whole device pixels at any scale.
  const auto shadow =
      static_cast<float>(static_cast<double>(std::lround(2.0 * mScale)) / mScale);
  c.setColour(layout::kWordShadow);
  c.drawCentred(kText, band.translated(shadow, shadow));
  c.setColour(layout::kWordInk);
  c.drawCentred(kText, band);
}

void BbmView::drawKnobs(Canvas &c) {
  // Pre-scaled to the knob's device size, so the rotation composites at ~1:1.
  const int px = devicePx(layout::kKnobBox);
  cairo_surface_t *const art = mImages.getScaled("dialknob", px, px);
  for (const layout::Knob &k : layout::kKnobs) {
    const double angle =
        layout::kKnobStartAngle +
        at(mNorm, k.id) * (layout::kKnobEndAngle - layout::kKnobStartAngle);
    c.drawImageRotated(art, placed(k.cx, k.cy, layout::kKnobBox), angle);
  }
}

void BbmView::drawLamp(Canvas &c) {
  // Lit when the pedal is actually in circuit: footswitch on and not host-bypassed.
  const bool lit = engaged() && mHostBypass < 0.5;
  const int px = devicePx(layout::kLedBox);
  c.drawImage(mImages.getScaled(lit ? "onlightmuff" : "offlightmuff", px, px),
              placed(layout::kLedCx, layout::kLedCy, layout::kLedBox));
}

void BbmView::drawFootswitch(Canvas &c) {
  // Depressed art while engaged, raised while bypassed.
  const int px = devicePx(layout::kFootBox);
  c.drawImage(mImages.getScaled(engaged() ? "footswitch_down" : "footswitch_up", px, px),
              placed(layout::kFootCx, layout::kFootCy, layout::kFootBox));
}

void BbmView::drawBar(Canvas &c) {
  c.setColour(layout::kBarFill);
  c.fillRect(Rect(0.0f, 0.0f, kW, kBar));
  c.setColour(layout::kBarHairline);
  c.fillRect(Rect(0.0f, kBar - 1.0f, kW, 1.0f)); // hairline under the strip
}

} // namespace bbm
