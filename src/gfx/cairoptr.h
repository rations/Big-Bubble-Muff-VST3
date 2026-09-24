// BigBubbleMuff — RAII owners for Cairo objects.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#pragma once

#include <cairo/cairo.h>

#include <memory>

namespace bbm {

struct SurfaceDeleter {
  void operator()(cairo_surface_t *s) const noexcept { cairo_surface_destroy(s); }
};
using SurfacePtr = std::unique_ptr<cairo_surface_t, SurfaceDeleter>;

struct ContextDeleter {
  void operator()(cairo_t *cr) const noexcept { cairo_destroy(cr); }
};
using ContextPtr = std::unique_ptr<cairo_t, ContextDeleter>;

struct FontFaceDeleter {
  void operator()(cairo_font_face_t *f) const noexcept { cairo_font_face_destroy(f); }
};
using FontFacePtr = std::unique_ptr<cairo_font_face_t, FontFaceDeleter>;

struct FontOptionsDeleter {
  void operator()(cairo_font_options_t *o) const noexcept {
    cairo_font_options_destroy(o);
  }
};
using FontOptionsPtr = std::unique_ptr<cairo_font_options_t, FontOptionsDeleter>;

} // namespace bbm
