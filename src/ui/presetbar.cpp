// BigBubbleMuff — preset strip implementation. See presetbar.h.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#include "ui/presetbar.h"

#include "ui/layout.h"

#include "pluginterfaces/base/keycodes.h"

#include <algorithm>
#include <cmath>

using namespace Steinberg;

namespace bbm {

namespace {
// The strip's proportions, from the previous editor's bar: padding 0.14 and gaps
// 0.12 of the bar's height, buttons 1.4 heights wide.
constexpr auto kBarH = static_cast<float>(layout::kBarHeight);
constexpr float kPad = kBarH * 0.14f;
constexpr float kGap = kBarH * 0.12f;
constexpr float kButtonW = kBarH * 1.4f;
constexpr float kInnerH = kBarH - 2.0f * kPad;
constexpr float kTextInset = 8.0f;
constexpr float kLineHeight = 16.0f; // body text, as a line height
constexpr float kRadius = 3.0f;

constexpr int kArmTicks = 90;    // ~3 s at the editor's 33 ms tick
constexpr int kStatusTicks = 60; // ~2 s

// Placeholder text: the box's text colour at ~55 % alpha.
constexpr unsigned kDimText = (layout::kComboText & 0x00FFFFFFU) | 0x8C000000U;

void drawButton(Canvas &c, const Rect &r, const char *label, bool lit) {
  c.setColour(lit ? layout::kComboOutline : layout::kComboFill);
  c.fillRoundRect(r, kRadius);
  c.setColour(layout::kComboOutline);
  c.setPenSize(1.0f);
  c.strokeRoundRect(r, kRadius);
  c.setColour(layout::kComboText);
  c.drawCentred(label, r);
}
} // namespace

PresetBar::PresetBar(presets::Store store, Callbacks callbacks)
    : mStore(std::move(store)), mCb(std::move(callbacks)) {}

Rect PresetBar::deleteButton() {
  return {static_cast<float>(layout::kWidth) - kPad - kButtonW, kPad, kButtonW, kInnerH};
}

Rect PresetBar::saveButton() {
  const Rect del = deleteButton();
  return {del.x - kGap - kButtonW, kPad, kButtonW, kInnerH};
}

Rect PresetBar::nameBox() {
  const Rect save = saveButton();
  return {kPad, kPad, save.x - kGap - kPad, kInnerH};
}

int PresetBar::visibleRows() const {
  const float room = static_cast<float>(layout::kHeight) - kBarH - kPad;
  const int fit = std::max(1, static_cast<int>(room / kRowHeight));
  return std::min(fit, std::max(1, static_cast<int>(mNames.size())));
}

Rect PresetBar::listRect() const {
  const Rect box = nameBox();
  return {box.x, kBarH, box.w, kRowHeight * static_cast<float>(visibleRows())};
}

int PresetBar::rowAt(float lx, float ly) const {
  const Rect list = listRect();
  if (mNames.empty() || !list.contains(lx, ly))
    return -1;
  const int row = mScroll + static_cast<int>((ly - list.y) / kRowHeight);
  return row < static_cast<int>(mNames.size()) ? row : -1;
}

void PresetBar::status(const char *text) {
  mStatus = text;
  mStatusTicks = kStatusTicks;
}

//------------------------------------------------------------------------
void PresetBar::openList() {
  mNames = mStore.list();
  mListOpen = true;
  mHover = -1;
  // Start with the current preset in view.
  const auto it = std::find(mNames.begin(), mNames.end(), mCurrent);
  const int index = it == mNames.end() ? 0 : static_cast<int>(it - mNames.begin());
  const int maxScroll = std::max(0, static_cast<int>(mNames.size()) - visibleRows());
  mScroll = std::clamp(index - visibleRows() / 2, 0, maxScroll);
}

void PresetBar::closeList() {
  mListOpen = false;
  mHover = -1;
}

void PresetBar::pick(std::size_t index) {
  if (index >= mNames.size())
    return;
  const std::string name = mNames[index];
  closeList();
  presets::Norms norm = mCb.current ? mCb.current() : presets::Norms{};
  if (!mStore.load(name, norm)) {
    status("Could not load preset");
    return;
  }
  mCurrent = name;
  if (mCb.apply)
    mCb.apply(norm);
}

void PresetBar::beginEdit() {
  closeList();
  mEditing = true;
  mEdit = mCurrent;
  mCaret = mEdit.size();
  if (mCb.keyboardFocus)
    mCb.keyboardFocus(true);
}

void PresetBar::endEdit(bool commit) {
  if (!mEditing)
    return;
  mEditing = false;
  if (mCb.keyboardFocus)
    mCb.keyboardFocus(false);
  if (!commit)
    return;
  // Surrounding spaces are never part of a name (a trailing one is refused).
  std::string name = mEdit;
  while (!name.empty() && name.back() == ' ')
    name.pop_back();
  const std::size_t first = name.find_first_not_of(' ');
  name.erase(0, first == std::string::npos ? name.size() : first);
  if (!presets::nameIsSafe(name)) {
    status(name.empty() ? "Type a name to save" : "Invalid preset name");
    return;
  }
  if (!mCb.current || !mStore.save(name, mCb.current())) {
    status("Could not save preset");
    return;
  }
  mCurrent = name;
  status("Saved");
}

void PresetBar::deleteClicked() {
  if (mCurrent.empty()) {
    status("No preset selected");
    return;
  }
  if (mArmTicks <= 0) {
    mArmTicks = kArmTicks;
    return;
  }
  mArmTicks = 0;
  if (!mStore.remove(mCurrent)) {
    status("Could not delete preset");
    return;
  }
  mCurrent.clear();
  status("Deleted");
}

//------------------------------------------------------------------------
bool PresetBar::mouseDown(float lx, float ly) {
  const bool armed = mArmTicks > 0;
  mArmTicks = 0; // any click disarms Delete, except the one that confirms it

  if (mListOpen) {
    const int row = rowAt(lx, ly);
    if (row >= 0) {
      pick(static_cast<std::size_t>(row));
      return true;
    }
    closeList();
    // A click on the box itself only closes the list; anywhere else in the bar
    // falls through to the buttons; anywhere on the face is swallowed so a stray
    // dismissing click never turns a knob.
    if (ly >= kBarH || nameBox().contains(lx, ly))
      return true;
  }

  if (ly >= kBarH) {
    if (mEditing)
      endEdit(false); // clicking away abandons the name
    return false;
  }

  if (saveButton().contains(lx, ly)) {
    if (mEditing)
      endEdit(true);
    else
      beginEdit();
    return true;
  }
  if (deleteButton().contains(lx, ly)) {
    endEdit(false);
    mArmTicks = armed ? 1 : 0; // restore the arm so this click can confirm it
    deleteClicked();
    return true;
  }
  if (nameBox().contains(lx, ly)) {
    if (!mEditing)
      openList();
    return true;
  }
  return true; // the bar's own background
}

bool PresetBar::mouseMove(float lx, float ly) {
  if (!mListOpen)
    return false;
  const int row = rowAt(lx, ly);
  if (row == mHover)
    return false;
  mHover = row;
  return true;
}

bool PresetBar::wheel(float lx, float ly, int delta) {
  if (!mListOpen)
    return ly < kBarH; // the bar itself absorbs the wheel
  if (listRect().contains(lx, ly)) {
    const int maxScroll = std::max(0, static_cast<int>(mNames.size()) - visibleRows());
    mScroll = std::clamp(mScroll - delta, 0, maxScroll);
    mHover = rowAt(lx, ly);
  }
  return true;
}

bool PresetBar::key(char16 ch, int16 keyCode, int16 /*modifiers*/) {
  if (!mEditing) {
    if (mListOpen && keyCode == KEY_ESCAPE) {
      closeList();
      return true;
    }
    return false;
  }
  switch (keyCode) {
  case KEY_RETURN:
  case KEY_ENTER:
    endEdit(true);
    return true;
  case KEY_ESCAPE:
    endEdit(false);
    return true;
  case KEY_BACK:
    if (mCaret > 0) {
      mEdit.erase(mCaret - 1, 1);
      --mCaret;
    }
    return true;
  case KEY_DELETE:
    if (mCaret < mEdit.size())
      mEdit.erase(mCaret, 1);
    return true;
  case KEY_LEFT:
    mCaret = mCaret > 0 ? mCaret - 1 : 0;
    return true;
  case KEY_RIGHT:
    mCaret = std::min(mEdit.size(), mCaret + 1);
    return true;
  case KEY_HOME:
    mCaret = 0;
    return true;
  case KEY_END:
    mCaret = mEdit.size();
    return true;
  default:
    break;
  }
  // Only characters a name may hold are accepted, so the field never shows
  // something Save would then refuse.
  if (ch > 0 && ch < 0x7F && mEdit.size() < presets::kMaxNameLength) {
    const auto c = static_cast<char>(ch);
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == '_' ||
                    c == '-';
    if (ok) {
      mEdit.insert(mCaret, 1, c);
      ++mCaret;
    }
  }
  return true; // while editing, every key is the field's
}

bool PresetBar::tick() {
  bool repaint = false;
  if (mArmTicks > 0) {
    --mArmTicks;
    repaint = mArmTicks == 0;
  }
  if (mStatusTicks > 0) {
    --mStatusTicks;
    if (mStatusTicks == 0) {
      mStatus.clear();
      repaint = true;
    }
  }
  return repaint;
}

//------------------------------------------------------------------------
void PresetBar::draw(Canvas &c) const {
  c.setColour(layout::kBarFill);
  c.fillRect(Rect(0.0f, 0.0f, static_cast<float>(layout::kWidth), kBarH));
  c.setColour(layout::kBarHairline);
  c.fillRect(Rect(0.0f, kBarH - 1.0f, static_cast<float>(layout::kWidth), 1.0f));

  c.setFont(Font::Body);
  c.setFontSize(c.emForLineHeight(kLineHeight));

  // The name box.
  const Rect box = nameBox();
  c.setColour(layout::kComboFill);
  c.fillRoundRect(box, kRadius);
  c.setColour(mEditing ? layout::kComboText : layout::kComboOutline);
  c.setPenSize(1.0f);
  c.strokeRoundRect(box, kRadius);

  const float ascent = c.fontAscent();
  const float baseline = box.y + (box.h - (ascent + c.fontDescent())) * 0.5f + ascent;
  const float textX = box.x + kTextInset;
  const float maxW = box.w - 2.0f * kTextInset - 14.0f; // room for the arrow
  c.pushClip(box.inset(1.0f));
  if (mEditing) {
    // Scroll the text left so the caret is always visible.
    const std::string before = mEdit.substr(0, mCaret);
    const float caretW = c.stringWidth(before.c_str());
    const float shift = std::max(0.0f, caretW - maxW);
    c.setColour(layout::kComboText);
    c.drawString(mEdit.c_str(), textX - shift, baseline);
    const float cx = textX - shift + caretW + 0.5f;
    c.strokeLine(cx, box.y + 5.0f, cx, box.bottom() - 5.0f);
  } else if (!mStatus.empty()) {
    c.setColour(layout::kComboText);
    c.drawString(c.clipToWidth(mStatus, maxW).c_str(), textX, baseline);
  } else if (!mCurrent.empty()) {
    c.setColour(layout::kComboText);
    c.drawString(c.clipToWidth(mCurrent, maxW).c_str(), textX, baseline);
  } else {
    c.setColour(kDimText);
    c.drawString("Select preset", textX, baseline);
  }
  c.popClip();

  if (!mEditing) {
    // Dropdown arrow.
    const float ax = box.right() - kTextInset - 4.0f;
    const float ay = box.centreY();
    c.setColour(layout::kComboText);
    c.fillTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
  }

  drawButton(c, saveButton(), mEditing ? "OK" : "Save", mEditing);
  drawButton(c, deleteButton(), mArmTicks > 0 ? "Sure?" : "Del", mArmTicks > 0);
}

void PresetBar::drawOverlay(Canvas &c) const {
  if (!mListOpen)
    return;
  c.setFont(Font::Body);
  c.setFontSize(c.emForLineHeight(kLineHeight));
  const Rect list = listRect();
  c.setColour(layout::kComboFill);
  c.fillRect(list);
  c.setColour(layout::kComboOutline);
  c.setPenSize(1.0f);
  c.strokeRoundRect(list, 0.0f);

  const float ascent = c.fontAscent();
  const float pad = (kRowHeight - (ascent + c.fontDescent())) * 0.5f;
  c.pushClip(list);
  if (mNames.empty()) {
    c.setColour(kDimText);
    c.drawString("No presets", list.x + kTextInset, list.y + pad + ascent);
  }
  for (int i = 0; i < visibleRows(); ++i) {
    const int index = mScroll + i;
    if (index >= static_cast<int>(mNames.size()))
      break;
    const auto &name = mNames[static_cast<std::size_t>(index)];
    const Rect row(list.x, list.y + kRowHeight * static_cast<float>(i), list.w,
                   kRowHeight);
    if (index == mHover || name == mCurrent) {
      c.setColour(index == mHover ? layout::kComboOutline : layout::kBarFill);
      c.fillRect(row.inset(1.0f));
    }
    c.setColour(layout::kComboText);
    c.drawString(c.clipToWidth(name, row.w - 2.0f * kTextInset).c_str(),
                 row.x + kTextInset, row.y + pad + ascent);
  }
  c.popClip();
}

} // namespace bbm
