// BigBubbleMuff — the editor's two typefaces, loaded with FreeType from the
// embedded font files (gfx/resources.h) and wrapped as Cairo font faces.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Adapted from the owner's rations-pedals src/gfx/fontstack.h (MIT).
//
// Liberation Sans (SIL OFL 1.1, resources/fonts/OFL.txt) is the face the JUCE
// build drew with on this machine: JUCE's Linux default sans-serif is the first of
// "Verdana", "Bitstream Vera Sans", "Luxi Sans", "Liberation Sans", ... that is
// installed (juce_Fonts_linux.cpp, getDefaultSansSerifFontName), and here that is
// Liberation Sans. Bold Italic is the wordmark, Regular the preset bar.
//
// If a face fails to load, a Cairo "toy" face of the same style stands in so text
// still renders.
#pragma once

#include "gfx/cairoptr.h"

#include <cairo/cairo.h>

#include <memory>

namespace bbm {

class FontStack {
public:
  FontStack() = default;
  ~FontStack() = default;

  FontStack(const FontStack &) = delete;
  FontStack &operator=(const FontStack &) = delete;
  FontStack(FontStack &&) = delete;
  FontStack &operator=(FontStack &&) = delete;

  // Load both faces. Safe to call more than once (the second call is a no-op).
  // Returns false if either face fell back to a toy face.
  bool load();

  cairo_font_face_t *wordmark() const { return mWordmark.get(); }
  cairo_font_face_t *body() const { return mBody.get(); }

private:
  // A cairo face that OWNS its FT_Face and a reference to the FreeType library
  // (see FaceOwner in the .cpp). Null when the embedded font will not load.
  FontFacePtr loadFace(const char *name);

  FontFacePtr mWordmark;
  FontFacePtr mBody;
  bool mRealFaces = false;

  // FT_Library, held by shared_ptr because each face keeps a reference of its own:
  // the library must outlive every face opened from it, and cairo decides when
  // those die.
  std::shared_ptr<void> mLibrary;
};

} // namespace bbm
