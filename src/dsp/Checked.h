// BigBubbleMuff — checked element access.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// The C++ Core Guidelines bounds profile forbids indexing a std::array with a
// non-constant index and prescribes gsl::at instead. This is that function without
// the GSL dependency. The index is in range by construction at every call site;
// if it ever were not, libstdc++'s _GLIBCXX_ASSERTIONS (always on, see
// cmake/Hardening.cmake) traps inside operator[] rather than reading out of bounds.
// It never throws, so it is safe on the audio thread.
#pragma once

#include <cstddef>

namespace bbm {

template <typename Container, typename Index>
constexpr decltype(auto) at(Container &c, Index i) noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index): this IS gsl::at
  return c[static_cast<std::size_t>(i)];
}

} // namespace bbm
