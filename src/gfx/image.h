// BigBubbleMuff — the editor's raster art, decoded from the embedded PNGs.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Adapted from the owner's rations-pedals src/gfx/image.h (MIT).
//
// Cairo reads PNG natively (cairo_image_surface_create_from_png_stream), so nothing
// links libpng directly. A layer that will not decode yields a cached null, which
// the Canvas image calls skip, so the editor degrades to its flat background.
//
// PRE-SCALING. The editor is host-resizable, so the art is almost never drawn at
// its stored size, and re-running Cairo's GOOD filter over every layer of every
// frame is the most expensive thing the editor could do (rations-pedals measured
// 3.7 ms per frame for one downscaled layer, against 0.03 ms for a 1:1 blit).
// getScaled() caches a layer at an exact pixel size; purgeScaled() drops the set
// on a resize, so dragging a window edge does not accumulate surfaces.
#pragma once

#include "gfx/cairoptr.h"

#include <cairo/cairo.h>

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <tuple>

namespace bbm {

class ImageCache {
public:
  ImageCache() = default;
  ~ImageCache() = default;

  ImageCache(const ImageCache &) = delete;
  ImageCache &operator=(const ImageCache &) = delete;
  ImageCache(ImageCache &&) = delete;
  ImageCache &operator=(ImageCache &&) = delete;

  // "img/<name>.png" from the embedded set. Owned by the cache; may be null.
  cairo_surface_t *get(std::string_view name);

  // The same layer resampled to exactly w x h pixels, cached and owned here. Null
  // if the layer is missing or the size is not positive.
  cairo_surface_t *getScaled(std::string_view name, int w, int h);

  // Drop every pre-scaled entry, keeping the originals. Call on a resize.
  void purgeScaled() { mScaled.clear(); }

private:
  struct ScaledKey {
    std::string name;
    int w = 0;
    int h = 0;
    bool operator<(const ScaledKey &o) const {
      return std::tie(name, w, h) < std::tie(o.name, o.w, o.h);
    }
  };

  std::map<std::string, SurfacePtr, std::less<>> mCache; // null entry = load failed
  std::map<ScaledKey, SurfacePtr> mScaled;
};

} // namespace bbm
