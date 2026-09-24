// BigBubbleMuff — user presets on disk.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// The ONE place the plug-in touches the filesystem (CLAUDE.md §4). Everything here
// runs on the UI / message thread, never from process().
//
// Location: ~/.config/BigBubbleMuff/Presets, one "<name>.bbmpreset" per preset.
// The directory is found from $HOME, the variable the JUCE build's
// userApplicationDataDirectory resolved "~" from; nothing else in the environment
// is read. Directories this code creates are 0700 and files 0600.
//
// Format (plain text, UTF-8, LF or CRLF):
//
//   #BBMPRESET 1
//   sustain=0.75
//   tone=0.5
//   volume=0.5
//   output=0
//   gate=0.12
//
// Values are PLAIN (the units the host shows: Output in dB), so a preset reads the
// way the knobs are labelled and survives a range change. A preset holds the five
// knobs only: the footswitch and the host bypass are performance state, and loading
// a sound should not switch the pedal in or out.
//
// Files are UNTRUSTED (anyone can edit or swap them): capped at 64 KiB, opened
// without following a symlink, parsed with locale-independent number parsing,
// version-checked, and every value clamped exactly as the state reader clamps.
// Unknown keys are ignored; a missing key takes its parameter's default; any
// non-finite or unparsable value fails the whole load, which then applies nothing.
#pragma once

#include "plugin/ids.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace bbm::presets {

inline constexpr int kFormatVersion = 1;
inline constexpr std::size_t kMaxFileBytes = std::size_t{64} * 1024;
inline constexpr std::size_t kMaxNameLength = 64;
inline constexpr std::size_t kMaxListed = 1024;
inline constexpr std::string_view kExtension = ".bbmpreset";

// The parameters a preset carries, with their file keys. Never rename a key.
struct Key {
  ParamId id;
  const char *key;
};
inline constexpr std::array<Key, 5> kKeys{{
    {kSustainId, "sustain"},
    {kToneId, "tone"},
    {kVolumeId, "volume"},
    {kOutputId, "output"},
    {kGateId, "gate"},
}};

// Normalised values, indexed by ParamId like kParams.
using Norms = std::array<double, kParamCount>;

// 1..64 characters of [A-Za-z0-9 ._-], not starting with '.' and not ending in a
// space. Everything that becomes a path goes through this first.
bool nameIsSafe(std::string_view name);

// The file text for `norm` (only the kKeys parameters are written).
std::string serialise(const Norms &norm);

// Parse file text. On success `out` holds the defaults with every kKeys value the
// file supplies applied (clamped); on failure `out` is untouched.
bool parse(std::string_view text, Norms &out);

class Store {
public:
  // An empty `dir` is a store with no directory: every operation fails cleanly.
  explicit Store(std::string dir) : mDir(std::move(dir)) {}

  // ~/.config/BigBubbleMuff/Presets, or empty if $HOME is unset or not absolute.
  static std::string defaultDir();

  const std::string &dir() const { return mDir; }

  // Names of the presets on disk, sorted (case-insensitive, then byte order).
  std::vector<std::string> list() const;
  bool exists(std::string_view name) const;
  // Creates the directory on demand. Replaces an existing preset of that name.
  bool save(std::string_view name, const Norms &norm) const;
  bool load(std::string_view name, Norms &out) const;
  bool remove(std::string_view name) const;

private:
  std::string pathFor(std::string_view name) const;
  bool ensureDir() const;

  std::string mDir;
};

} // namespace bbm::presets
