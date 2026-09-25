// BigBubbleMuff — the drawing surface the editor paints through.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Adapted from the owner's rations-pedals src/gfx/canvas.h (MIT), trimmed to the
// primitives this editor uses.
//
// Conventions, pinned here once:
//   * Rect is {x, y, w, h} with EXCLUSIVE right/bottom edges.
//   * Colours are 0xAARRGGBB — the form the editor's palette was written in.
//   * Rotation angles are radians, clockwise positive (Cairo's y-down default).
#pragma once

#include <cairo/cairo.h>

#include <cstdint>
#include <string>

namespace bbm {

class FontStack;

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;

  constexpr Rect() = default;
  constexpr Rect(float ax, float ay, float aw, float ah) : x(ax), y(ay), w(aw), h(ah) {}

  // A size x size square centred on (cx, cy).
  static constexpr Rect centred(float cx, float cy, float size) {
    return {cx - size * 0.5f, cy - size * 0.5f, size, size};
  }

  constexpr float right() const { return x + w; }
  constexpr float bottom() const { return y + h; }
  constexpr float centreX() const { return x + w * 0.5f; }
  constexpr float centreY() const { return y + h * 0.5f; }

  constexpr bool contains(float px, float py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }

  constexpr Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
  constexpr Rect translated(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
};

enum class Font { Body, Wordmark };

class Canvas {
public:
  // Does not take ownership; both must outlive the Canvas. `fonts` may be null.
  Canvas(cairo_t *cr, const FontStack *fonts);

  cairo_t *cr() const { return mCr; }

  //--- state ---------------------------------------------------------
  void setColour(std::uint32_t argb);
  void setPenSize(float px);

  //--- shapes --------------------------------------------------------
  void fillRect(const Rect &r);
  void fillRoundRect(const Rect &r, float radius);
  void strokeRoundRect(const Rect &r, float radius);
  void strokeLine(float x0, float y0, float x1, float y1);
  void fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2);

  //--- images --------------------------------------------------------
  // Scale `image` to fill `dest`, alpha-composited. No-op when null.
  void drawImage(cairo_surface_t *image, const Rect &dest);
  // Scale to fill `dest`, then rotate about the centre of `dest` by `angle`
  // radians, clockwise. The art must be square and centred on its own pivot.
  void drawImageRotated(cairo_surface_t *image, const Rect &dest, double angle);

  //--- text ----------------------------------------------------------
  void setFont(Font f);
  // Cairo's size: the em square, in the current user units.
  void setFontSize(float em);
  // The em size at which this font's line (ascent + descent) is `height` tall.
  float emForLineHeight(float height) const;
  // x, y is the baseline origin of the first glyph.
  void drawString(const char *text, float x, float y);
  // Advance width at the current font.
  float stringWidth(const char *text) const;
  float fontAscent() const;
  float fontDescent() const;
  // Draw one line centred in `box` (vertically by its line box), squeezed
  // horizontally when it would overrun the box's width.
  void drawCentred(const char *text, const Rect &box);
  // Truncate with an ellipsis until it fits maxW at the current font.
  std::string clipToWidth(const std::string &s, float maxW) const;

  //--- clipping ------------------------------------------------------
  void pushClip(const Rect &r);
  void popClip();

private:
  void applyFont() const;

  cairo_t *mCr = nullptr;
  const FontStack *mFonts = nullptr;
  Font mFont = Font::Body;
  float mFontSize = 12.0f;
};

} // namespace bbm
