// BigBubbleMuff — the art and fonts compiled into the plug-in.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// The editor reads nothing from disk: the PNGs in gui/ and the fonts in
// resources/fonts/ are turned into arrays at build time by
// tools/embed_resources.cpp (see CMakeLists.txt), which defines findResource().
// This keeps CLAUDE.md §4's rule that the user-preset directory is the plug-in's
// only filesystem access, exactly as the JUCE build's BinaryData did.
//
// The bytes have static storage duration, which FreeType relies on: a face made
// with FT_New_Memory_Face reads straight out of them for as long as it lives.
#pragma once

#include <span>
#include <string_view>

namespace bbm {

// `name` is the path relative to the resource root: "img/mufffbase.png",
// "fonts/LiberationSans-BoldItalic.ttf". Empty for an unknown name.
std::span<const unsigned char> findResource(std::string_view name) noexcept;

} // namespace bbm
