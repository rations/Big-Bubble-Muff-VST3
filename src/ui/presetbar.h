// BigBubbleMuff — the preset strip across the top of the editor.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// A name box, Save and Delete, as the previous editor's bar had:
//   * clicking the box opens a list of the presets on disk over the faceplate;
//     picking one loads it (every knob as its own host edit gesture);
//   * Save opens an inline name field over the box, prefilled with the current
//     name; Enter saves (replacing a preset of that name), Escape cancels;
//   * Delete arms on the first click and deletes on a second within ~3 s; any
//     other click disarms it.
//
// Pure logic over a Canvas and logical coordinates: it owns no window and knows
// nothing of X11. Everything runs on the UI thread, and so does all its I/O.
#pragma once

#include "gfx/canvas.h"
#include "presets/presetstore.h"

#include "pluginterfaces/base/ftypes.h"

#include <functional>
#include <string>
#include <vector>

namespace bbm {

class PresetBar {
public:
  struct Callbacks {
    std::function<presets::Norms()> current;           // the knobs as they are now
    std::function<void(const presets::Norms &)> apply; // load these into the host
    std::function<void(bool)> keyboardFocus;           // take / release key focus
  };

  PresetBar(presets::Store store, Callbacks callbacks);

  // Geometry, in logical units (the bar spans y = 0 .. layout::kBarHeight).
  static Rect nameBox();
  static Rect saveButton();
  static Rect deleteButton();
  Rect listRect() const;
  static constexpr float kRowHeight = 26.0f;

  void draw(Canvas &c) const;        // the strip itself
  void drawOverlay(Canvas &c) const; // the open list, above everything else

  // Each returns true when the event was the bar's (the caller then ignores it).
  bool mouseDown(float lx, float ly);
  bool mouseMove(float lx, float ly);
  bool wheel(float lx, float ly, int delta);
  bool key(Steinberg::char16 ch, Steinberg::int16 keyCode, Steinberg::int16 modifiers);

  // ~30 Hz: times out the Delete arm and status messages. True if a repaint is due.
  bool tick();

  bool listOpen() const { return mListOpen; }
  bool editing() const { return mEditing; }
  const std::string &currentName() const { return mCurrent; }
  const std::string &editText() const { return mEdit; }

private:
  void openList();
  void closeList();
  void pick(std::size_t index);
  void beginEdit();
  void endEdit(bool commit);
  void deleteClicked();
  void status(const char *text);
  int visibleRows() const;
  int rowAt(float lx, float ly) const;

  presets::Store mStore;
  Callbacks mCb;

  std::string mCurrent; // name shown in the box
  std::vector<std::string> mNames;
  bool mListOpen = false;
  int mScroll = 0; // first visible row
  int mHover = -1;

  bool mEditing = false;
  std::string mEdit;
  std::size_t mCaret = 0;

  int mArmTicks = 0; // > 0 while Delete is armed
  std::string mStatus;
  int mStatusTicks = 0;
};

} // namespace bbm
