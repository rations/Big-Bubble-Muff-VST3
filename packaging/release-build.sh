# BigBubbleMuff — the clean Release build every package is made from, sourced by
# makedist.sh and makedeb.sh.
# Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#
# Packages are never made from a developer's build tree: that tree may be stale, a
# Debug or sanitizer build, or configured from an older revision. Instead this
# wipes build-release/ (it belongs to the packaging scripts alone), configures it
# from scratch, builds, and runs the test suite and the Steinberg validator. Only a
# build that passes both is packaged.
#
# The VST3 SDK is fetched at the commit pinned in CMakeLists.txt. To build offline
# from a local copy of that same commit, set BBM_VST3SDK=/path/to/vst3sdk;
# $HOME/third_party/vst3sdk is used when it exists and BBM_VST3SDK is unset.
#
#   bbm_release_build <repo>     sets BBM_RELEASE to the finished build directory

bbm_release_build() {
  local repo="$1"
  BBM_RELEASE="$repo/build-release"

  local sdk="${BBM_VST3SDK:-}"
  if [ -z "$sdk" ] && [ -f "$HOME/third_party/vst3sdk/CMakeLists.txt" ]; then
    sdk="$HOME/third_party/vst3sdk"
  fi
  local args=(-G Ninja -DCMAKE_BUILD_TYPE=Release)
  if [ -n "$sdk" ]; then
    echo "== VST3 SDK from $sdk (offline) =="
    args+=("-DFETCHCONTENT_SOURCE_DIR_VST3SDK=$sdk")
  else
    echo "== VST3 SDK fetched at the pinned commit =="
  fi

  echo "== clean Release build in $BBM_RELEASE =="
  rm -rf "$BBM_RELEASE"
  cmake -S "$repo" -B "$BBM_RELEASE" "${args[@]}"
  cmake --build "$BBM_RELEASE" --parallel "$(nproc)"

  echo "== tests =="
  ctest --test-dir "$BBM_RELEASE" --output-on-failure

  echo "== VST3 validator =="
  local report
  report="$("$BBM_RELEASE/bin/Release/validator" \
    "$BBM_RELEASE/VST3/Release/BigBubbleMuff.vst3" 2>&1)" || true
  if ! printf '%s\n' "$report" | grep -q 'tests passed, 0 tests failed'; then
    printf '%s\n' "$report" | tail -20 >&2
    echo "release build: the VST3 validator did not pass; nothing packaged" >&2
    exit 1
  fi
  printf '%s\n' "$report" | grep 'tests passed'
}
