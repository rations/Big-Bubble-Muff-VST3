// BigBubbleMuff — editor geometry, in logical units.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Carried over unchanged from the owner's layout for the previous editor, tuned
// against gui/mufffbase.png (500x750): knobs three-over-two, the lamp in the gap,
// the wordmark below, the footswitch near the bottom. The faceplate sits under a
// 40-unit preset bar; everything below is in FACEPLATE space and is pushed down by
// kBarHeight when drawn. The window scales all of it by one factor with the
// aspect ratio locked.
#pragma once

#include "plugin/ids.h"

#include <array>

namespace bbm::layout {

inline constexpr int kFaceWidth = 500;  // faceplate art width
inline constexpr int kFaceHeight = 750; // faceplate art height
inline constexpr int kBarHeight = 40;   // preset strip on top
inline constexpr int kWidth = kFaceWidth;
inline constexpr int kHeight = kFaceHeight + kBarHeight;

inline constexpr double kMinScale = 0.5;
inline constexpr double kMaxScale = 2.0;

// The faceplate has a faux-3D right edge, so the true face is defined by the four
// corner screws (~x = {42, 415}, y = {40, 712}) and its centre is ~(228, 376).
inline constexpr int kFaceCx = 228;

struct Knob {
  ParamId id;
  int cx, cy; // faceplate space
};
inline constexpr int kKnobBox = 118;
inline constexpr std::array<Knob, 5> kKnobs{{
    {kSustainId, 102, 152},
    {kToneId, 228, 152},
    {kVolumeId, 354, 152},
    {kOutputId, 160, 344},
    {kGateId, 296, 344},
}};
// Pointer sweep, radians clockwise from the art's own (straight-up) orientation.
inline constexpr double kKnobStartAngle = 1.25 * 3.14159265358979323846;
inline constexpr double kKnobEndAngle = 2.75 * 3.14159265358979323846;

inline constexpr int kLedBox = 40;
inline constexpr int kLedCx = kFaceCx;
inline constexpr int kLedCy = 248;

inline constexpr int kFootBox = 132;
inline constexpr int kFootCx = kFaceCx;
inline constexpr int kFootCy = 600;

inline constexpr int kWordCx = kFaceCx;
inline constexpr int kWordWidth = 446;
inline constexpr int kWordTop = 446;
inline constexpr int kWordHeight = 64;
// The wordmark's line height as a fraction of its band.
inline constexpr float kWordLineFraction = 0.78f;

// Palette, 0xAARRGGBB.
inline constexpr unsigned kBackground = 0xff2f3326; // behind transparent corners
inline constexpr unsigned kWordShadow = 0x55000000; // soft drop shadow
inline constexpr unsigned kWordInk = 0xd11a2110;    // worn dark-green ink
inline constexpr unsigned kBarFill = 0xff20231b;
inline constexpr unsigned kBarHairline = 0xff0d0f08;
inline constexpr unsigned kComboFill = 0xff14160f;
inline constexpr unsigned kComboText = 0xffd8d8c4;
inline constexpr unsigned kComboOutline = 0xff3a3f30;

// Knob feel, from the owner's rations-amp editor (rationsview.cpp): a full sweep
// is 200 logical units of vertical drag, one wheel notch is 0.05 of the range.
// Shift divides the drag by four, as rations-pedals' fine adjust does.
inline constexpr double kKnobDragRange = 200.0;
inline constexpr double kWheelStep = 0.05;
inline constexpr double kFineFactor = 0.25;

} // namespace bbm::layout
