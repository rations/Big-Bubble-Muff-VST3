// BigBubbleMuff — FontStack implementation. See fontstack.h.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Adapted from the owner's rations-pedals src/gfx/fontstack.cpp (MIT).
#include "gfx/fontstack.h"

#include "gfx/resources.h"

#include <cairo/cairo-ft.h>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace bbm {
namespace {

// Everything a cairo font face needs kept alive for as long as CAIRO says, not for
// as long as the FontStack says. Attached to the face as user data, so cairo frees
// it when it drops its last internal reference.
//
// cairo caches font faces process-wide, keyed on the FT_Face POINTER
// (cairo-ft-font.c, _cairo_ft_unscaled_font_keys_equal). Calling FT_Done_Face()
// after cairo_font_face_destroy() leaves that cache entry dangling, and the next
// face allocated at the same address inherits its glyphs. cairo documents the rule
// on cairo_ft_font_face_create_for_ft_face(): "You must not call FT_Done_Face()
// before the last reference to the cairo_font_face_t has been dropped", and
// prescribes this user-data callback. The font bytes are embedded (static), so
// nothing else needs keeping.
struct FaceOwner {
  FT_Face face = nullptr;
  std::shared_ptr<void> library; // FT_Library, released after the face
};

const cairo_user_data_key_t kFaceOwnerKey = {};

void destroyFaceOwner(void *data) {
  // Ownership was handed to cairo with release() in loadFace; this is its return.
  const std::unique_ptr<FaceOwner> owner(static_cast<FaceOwner *>(data));
  if (owner->face != nullptr)
    FT_Done_Face(owner->face);
}

FontFacePtr toyFace(bool boldItalic) {
  return FontFacePtr(cairo_toy_font_face_create(
      "sans-serif", boldItalic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL,
      boldItalic ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL));
}

} // namespace

FontFacePtr FontStack::loadFace(const char *name) {
  if (!mLibrary)
    return nullptr;
  const std::span<const unsigned char> bytes = findResource(name);
  if (bytes.empty())
    return nullptr;

  FT_Face face = nullptr;
  if (FT_New_Memory_Face(static_cast<FT_Library>(mLibrary.get()), bytes.data(),
                         static_cast<FT_Long>(bytes.size()), 0, &face) != 0)
    return nullptr;

  FontFacePtr cf(cairo_ft_font_face_create_for_ft_face(face, 0));
  if (!cf || cairo_font_face_status(cf.get()) != CAIRO_STATUS_SUCCESS) {
    cf.reset();
    FT_Done_Face(face);
    return nullptr;
  }
  // Hand the face and a library reference to cairo BEFORE the face is used, and
  // treat a failure to do so as a failed load: a face cairo cannot be made to own
  // is a face nobody can safely free.
  auto owner = std::make_unique<FaceOwner>(FaceOwner{face, mLibrary});
  if (cairo_font_face_set_user_data(cf.get(), &kFaceOwnerKey, owner.get(),
                                    destroyFaceOwner) != CAIRO_STATUS_SUCCESS) {
    cf.reset();
    FT_Done_Face(face);
    return nullptr;
  }
  // Now cairo's: destroyFaceOwner frees it, so the pointer is deliberately dropped.
  owner.release(); // NOLINT(bugprone-unused-return-value)
  return cf;
}

bool FontStack::load() {
  if (mWordmark && mBody)
    return mRealFaces;

  FT_Library lib = nullptr;
  if (FT_Init_FreeType(&lib) == 0)
    mLibrary = std::shared_ptr<void>(
        lib, [](void *p) { FT_Done_FreeType(static_cast<FT_Library>(p)); });

  mWordmark = loadFace("fonts/LiberationSans-BoldItalic.ttf");
  mBody = loadFace("fonts/LiberationSans-Regular.ttf");
  mRealFaces = mWordmark && mBody;
  if (!mWordmark)
    mWordmark = toyFace(true);
  if (!mBody)
    mBody = toyFace(false);
  return mRealFaces;
}

} // namespace bbm
