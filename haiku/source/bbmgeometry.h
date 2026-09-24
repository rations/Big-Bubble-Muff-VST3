// BigBubbleMuff (Haiku) — editor layout, in faceplate pixels.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// These are the Linux editor's coordinates, mirrored from the `Layout` namespace
// in src/PluginEditor.cpp, which is the source of truth: it is what the art
// (gui/mufffbase.png, 500x750) was tuned against. Two deliberate differences:
//
//   * no preset bar. The Linux editor reserves a 40 px strip on top for the user
//     preset dropdown and pushes the faceplate down by it. VST3 hosts own preset
//     management, so this editor is the faceplate alone and every Y here is a
//     faceplate coordinate with no offset.
//   * no scaling. jackDAW-haiku pins the editor to IPlugView::getSize(), so the
//     window is fixed at the art's native size and these are literal pixels.
//
// If a control moves in src/PluginEditor.cpp, move it here too.
#pragma once

#include "bbmids.h"

namespace bbmh {
namespace geo {

// Faceplate art size = editor size (gui/mufffbase.png is 500x750).
inline constexpr int kWinW = 500;
inline constexpr int kWinH = 750;

// The faceplate has a faux-3D right edge, so the true face is defined by the
// four corner screws and its centre sits left of the image centre.
inline constexpr int kFaceCx = 228;

// Behind any transparent faceplate corner (matches the Linux fillAll).
inline constexpr unsigned int kBackdrop = 0x2f3326;

// --- knobs ---------------------------------------------------------------
// The art is 128x128 and is drawn into a 118 px box, rotated over the same
// 270 degree sweep the Linux build uses (juce::Slider rotary parameters
// 1.25*pi .. 2.75*pi, set in BigBubbleMuffEditor::configureKnob).
inline constexpr int kKnobBox = 118;
inline constexpr int kKnobSrc = 128;
inline constexpr double kKnobStartRad = 1.25 * 3.14159265358979323846;
inline constexpr double kKnobSweepRad = 1.50 * 3.14159265358979323846;

// Vertical pixels of drag for the full range, matching juce::Slider's
// pixelsForFullDragExtent so a gesture feels the same on both platforms.
inline constexpr float kKnobDragRange = 250.0f;

struct KnobSpec {
  Steinberg::Vst::ParamID id;
  int cx;
  int cy;
};

// Three over two, in the Linux editor's order.
inline constexpr int kKnobCount = 5;
inline constexpr KnobSpec kKnobs[kKnobCount] = {
    {kSustainId, 102, 152}, {kToneId, 228, 152}, {kVolumeId, 354, 152},
    {kOutputId, 160, 344},  {kGateId, 296, 344},
};

// --- lamp ----------------------------------------------------------------
// Indicative only; the footswitch owns the toggle (LedComponent disables mouse
// interception on the Linux side).
inline constexpr int kLedBox = 40;
inline constexpr int kLedCx = kFaceCx;
inline constexpr int kLedCy = 248;

// --- footswitch ----------------------------------------------------------
// Circular hit test so the transparent corners of the 300x300 art stay inert,
// matching FootswitchButton::hitTest (radius = half the box).
inline constexpr int kFootBox = 132;
inline constexpr int kFootCx = kFaceCx;
inline constexpr int kFootCy = 600;
inline constexpr float kFootHitR = kFootBox / 2.0f;

// --- wordmark ------------------------------------------------------------
// The base art carries no text: "BIG BUBBLE MUFF" is drawn, shrunk to fit the
// band if the chosen face is wider than kWordWidth.
inline constexpr int kWordCx = kFaceCx;
inline constexpr int kWordWidth = 446;
inline constexpr int kWordTop = 446;
inline constexpr int kWordHeight = 64;
inline constexpr float kWordSize = kWordHeight * 0.78f;
inline constexpr int kWordShadow = 2;

} // namespace geo
} // namespace bbmh
