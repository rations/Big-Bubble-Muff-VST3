#!/usr/bin/env bash
# BigBubbleMuff — build the release tarball: the VST3 and LV2 bundles, the
# install/uninstall scripts, and the licence files, flat in one directory.
# Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#
#   packaging/makedist.sh
#
# BBM_BUILD_DIR picks the build tree (default: build); extra configure options go in
# BBM_CMAKE_ARGS, e.g. -DFETCHCONTENT_SOURCE_DIR_VST3SDK=/path/to/vst3sdk offline.
# The packaged (stripped) bundles are gated by packaging/gate.sh before tarring.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BBM_BUILD_DIR:-$REPO/build}"
# shellcheck source=packaging/gate.sh
. "$REPO/packaging/gate.sh"

# shellcheck disable=SC2086
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release ${BBM_CMAKE_ARGS:-} -S "$REPO"
cmake --build "$BUILD" --parallel "$(nproc)"

VERSION="$(sed -n 's/^project(BigBubbleMuff VERSION \([0-9][0-9.]*\).*/\1/p' "$REPO/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "could not read the version from CMakeLists.txt" >&2; exit 1; }
ARCH="$(uname -m)"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
PKG="$STAGE/BigBubbleMuff-${VERSION}"
mkdir -p "$PKG"

cp -r "$BUILD/VST3/Release/BigBubbleMuff.vst3" "$BUILD/lv2/BigBubbleMuff.lv2" "$PKG/"
find "$PKG" -name '*.so' -exec strip --strip-unneeded {} \;
bbm_gate_bundles "$PKG/BigBubbleMuff.vst3" "$PKG/BigBubbleMuff.lv2"

cp "$REPO/packaging/install.sh" "$REPO/packaging/uninstall.sh" "$PKG/"
chmod +x "$PKG/install.sh" "$PKG/uninstall.sh"
cp "$REPO/LICENSE" "$REPO/NOTICE" "$REPO/README.md" "$PKG/"
cp "$REPO/resources/fonts/OFL.txt" "$PKG/OFL-LiberationSans.txt"

mkdir -p "$REPO/dist"
TARBALL="$REPO/dist/BigBubbleMuff-${VERSION}-linux-${ARCH}.tar.gz"
tar -czf "$TARBALL" -C "$STAGE" "BigBubbleMuff-${VERSION}"

echo "Packaged: $TARBALL"
tar -tzf "$TARBALL"
