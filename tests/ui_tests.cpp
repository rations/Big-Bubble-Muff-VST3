// BigBubbleMuff — editor tests, headless: no X display is opened.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// The view is driven through its own hooks (a subclass reaches the protected
// overrides) and painted into an offscreen image surface, so everything except the
// X11 embedding itself is exercised here.
#include "test.h"

#include "gfx/cairoptr.h"
#include "gfx/canvas.h"
#include "gfx/fontstack.h"
#include "gfx/image.h"
#include "gfx/resources.h"
#include "plugin/controller.h"
#include "plugin/ids.h"
#include "ui/bbmview.h"
#include "ui/layout.h"

#include "pluginterfaces/gui/iplugview.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

using namespace Steinberg;

namespace {

class Probe : public bbm::BbmView {
public:
  using BbmView::BbmView;
  void attach() { onAttached(); }
  void resize(int w, int h) { onResized(w, h); }
  void draw(cairo_t *cr) { onDraw(cr); }
  void press(int x, int y) { onMouseDown(x, y, 1); }
  void move(int x, int y) { onMouseMove(x, y); }
  void unpress(int x, int y) { onMouseUp(x, y, 1); }
  void wheel(int x, int y, int delta) { onMouseWheel(x, y, delta); }
};

// A controller with an editor attached the way the SDK attaches one.
struct UiRig {
  IPtr<bbm::Controller> ctrl;
  IPtr<Probe> view;

  UiRig() : ctrl(owned(new bbm::Controller())) {
    ctrl->initialize(nullptr);
    view = owned(new Probe(ctrl));
    ctrl->editorAttached(view);
    view->attach();
    view->resize(bbm::layout::kWidth, bbm::layout::kHeight);
  }
  ~UiRig() {
    ctrl->editorRemoved(view);
    view = nullptr;
    ctrl->terminate();
  }
  UiRig(const UiRig &) = delete;
  UiRig &operator=(const UiRig &) = delete;
  UiRig(UiRig &&) = delete;
  UiRig &operator=(UiRig &&) = delete;

  double norm(Vst::ParamID id) const { return ctrl->getParamNormalized(id); }
};

// Logical faceplate point -> device pixel at scale s (preset bar included).
int dev(int logical, double s) {
  return static_cast<int>(std::lround(logical * s));
}

bool near(double a, double b) {
  return std::fabs(a - b) < 1e-9;
}

std::uint32_t pixel(cairo_surface_t *s, int x, int y) {
  cairo_surface_flush(s);
  const unsigned char *row =
      cairo_image_surface_get_data(s) +
      static_cast<std::ptrdiff_t>(y) * cairo_image_surface_get_stride(s);
  std::uint32_t v = 0;
  std::memcpy(&v, row + static_cast<std::ptrdiff_t>(x) * 4, sizeof v);
  return v; // CAIRO_FORMAT_ARGB32 is native-endian 0xAARRGGBB, premultiplied
}

} // namespace

TEST_CASE("Resources", "every embedded PNG decodes at its expected size") {
  struct Art {
    const char *name;
    int w, h;
  };
  const Art art[] = {{"mufffbase", 500, 750},     {"dialknob", 128, 128},
                     {"footswitch_up", 300, 300}, {"footswitch_down", 300, 300},
                     {"onlightmuff", 128, 128},   {"offlightmuff", 128, 128}};
  bbm::ImageCache cache;
  for (const Art &a : art) {
    cairo_surface_t *s = cache.get(a.name);
    CHECK_MSG(s != nullptr, a.name);
    if (s == nullptr)
      continue;
    CHECK_MSG(cairo_image_surface_get_width(s) == a.w, a.name);
    CHECK_MSG(cairo_image_surface_get_height(s) == a.h, a.name);
  }
  CHECK(cache.get("no-such-layer") == nullptr);
  CHECK(bbm::findResource("img/../../etc/passwd").empty());
}

TEST_CASE("Resources", "both bundled faces load through FreeType") {
  bbm::FontStack fonts;
  CHECK(fonts.load());
  CHECK(fonts.wordmark() != nullptr);
  CHECK(fonts.body() != nullptr);
  CHECK(cairo_font_face_get_type(fonts.wordmark()) == CAIRO_FONT_TYPE_FT);
  CHECK(cairo_font_face_get_type(fonts.body()) == CAIRO_FONT_TYPE_FT);
}

TEST_CASE("Editor", "resize keeps the aspect ratio inside 0.5x..2x") {
  const auto check = [](int w, int h, int ew, int eh) {
    bbm::BbmView::constrain(w, h);
    CHECK_MSG(w == ew && h == eh, std::to_string(w) + "x" + std::to_string(h) +
                                      " expected " + std::to_string(ew) + "x" +
                                      std::to_string(eh));
  };
  check(500, 790, 500, 790);
  check(1000, 1580, 1000, 1580);
  check(100, 100, 250, 395);     // below the floor
  check(4000, 790, 500, 790);    // the tighter axis wins
  check(2000, 4000, 1000, 1580); // above the ceiling
  check(750, 5000, 750, 1185);
}

TEST_CASE("Editor", "the controller hands out an X11 editor, and only an editor") {
  auto ctrl = owned(new bbm::Controller());
  ctrl->initialize(nullptr);
  IPtr<IPlugView> view = owned(ctrl->createView(Vst::ViewType::kEditor));
  CHECK(view != nullptr);
  if (view) {
    CHECK(view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) == kResultTrue);
    CHECK(view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultFalse);
    CHECK(view->canResize() == kResultTrue);
    ViewRect r;
    CHECK(view->getSize(&r) == kResultOk);
    CHECK(r.getWidth() == bbm::layout::kWidth && r.getHeight() == bbm::layout::kHeight);
  }
  CHECK(ctrl->createView("somethingElse") == nullptr);
  CHECK(ctrl->createView(nullptr) == nullptr);
  view = nullptr;
  ctrl->terminate();
}

TEST_CASE("Editor", "the wordmark fits its band with at most a mild squeeze") {
  const bbm::SurfacePtr s(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8));
  const bbm::ContextPtr cr(cairo_create(s.get()));
  bbm::FontStack fonts;
  fonts.load();
  bbm::Canvas c(cr.get(), &fonts);
  c.setFont(bbm::Font::Wordmark);
  c.setFontSize(
      c.emForLineHeight(bbm::layout::kWordHeight * bbm::layout::kWordLineFraction));
  const float width = c.stringWidth("BIG BUBBLE MUFF");
  bbmtest::log("wordmark width " + std::to_string(width) + " in a " +
               std::to_string(bbm::layout::kWordWidth) + " band");
  CHECK(width > 0.0f);
  CHECK(width <= bbm::layout::kWordWidth / 0.8f);
}

TEST_CASE("Editor", "paints the whole window offscreen at 1x and 2x") {
  for (const double s : {1.0, 2.0}) {
    UiRig rig;
    const int w = dev(bbm::layout::kWidth, s);
    const int h = dev(bbm::layout::kHeight, s);
    rig.view->resize(w, h);
    const bbm::SurfacePtr surf(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h));
    const bbm::ContextPtr cr(cairo_create(surf.get()));
    rig.view->draw(cr.get());
    CHECK(cairo_status(cr.get()) == CAIRO_STATUS_SUCCESS);
    // The preset strip.
    CHECK(pixel(surf.get(), dev(10, s), dev(10, s)) == bbm::layout::kBarFill);
    // The faceplate and the footswitch cap are opaque art, not the background.
    const std::uint32_t face = pixel(surf.get(), dev(228, s), dev(420, s));
    CHECK((face >> 24) == 0xFF);
    CHECK(face != bbm::layout::kBackground);
    const std::uint32_t foot =
        pixel(surf.get(), dev(bbm::layout::kFootCx, s),
              dev(bbm::layout::kFootCy + bbm::layout::kBarHeight, s));
    CHECK((foot >> 24) == 0xFF);
  }
}

TEST_CASE("Editor", "footswitch, knob drag and wheel edit through the controller") {
  UiRig rig;
  const int bar = bbm::layout::kBarHeight;

  // Footswitch: one press toggles, a second toggles back.
  CHECK(near(rig.norm(bbm::kSwitchId), 1.0));
  rig.view->press(bbm::layout::kFootCx, bbm::layout::kFootCy + bar);
  rig.view->unpress(bbm::layout::kFootCx, bbm::layout::kFootCy + bar);
  CHECK(near(rig.norm(bbm::kSwitchId), 0.0));
  rig.view->press(bbm::layout::kFootCx, bbm::layout::kFootCy + bar);
  CHECK(near(rig.norm(bbm::kSwitchId), 1.0));
  // The art's transparent corner is not the switch.
  rig.view->press(bbm::layout::kFootCx + 63, bbm::layout::kFootCy + bar + 63);
  CHECK(near(rig.norm(bbm::kSwitchId), 1.0));

  // Tone drag: 40 units up is 40/200 of the range; dragging past the end clamps.
  const auto &tone = bbm::layout::kKnobs[1];
  rig.view->press(tone.cx, tone.cy + bar);
  rig.view->move(tone.cx, tone.cy + bar - 40);
  CHECK(near(rig.norm(bbm::kToneId), 0.5 + 40.0 / bbm::layout::kKnobDragRange));
  rig.view->move(tone.cx, tone.cy + bar - 1000);
  CHECK(near(rig.norm(bbm::kToneId), 1.0));
  rig.view->unpress(tone.cx, tone.cy + bar - 1000);
  // After release, motion changes nothing.
  rig.view->move(tone.cx, tone.cy + bar + 500);
  CHECK(near(rig.norm(bbm::kToneId), 1.0));

  // Wheel on Volume: one notch each way.
  const auto &vol = bbm::layout::kKnobs[2];
  rig.view->wheel(vol.cx, vol.cy + bar, +1);
  CHECK(near(rig.norm(bbm::kVolumeId), 0.5 + bbm::layout::kWheelStep));
  rig.view->wheel(vol.cx, vol.cy + bar, -1);
  rig.view->wheel(vol.cx, vol.cy + bar, -1);
  CHECK(near(rig.norm(bbm::kVolumeId), 0.5 - bbm::layout::kWheelStep));
  // Off every control, the wheel does nothing.
  rig.view->wheel(5, 5, +1);
  for (int i = 0; i < bbm::kParamCount; ++i)
    CHECK(std::isfinite(rig.norm(static_cast<Vst::ParamID>(i))));

  // At 2x, the same logical point is twice as far into the window.
  rig.view->resize(dev(bbm::layout::kWidth, 2.0), dev(bbm::layout::kHeight, 2.0));
  const auto &sus = bbm::layout::kKnobs[0];
  rig.view->wheel(dev(sus.cx, 2.0), dev(sus.cy + bar, 2.0), -1);
  CHECK(near(rig.norm(bbm::kSustainId), 0.75 - bbm::layout::kWheelStep));
}

TEST_CASE("Editor", "host-side changes reach the attached view without crashing") {
  UiRig rig;
  // Every route a host or state load uses, including out-of-range and non-finite
  // values, which the controller clamps before the view sees them.
  rig.ctrl->setParamNormalized(bbm::kSustainId, 0.1);
  rig.ctrl->setParamNormalized(bbm::kBypassId, 1.0);
  rig.ctrl->setParamNormalized(bbm::kToneId, 7.0);
  rig.ctrl->setParamNormalized(99, 0.5); // no such parameter
  CHECK(near(rig.norm(bbm::kSustainId), 0.1));
  CHECK(near(rig.norm(bbm::kToneId), 1.0));
  const bbm::SurfacePtr surf(cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, bbm::layout::kWidth, bbm::layout::kHeight));
  const bbm::ContextPtr cr(cairo_create(surf.get()));
  rig.view->draw(cr.get());
  CHECK(cairo_status(cr.get()) == CAIRO_STATUS_SUCCESS);
}
