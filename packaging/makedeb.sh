#!/usr/bin/env bash
# BigBubbleMuff — build a Debian package (.deb) with no systemd dependency, so it
# installs cleanly on Devuan/sysvinit as well as Debian/Ubuntu and derivatives.
# Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#
# The package ships the VST3 plug-in (/usr/lib/vst3) and the LV2 plug-in
# (/usr/lib/lv2). It declares only library Depends: no maintainer scripts, no
# services, no systemd units. Built from the same clean, tested Release build as
# makedist.sh (packaging/release-build.sh).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAINTAINER="${MAINTAINER:-rations <rations@users.noreply.github.com>}"
# shellcheck source=packaging/gate.sh
. "$REPO/packaging/gate.sh"
# shellcheck source=packaging/release-build.sh
. "$REPO/packaging/release-build.sh"

bbm_release_build "$REPO"
BUILD="$BBM_RELEASE"

VERSION="$(sed -n 's/^project(BigBubbleMuff VERSION \([0-9][0-9.]*\).*/\1/p' "$REPO/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "could not read the version from CMakeLists.txt" >&2; exit 1; }
case "$(uname -m)" in
  x86_64) ARCH=amd64 ;;
  aarch64) ARCH=arm64 ;;
  armv7l) ARCH=armhf ;;
  *) ARCH="$(uname -m)" ;;
esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
DEB="$WORK/bigbubblemuff_${VERSION}_${ARCH}"
mkdir -p "$DEB/DEBIAN" "$DEB/usr/lib/vst3" "$DEB/usr/lib/lv2" \
         "$DEB/usr/share/doc/bigbubblemuff"

# Payload --------------------------------------------------------------------
cp -r "$BUILD/VST3/Release/BigBubbleMuff.vst3" "$DEB/usr/lib/vst3/"
cp -r "$BUILD/lv2/BigBubbleMuff.lv2" "$DEB/usr/lib/lv2/"
find "$DEB/usr/lib" -name '*.so' -exec strip --strip-unneeded {} \;
bbm_gate_bundles "$DEB/usr/lib/vst3/BigBubbleMuff.vst3" "$DEB/usr/lib/lv2/BigBubbleMuff.lv2"

# Debian wants the licence terms in one copyright file.
cat "$REPO/LICENSE" "$REPO/NOTICE" > "$DEB/usr/share/doc/bigbubblemuff/copyright"

# Control metadata -----------------------------------------------------------
INSTALLED_KB="$(du -sk "$DEB" | cut -f1)"
cat > "$DEB/DEBIAN/control" <<CONTROL
Package: bigbubblemuff
Version: ${VERSION}
Section: sound
Priority: optional
Architecture: ${ARCH}
Maintainer: ${MAINTAINER}
Installed-Size: ${INSTALLED_KB}
Depends: libc6, libgcc-s1, libstdc++6, libcairo2, libfreetype6, libx11-6
Description: Big Muff Pi fuzz/distortion — VST3 and LV2 plug-ins
 Linux emulation of the Russian "Bubble Font" Big Muff Pi. The whole schematic,
 input jack to Volume wiper, is simulated as one nodal circuit model (DK method)
 checked against ngspice. Fully offline, with no systemd dependency.
CONTROL

# Build ----------------------------------------------------------------------
mkdir -p "$REPO/dist"
OUT="$REPO/dist/bigbubblemuff_${VERSION}_${ARCH}.deb"
if ! dpkg-deb --root-owner-group --build "$DEB" "$OUT" 2>/dev/null; then
  fakeroot dpkg-deb --build "$DEB" "$OUT"
fi

echo "Built: $OUT"
dpkg-deb -I "$OUT"
echo "Contents:"
dpkg-deb -c "$OUT"
