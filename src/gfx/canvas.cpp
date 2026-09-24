// BigBubbleMuff — Canvas implementation. See canvas.h for the conventions.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Adapted from the owner's rations-pedals src/gfx/canvas.cpp (MIT).
#include "gfx/canvas.h"

#include "gfx/cairoptr.h"
#include "gfx/fontstack.h"

#include <algorithm>
#include <numbers>

namespace bbm {

namespace {

constexpr double kPi = std::numbers::pi;

// Append a rounded-rect path: four arcs joined by the straight edges.
void roundRectPath(cairo_t *cr, const Rect &r, float radius) {
  const double maxR = std::min(r.w, r.h) * 0.5;
  const double rad = std::min(static_cast<double>(radius), maxR);
  if (rad <= 0.0) {
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    return;
  }
  const double l = r.x, t = r.y, rt = r.right(), b = r.bottom();
  cairo_new_sub_path(cr);
  cairo_arc(cr, rt - rad, t + rad, rad, -0.5 * kPi, 0.0);
  cairo_arc(cr, rt - rad, b - rad, rad, 0.0, 0.5 * kPi);
  cairo_arc(cr, l + rad, b - rad, rad, 0.5 * kPi, kPi);
  cairo_arc(cr, l + rad, t + rad, rad, kPi, 1.5 * kPi);
  cairo_close_path(cr);
}

double channel(std::uint32_t argb, unsigned shift) {
  return static_cast<double>((argb >> shift) & 0xFFU) / 255.0;
}

} // namespace

Canvas::Canvas(cairo_t *cr, const FontStack *fonts) : mCr(cr), mFonts(fonts) {
  cairo_set_line_cap(mCr, CAIRO_LINE_CAP_BUTT);
  cairo_set_line_join(mCr, CAIRO_LINE_JOIN_MITER);

  // Metric hinting OFF. The editor draws in logical units inside one
  // cairo_scale(s, s), so a string's width has to be s times its width at scale 1
  // or every centred legend walks as the window is dragged; with metric hinting
  // each advance is rounded to a whole device pixel. SLIGHT keeps the vertical
  // grid-fitting that stops small text going soft.
  const FontOptionsPtr opts(cairo_font_options_create());
  if (cairo_font_options_status(opts.get()) == CAIRO_STATUS_SUCCESS) {
    cairo_font_options_set_hint_style(opts.get(), CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_hint_metrics(opts.get(), CAIRO_HINT_METRICS_OFF);
    cairo_set_font_options(mCr, opts.get());
  }
  applyFont();
}

void Canvas::setColour(std::uint32_t argb) {
  cairo_set_source_rgba(mCr, channel(argb, 16), channel(argb, 8), channel(argb, 0),
                        channel(argb, 24));
}

void Canvas::setPenSize(float px) {
  cairo_set_line_width(mCr, px);
}

void Canvas::fillRect(const Rect &r) {
  cairo_rectangle(mCr, r.x, r.y, r.w, r.h);
  cairo_fill(mCr);
}

void Canvas::fillRoundRect(const Rect &r, float radius) {
  roundRectPath(mCr, r, radius);
  cairo_fill(mCr);
}

void Canvas::strokeRoundRect(const Rect &r, float radius) {
  // Inset by half the pen so the stroke lands inside the rect.
  const auto half = static_cast<float>(cairo_get_line_width(mCr) * 0.5);
  roundRectPath(mCr, r.inset(half), radius);
  cairo_stroke(mCr);
}

void Canvas::strokeLine(float x0, float y0, float x1, float y1) {
  cairo_move_to(mCr, x0, y0);
  cairo_line_to(mCr, x1, y1);
  cairo_stroke(mCr);
}

void Canvas::fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2) {
  cairo_move_to(mCr, x0, y0);
  cairo_line_to(mCr, x1, y1);
  cairo_line_to(mCr, x2, y2);
  cairo_close_path(mCr);
  cairo_fill(mCr);
}

void Canvas::drawImage(cairo_surface_t *image, const Rect &dest) {
  if (image == nullptr || dest.w <= 0.0f || dest.h <= 0.0f)
    return;
  const int iw = cairo_image_surface_get_width(image);
  const int ih = cairo_image_surface_get_height(image);
  if (iw <= 0 || ih <= 0)
    return;
  cairo_save(mCr);
  cairo_translate(mCr, dest.x, dest.y);
  cairo_scale(mCr, dest.w / static_cast<double>(iw), dest.h / static_cast<double>(ih));
  cairo_set_source_surface(mCr, image, 0.0, 0.0);
  cairo_pattern_set_filter(cairo_get_source(mCr), CAIRO_FILTER_GOOD);
  cairo_rectangle(mCr, 0.0, 0.0, iw, ih);
  cairo_fill(mCr);
  cairo_restore(mCr);
}

void Canvas::drawImageRotated(cairo_surface_t *image, const Rect &dest, double angle) {
  if (image == nullptr || dest.w <= 0.0f || dest.h <= 0.0f)
    return;
  const int iw = cairo_image_surface_get_width(image);
  const int ih = cairo_image_surface_get_height(image);
  if (iw <= 0 || ih <= 0)
    return;
  cairo_save(mCr);
  // Pivot about the destination centre.
  cairo_translate(mCr, dest.centreX(), dest.centreY());
  cairo_rotate(mCr, angle);
  cairo_translate(mCr, -dest.w * 0.5, -dest.h * 0.5);
  cairo_scale(mCr, dest.w / static_cast<double>(iw), dest.h / static_cast<double>(ih));
  cairo_set_source_surface(mCr, image, 0.0, 0.0);
  // GOOD, not BILINEAR: bilinear samples only 2x2 texels, and at minification a
  // pointer a few source pixels wide washes out.
  cairo_pattern_set_filter(cairo_get_source(mCr), CAIRO_FILTER_GOOD);
  cairo_rectangle(mCr, 0.0, 0.0, iw, ih);
  cairo_fill(mCr);
  cairo_restore(mCr);
}

void Canvas::applyFont() const {
  if (mFonts != nullptr) {
    cairo_font_face_t *face =
        (mFont == Font::Wordmark) ? mFonts->wordmark() : mFonts->body();
    if (face != nullptr)
      cairo_set_font_face(mCr, face);
  }
  cairo_set_font_size(mCr, mFontSize);
}

void Canvas::setFont(Font f) {
  mFont = f;
  applyFont();
}

void Canvas::setFontSize(float em) {
  mFontSize = em;
  cairo_set_font_size(mCr, em);
}

float Canvas::emForLineHeight(float height) const {
  cairo_save(mCr);
  cairo_set_font_size(mCr, 1.0);
  cairo_font_extents_t fe{};
  cairo_font_extents(mCr, &fe);
  cairo_restore(mCr);
  const double line = fe.ascent + fe.descent;
  return line > 0.0 ? static_cast<float>(height / line) : height;
}

void Canvas::drawString(const char *text, float x, float y) {
  if (text == nullptr || *text == '\0')
    return;
  cairo_move_to(mCr, x, y);
  cairo_show_text(mCr, text);
  cairo_new_path(mCr); // show_text leaves the current point set
}

float Canvas::stringWidth(const char *text) const {
  if (text == nullptr || *text == '\0')
    return 0.0f;
  cairo_text_extents_t ext{};
  cairo_text_extents(mCr, text, &ext);
  // x_advance, not width: where the next glyph would start, which is what
  // centring and truncation need.
  return static_cast<float>(ext.x_advance);
}

float Canvas::fontAscent() const {
  cairo_font_extents_t fe{};
  cairo_font_extents(mCr, &fe);
  return static_cast<float>(fe.ascent);
}

float Canvas::fontDescent() const {
  cairo_font_extents_t fe{};
  cairo_font_extents(mCr, &fe);
  return static_cast<float>(fe.descent);
}

void Canvas::drawCentred(const char *text, const Rect &box) {
  const float width = stringWidth(text);
  if (width <= 0.0f || box.w <= 0.0f)
    return;
  const float ascent = fontAscent();
  const float line = ascent + fontDescent();
  const float baseline = box.y + (box.h - line) * 0.5f + ascent;
  const float squeeze = std::min(1.0f, box.w / width);
  cairo_save(mCr);
  cairo_translate(mCr, box.centreX(), baseline);
  cairo_scale(mCr, squeeze, 1.0);
  drawString(text, -width * 0.5f, 0.0f);
  cairo_restore(mCr);
}

std::string Canvas::clipToWidth(const std::string &s, float maxW) const {
  if (stringWidth(s.c_str()) <= maxW)
    return s;
  // Trim whole UTF-8 characters off the end until the string plus an ellipsis
  // fits. Cutting mid-sequence would emit a replacement glyph.
  std::string cut = s;
  while (!cut.empty()) {
    std::size_t n = cut.size() - 1;
    while (n > 0 && (static_cast<unsigned char>(cut[n]) & 0xC0U) == 0x80U)
      --n;
    cut.resize(n);
    if (cut.empty())
      break;
    const std::string candidate = cut + "\xE2\x80\xA6"; // U+2026 HORIZONTAL ELLIPSIS
    if (stringWidth(candidate.c_str()) <= maxW)
      return candidate;
  }
  return {};
}

void Canvas::pushClip(const Rect &r) {
  cairo_save(mCr);
  cairo_rectangle(mCr, r.x, r.y, r.w, r.h);
  cairo_clip(mCr);
}

void Canvas::popClip() {
  cairo_restore(mCr);
  // cairo_restore also rolls back a font selection made after the save.
  applyFont();
}

} // namespace bbm
