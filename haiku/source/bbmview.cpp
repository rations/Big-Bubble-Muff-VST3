// BigBubbleMuff (Haiku) — native editor implementation.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// The pedal face, composited from the SAME six PNGs the Linux build uses
// (gui/*.png, copied into the bundle's Contents/Resources/gui by CMake): the
// faceplate backdrop, five rotating knobs from one dial image, the bypass lamp
// in its two states, and the footswitch in its two states. The wordmark is
// drawn rather than blitted, exactly as on Linux -- the base art carries no
// text. Every bitmap load is NULL-checked with a flat-colour fallback, so a
// missing Resources directory degrades instead of crashing.
//
// Threading: everything here runs on the host window's looper thread (the
// kPlatformTypeHaikuBView contract) and the controller is on that same thread,
// so parameter reads/writes and paramChanged() need no locking.

#include "bbmview.h"
#include "bbmcontroller.h"
#include "bbmgeometry.h"
#include "bbmids.h"

#include <Bitmap.h>
#include <Font.h>
#include <GraphicsDefs.h>
#include <Message.h>
#include <Path.h>
#include <TranslationUtils.h>
#include <View.h>
#include <Window.h>

#include <image.h>

#include <cmath>
#include <cstdio>

using namespace Steinberg;

namespace bbmh {

namespace {

rgb_color rgb(unsigned int v, uint8 alpha = 255) {
  return {static_cast<uint8>((v >> 16) & 0xffu), static_cast<uint8>((v >> 8) & 0xffu),
          static_cast<uint8>(v & 0xffu), alpha};
}

double clamp01(double v) {
  return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

// A faceplate-space box of `size` pixels centred on (cx, cy), matching the
// integer arithmetic of centredBox() in src/PluginEditor.cpp so the two editors
// place their controls on the same pixels.
BRect centredBox(int cx, int cy, int size) {
  const float left = static_cast<float>(cx - size / 2);
  const float top = static_cast<float>(cy - size / 2);
  return BRect(left, top, left + static_cast<float>(size) - 1.0f,
               top + static_cast<float>(size) - 1.0f);
}

bool hitCircle(BPoint where, float cx, float cy, float r) {
  const float dx = where.x - cx;
  const float dy = where.y - cy;
  return dx * dx + dy * dy <= r * r;
}

// Resolve the plug-in bundle's Contents directory from its own image: walk the
// loaded images for the one whose text segment contains this function, then go
// .so -> x86_64-haiku -> Contents. Never a hard-coded path.
bool contentsDir(BPath &out) {
  image_info info;
  int32 cookie = 0;
  const addr_t marker = reinterpret_cast<addr_t>(&contentsDir);
  while (get_next_image_info(0, &cookie, &info) == B_OK) {
    const addr_t text = reinterpret_cast<addr_t>(info.text);
    if (marker < text || marker >= text + static_cast<addr_t>(info.text_size))
      continue;
    BPath module(info.name), archDir;
    if (module.GetParent(&archDir) != B_OK || archDir.GetParent(&out) != B_OK)
      return false;
    return true; // out = <bundle>/Contents
  }
  return false;
}

// GetBitmapFile, not GetBitmap: the latter searches the HOST application's
// resources, which is never where a plug-in's art lives.
BBitmap *loadBitmap(const BPath &dir, const char *name) {
  BPath p(dir);
  if (p.Append(name) != B_OK)
    return nullptr;
  BBitmap *bmp = BTranslationUtils::GetBitmapFile(p.Path());
  if (bmp == nullptr)
    fprintf(stderr, "BigBubbleMuff: missing art %s (flat fallback)\n", p.Path());
  return bmp;
}

// All of the art is drawn smaller than it was authored (lamp 128 -> 40,
// footswitch 300 -> 132), so every blit filters; nearest-neighbour would alias
// the chrome and the lamp visibly.
void drawFitted(BView *v, const BBitmap *bmp, BRect dest) {
  if (bmp == nullptr)
    return;
  v->SetDrawingMode(B_OP_ALPHA);
  v->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
  v->DrawBitmap(bmp, bmp->Bounds(), dest, B_FILTER_BITMAP_BILINEAR);
}

uint8 toByte(float v) {
  const float r = v + 0.5f;
  return static_cast<uint8>(r < 0.0f ? 0.0f : (r > 255.0f ? 255.0f : r));
}

// One bilinear tap, premultiplied. Premultiplying before the interpolation is
// what stops the transparent surround from bleeding its colour into the knob's
// rim; outside the bitmap reads as fully transparent.
struct Tap {
  float b, g, r, a;
};

Tap tapPremul(const uint8 *bits, int32 bpr, int w, int h, bool hasAlpha, int x, int y) {
  if (x < 0 || y < 0 || x >= w || y >= h)
    return {0.0f, 0.0f, 0.0f, 0.0f};
  const uint8 *p = bits + static_cast<ptrdiff_t>(y) * bpr + static_cast<ptrdiff_t>(x) * 4;
  const float a = hasAlpha ? static_cast<float>(p[3]) / 255.0f : 1.0f;
  return {static_cast<float>(p[0]) * a, static_cast<float>(p[1]) * a,
          static_cast<float>(p[2]) * a, a * 255.0f};
}

// Render `src` into `dst` scaled to dst's size and rotated CLOCKWISE by `angle`
// radians about the centre, resampled bilinearly. This is the same combined
// scale-then-rotate the Linux editor asks JUCE for (drawImageTransformed with
// highResamplingQuality in MuffLookAndFeel::drawRotarySlider), done in one pass
// here because the app_server has no filtered rotated blit.
//
// Both bitmaps must be 32 bpp (B_RGBA32 or B_RGB32, which share their BGRA byte
// order) and square; the caller falls back to an unrotated blit otherwise.
bool rotateInto(BBitmap *dst, const BBitmap *src, double angle) {
  if (dst == nullptr || src == nullptr || !dst->IsValid() || !src->IsValid())
    return false;
  const color_space cs = src->ColorSpace();
  if (cs != B_RGBA32 && cs != B_RGB32)
    return false;
  const bool hasAlpha = (cs == B_RGBA32);

  const int sw = src->Bounds().IntegerWidth() + 1;
  const int sh = src->Bounds().IntegerHeight() + 1;
  const int dw = dst->Bounds().IntegerWidth() + 1;
  const int dh = dst->Bounds().IntegerHeight() + 1;
  if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
    return false;

  const uint8 *sbits = static_cast<const uint8 *>(src->Bits());
  uint8 *dbits = static_cast<uint8 *>(dst->Bits());
  if (sbits == nullptr || dbits == nullptr)
    return false;
  const int32 sbpr = src->BytesPerRow();
  const int32 dbpr = dst->BytesPerRow();

  const double cosT = std::cos(angle);
  const double sinT = std::sin(angle);
  // The art is square, so one scale factor serves both axes (an anisotropic
  // scale would not commute with the rotation).
  const double scale = static_cast<double>(sw) / static_cast<double>(dw);
  const double dcx = dw / 2.0, dcy = dh / 2.0;
  const double scx = sw / 2.0, scy = sh / 2.0;

  for (int y = 0; y < dh; ++y) {
    uint8 *row = dbits + static_cast<ptrdiff_t>(y) * dbpr;
    const double dy = (static_cast<double>(y) + 0.5) - dcy;
    for (int x = 0; x < dw; ++x) {
      const double dx = (static_cast<double>(x) + 0.5) - dcx;
      // Inverse of [dx dy] = R(angle) * [u v], then back out to source scale.
      const double u = (dx * cosT + dy * sinT) * scale + scx - 0.5;
      const double v = (-dx * sinT + dy * cosT) * scale + scy - 0.5;

      const double fx = std::floor(u), fy = std::floor(v);
      const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
      const float tx = static_cast<float>(u - fx), ty = static_cast<float>(v - fy);
      const float w00 = (1.0f - tx) * (1.0f - ty);
      const float w10 = tx * (1.0f - ty);
      const float w01 = (1.0f - tx) * ty;
      const float w11 = tx * ty;

      const Tap p00 = tapPremul(sbits, sbpr, sw, sh, hasAlpha, x0, y0);
      const Tap p10 = tapPremul(sbits, sbpr, sw, sh, hasAlpha, x0 + 1, y0);
      const Tap p01 = tapPremul(sbits, sbpr, sw, sh, hasAlpha, x0, y0 + 1);
      const Tap p11 = tapPremul(sbits, sbpr, sw, sh, hasAlpha, x0 + 1, y0 + 1);

      const float pa = p00.a * w00 + p10.a * w10 + p01.a * w01 + p11.a * w11;
      uint8 *o = row + static_cast<ptrdiff_t>(x) * 4;
      if (pa <= 0.5f) {
        o[0] = o[1] = o[2] = o[3] = 0;
        continue;
      }
      // Un-premultiply: B_RGBA32 is straight alpha.
      const float inv = 255.0f / pa;
      o[0] = toByte((p00.b * w00 + p10.b * w10 + p01.b * w01 + p11.b * w11) * inv);
      o[1] = toByte((p00.g * w00 + p10.g * w10 + p01.g * w01 + p11.g * w11) * inv);
      o[2] = toByte((p00.r * w00 + p10.r * w10 + p01.r * w01 + p11.r * w11) * inv);
      o[3] = toByte(pa);
    }
  }
  return true;
}

} // namespace

//------------------------------------------------------------------------
// MuffPanelView — the BView inside the host's parent view.
//------------------------------------------------------------------------
class MuffPanelView : public BView {
public:
  MuffPanelView(BRect frame, BigMuffController *controller)
      : BView(frame, "BigBubbleMuff-editor", B_FOLLOW_NONE, B_WILL_DRAW),
        mController(controller) {
    SetViewColor(B_TRANSPARENT_COLOR); // we repaint every pixel ourselves

    BPath contents;
    if (contentsDir(contents)) {
      BPath dir(contents);
      dir.Append("Resources/gui");
      mFace = loadBitmap(dir, "mufffbase.png");
      mKnob = loadBitmap(dir, "dialknob.png");
      mLedOn = loadBitmap(dir, "onlightmuff.png");
      mLedOff = loadBitmap(dir, "offlightmuff.png");
      mFootUp = loadBitmap(dir, "footswitch_up.png");
      mFootDown = loadBitmap(dir, "footswitch_down.png");
    } else {
      fprintf(stderr, "BigBubbleMuff: cannot locate bundle Resources (flat fallback)\n");
    }

    mWordFont = *be_bold_font;
    mWordFont.SetFace(B_BOLD_FACE | B_ITALIC_FACE);
    mWordFont.SetSize(geo::kWordSize);
  }

  ~MuffPanelView() override {
    delete mFace;
    delete mKnob;
    delete mLedOn;
    delete mLedOff;
    delete mFootUp;
    delete mFootDown;
    for (BBitmap *bmp : mKnobCache)
      delete bmp;
    delete mOffView; // removed from mOff below, so delete before the bitmap
    delete mOff;
  }

  MuffPanelView(const MuffPanelView &) = delete;
  MuffPanelView &operator=(const MuffPanelView &) = delete;

  void AttachedToWindow() override {
    BView::AttachedToWindow();

    // Compose offscreen and blit, so a repaint never flickers through the
    // half-drawn faceplate.
    const BRect b = Bounds();
    mOff = new BBitmap(b, B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
    if (mOff->IsValid()) {
      mOffView = new BView(b, "offscreen", B_FOLLOW_NONE, 0);
      mOff->AddChild(mOffView);
    } else {
      delete mOff;
      mOff = nullptr;
    }
  }

  void DetachedFromWindow() override {
    if (mOff != nullptr && mOffView != nullptr && mOff->Lock()) {
      mOff->RemoveChild(mOffView);
      mOff->Unlock();
    }
    BView::DetachedFromWindow();
  }

  void Draw(BRect) override {
    if (mOff != nullptr && mOffView != nullptr && mOff->Lock()) {
      compose(mOffView);
      mOffView->Sync();
      mOff->Unlock();
      SetDrawingMode(B_OP_COPY);
      DrawBitmap(mOff, B_ORIGIN);
    } else {
      compose(this);
    }
  }

  void MouseDown(BPoint where) override {
    int32 clicks = 1;
    if (Window() != nullptr && Window()->CurrentMessage() != nullptr)
      Window()->CurrentMessage()->FindInt32("clicks", &clicks);

    // Footswitch: circular so the transparent corners of the art stay inert.
    if (hitCircle(where, static_cast<float>(geo::kFootCx),
                  static_cast<float>(geo::kFootCy), geo::kFootHitR)) {
      const double bypassed = mController->getParamNormalized(kBypassId);
      editParam(kBypassId, bypassed >= 0.5 ? 0.0 : 1.0);
      Invalidate();
      return;
    }

    for (const geo::KnobSpec &k : geo::kKnobs) {
      if (!hitCircle(where, static_cast<float>(k.cx), static_cast<float>(k.cy),
                     geo::kKnobBox / 2.0f))
        continue;
      if (clicks == 2) {
        editParam(k.id, defaultOf(k.id));
        Invalidate();
      } else {
        startDrag(k.id, where);
      }
      return;
    }

    BView::MouseDown(where);
  }

  void MouseMoved(BPoint where, uint32, const BMessage *) override {
    if (!mDragging)
      return;
    // juce::Slider's RotaryVerticalDrag: up increases, 250 px is full range.
    const double norm =
        clamp01(mDragStartNorm + static_cast<double>(mDragStartY - where.y) /
                                     static_cast<double>(geo::kKnobDragRange));
    mController->setParamNormalized(mDragParam, norm);
    mController->performEdit(mDragParam, norm);
    Invalidate();
  }

  void MouseUp(BPoint) override {
    if (!mDragging)
      return;
    mController->endEdit(mDragParam);
    mDragging = false;
  }

  void MessageReceived(BMessage *message) override {
    if (message->what == B_MOUSE_WHEEL_CHANGED) {
      float dy = 0.0f;
      if (message->FindFloat("be:wheel_delta_y", &dy) == B_OK && dy != 0.0f)
        handleWheel(dy);
      return;
    }
    BView::MessageReceived(message);
  }

  // Called by BigMuffEditorView on the host window's looper thread.
  void paramChanged(Vst::ParamID, Vst::ParamValue) { Invalidate(); }

private:
  //--- parameter plumbing ------------------------------------------------
  double defaultOf(Vst::ParamID id) const {
    if (Vst::Parameter *p = mController->getParameterObject(id))
      return p->getInfo().defaultNormalizedValue;
    return 0.0;
  }

  void editParam(Vst::ParamID id, double norm) {
    mController->beginEdit(id);
    mController->setParamNormalized(id, norm);
    mController->performEdit(id, norm);
    mController->endEdit(id);
  }

  void startDrag(Vst::ParamID id, BPoint where) {
    mDragging = true;
    mDragParam = id;
    mDragStartY = where.y;
    mDragStartNorm = mController->getParamNormalized(id);
    mController->beginEdit(id);
    SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
  }

  void handleWheel(float dy) {
    BPoint where;
    uint32 buttons = 0;
    GetMouse(&where, &buttons, false);
    const double step = -static_cast<double>(dy) * 0.05;
    for (const geo::KnobSpec &k : geo::kKnobs) {
      if (!hitCircle(where, static_cast<float>(k.cx), static_cast<float>(k.cy),
                     geo::kKnobBox / 2.0f))
        continue;
      editParam(k.id, clamp01(mController->getParamNormalized(k.id) + step));
      Invalidate();
      return;
    }
  }

  //--- drawing -----------------------------------------------------------
  void compose(BView *v) {
    // Behind any transparent faceplate corner, as the Linux editor's fillAll.
    v->SetDrawingMode(B_OP_COPY);
    v->SetHighColor(rgb(geo::kBackdrop));
    v->FillRect(v->Bounds());
    drawFitted(v, mFace, v->Bounds());

    drawWordmark(v);

    for (int i = 0; i < geo::kKnobCount; ++i)
      drawKnob(v, i);

    const bool engaged = mController->getParamNormalized(kBypassId) < 0.5;
    drawFitted(v, engaged ? mLedOn : mLedOff,
               centredBox(geo::kLedCx, geo::kLedCy, geo::kLedBox));
    drawFitted(v, engaged ? mFootDown : mFootUp,
               centredBox(geo::kFootCx, geo::kFootCy, geo::kFootBox));
  }

  // "BIG BUBBLE MUFF" as a worn stencil: a soft shadow offset under dark green
  // ink, shrunk to fit the band -- the same treatment as the Linux paint().
  void drawWordmark(BView *v) {
    static const char *const kWord = "BIG BUBBLE MUFF";

    BFont font(mWordFont);
    v->SetFont(&font);
    const float width = v->StringWidth(kWord);
    if (width > static_cast<float>(geo::kWordWidth) && width > 0.0f) {
      font.SetSize(font.Size() * static_cast<float>(geo::kWordWidth) / width);
      v->SetFont(&font);
    }

    font_height fh;
    v->GetFontHeight(&fh);
    const float x = static_cast<float>(geo::kWordCx) - v->StringWidth(kWord) / 2.0f;
    const float y =
        static_cast<float>(geo::kWordTop) +
        (static_cast<float>(geo::kWordHeight) + fh.ascent - fh.descent) / 2.0f;

    v->SetDrawingMode(B_OP_ALPHA);
    v->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
    v->SetHighColor(rgb(0x000000, 0x55));
    v->DrawString(kWord, BPoint(x + geo::kWordShadow, y + geo::kWordShadow));
    v->SetHighColor(rgb(0x1a2110, 0xd1));
    v->DrawString(kWord, BPoint(x, y));
  }

  void drawKnob(BView *v, int index) {
    const geo::KnobSpec &k = geo::kKnobs[index];
    const BRect box = centredBox(k.cx, k.cy, geo::kKnobBox);
    const double norm = clamp01(mController->getParamNormalized(k.id));

    if (mKnob == nullptr) {
      v->SetDrawingMode(B_OP_OVER);
      v->SetHighColor(40, 40, 40);
      v->FillEllipse(box);
      return;
    }

    // Rotating five knobs costs a few hundred microseconds, so the result is
    // cached per knob and rebuilt only when that knob's value actually moves.
    BBitmap *&cache = mKnobCache[static_cast<size_t>(index)];
    if (cache == nullptr) {
      cache = new BBitmap(BRect(0.0f, 0.0f, geo::kKnobBox - 1.0f, geo::kKnobBox - 1.0f),
                          B_RGBA32);
      mKnobCacheNorm[static_cast<size_t>(index)] = -1.0;
    }
    if (!cache->IsValid()) {
      drawFitted(v, mKnob, box); // last-resort: unrotated
      return;
    }
    if (std::fabs(norm - mKnobCacheNorm[static_cast<size_t>(index)]) > 1e-6) {
      const double angle = geo::kKnobStartRad + norm * geo::kKnobSweepRad;
      if (!rotateInto(cache, mKnob, angle)) {
        drawFitted(v, mKnob, box); // unsupported source format
        return;
      }
      mKnobCacheNorm[static_cast<size_t>(index)] = norm;
    }

    v->SetDrawingMode(B_OP_ALPHA);
    v->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
    v->DrawBitmap(cache, cache->Bounds(), box);
  }

  BigMuffController *mController;

  BFont mWordFont;

  BBitmap *mFace = nullptr;
  BBitmap *mKnob = nullptr;
  BBitmap *mLedOn = nullptr;
  BBitmap *mLedOff = nullptr;
  BBitmap *mFootUp = nullptr;
  BBitmap *mFootDown = nullptr;

  BBitmap *mKnobCache[geo::kKnobCount] = {};
  double mKnobCacheNorm[geo::kKnobCount] = {-1.0, -1.0, -1.0, -1.0, -1.0};

  BBitmap *mOff = nullptr;
  BView *mOffView = nullptr;

  bool mDragging = false;
  Vst::ParamID mDragParam = 0;
  float mDragStartY = 0.0f;
  double mDragStartNorm = 0.0;
};

//------------------------------------------------------------------------
// BigMuffEditorView
//------------------------------------------------------------------------

BigMuffEditorView::BigMuffEditorView(BigMuffController *controller)
    : HaikuPlugView(controller) {
  ViewRect size(0, 0, geo::kWinW, geo::kWinH);
  setRect(size);
}

BView *BigMuffEditorView::createHaikuView(BRect frame) {
  mPanel = new MuffPanelView(frame, static_cast<BigMuffController *>(getController()));
  return mPanel;
}

void BigMuffEditorView::removedFromParent() {
  mPanel = nullptr; // HaikuPlugView deletes the BView
  HaikuPlugView::removedFromParent();
}

void BigMuffEditorView::paramChanged(Vst::ParamID id, Vst::ParamValue value) {
  if (mPanel != nullptr)
    mPanel->paramChanged(id, value);
}

} // namespace bbmh
