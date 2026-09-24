#!/usr/bin/env bash
# BigBubbleMuff — build the release tarball: the VST3 and LV2 bundles, the
# install/uninstall scripts, and the licence files, flat in one directory.
# Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#
#   packaging/makedist.sh
#
# Always packages a fresh, clean Release build (build-release/, made and tested by
# packaging/release-build.sh), never a developer's build tree. The packaged
# (stripped) bundles are then gated by packaging/gate.sh before tarring.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=packaging/gate.sh
. "$REPO/packaging/gate.sh"
# shellcheck source=packaging/release-build.sh
. "$REPO/packaging/release-build.sh"

bbm_release_build "$REPO"
BUILD="$BBM_RELEASE"

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
