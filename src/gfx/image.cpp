// BigBubbleMuff — ImageCache implementation. See image.h.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Adapted from the owner's rations-pedals src/gfx/image.cpp (MIT).
#include "gfx/image.h"

#include "gfx/resources.h"

#include <cstring>
#include <span>

namespace bbm {
namespace {

// Cairo reads a PNG through a callback, which is all an in-memory image needs.
struct MemoryPng {
  std::span<const unsigned char> bytes;
  std::size_t pos = 0;
};

cairo_status_t readMemoryPng(void *closure, unsigned char *out, unsigned int length) {
  auto *src = static_cast<MemoryPng *>(closure);
  if (length > src->bytes.size() - src->pos)
    return CAIRO_STATUS_READ_ERROR;
  std::memcpy(out, src->bytes.subspan(src->pos, length).data(), length);
  src->pos += length;
  return CAIRO_STATUS_SUCCESS;
}

} // namespace

cairo_surface_t *ImageCache::get(std::string_view name) {
  const auto it = mCache.find(name);
  if (it != mCache.end())
    return it->second.get(); // may be null: a previous failure

  std::string rel = "img/";
  rel += name;
  rel += ".png";
  SurfacePtr surface;
  const std::span<const unsigned char> bytes = findResource(rel);
  if (!bytes.empty()) {
    MemoryPng src{bytes, 0};
    surface.reset(cairo_image_surface_create_from_png_stream(&readMemoryPng, &src));
    if (cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS)
      surface.reset();
  }
  cairo_surface_t *const raw = surface.get();
  mCache.emplace(std::string(name), std::move(surface));
  return raw;
}

cairo_surface_t *ImageCache::getScaled(std::string_view name, int w, int h) {
  if (w <= 0 || h <= 0)
    return nullptr;

  ScaledKey key{std::string(name), w, h};
  const auto it = mScaled.find(key);
  if (it != mScaled.end())
    return it->second.get();

  cairo_surface_t *const source = get(name);
  SurfacePtr scaled;
  if (source != nullptr) {
    const int sw = cairo_image_surface_get_width(source);
    const int sh = cairo_image_surface_get_height(source);
    if (sw > 0 && sh > 0) {
      scaled.reset(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h));
      if (cairo_surface_status(scaled.get()) != CAIRO_STATUS_SUCCESS) {
        scaled.reset();
      } else {
        const ContextPtr cr(cairo_create(scaled.get()));
        if (cairo_status(cr.get()) == CAIRO_STATUS_SUCCESS) {
          cairo_scale(cr.get(), w / static_cast<double>(sw), h / static_cast<double>(sh));
          cairo_set_source_surface(cr.get(), source, 0.0, 0.0);
          // The one place the expensive filter runs: everything downstream is a
          // 1:1 blit of the result.
          cairo_pattern_set_filter(cairo_get_source(cr.get()), CAIRO_FILTER_GOOD);
          cairo_paint(cr.get());
        }
        cairo_surface_flush(scaled.get());
      }
    }
  }
  cairo_surface_t *const raw = scaled.get();
  mScaled.emplace(std::move(key), std::move(scaled));
  return raw;
}

} // namespace bbm
