#!/bin/sh
# make-hpkg.sh — build the `bigbubblemuff` .hpkg on a running Haiku (x86_64).
#
# Builds against the VST3-haiku SDK checkout (default ../../../VST3-haiku/vst3sdk
# relative to haiku/, matching haiku/CMakeLists.txt; override with VST3_SDK_DIR).
#
# VERSION and REVISION must stay in step with haiku/source/version.h and the
# project(VERSION ...) line in haiku/CMakeLists.txt; the check below enforces it
# rather than trusting three copies of the same number to stay in sync by hand.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)   # the haiku/ directory
VERSION=1.0.0
REVISION=1
STAGE="$HERE/stage"
SDK="${VST3_SDK_DIR:-$ROOT/../../VST3-haiku/vst3sdk}"

# Version consistency: source of truth is version.h, mirrored by CMake and here.
hdr_major=$(sed -n 's/^#define MAJOR_VERSION_STR "\(.*\)"/\1/p' "$ROOT/source/version.h")
hdr_sub=$(sed -n 's/^#define SUB_VERSION_STR "\(.*\)"/\1/p' "$ROOT/source/version.h")
hdr_rel=$(sed -n 's/^#define RELEASE_NUMBER_STR "\(.*\)"/\1/p' "$ROOT/source/version.h")
hdr_version="$hdr_major.$hdr_sub.$hdr_rel"
if [ "$hdr_version" != "$VERSION" ]; then
	echo "!! version mismatch: version.h says $hdr_version, this script says $VERSION" >&2
	exit 1
fi
if ! grep -q "VERSION $VERSION" "$ROOT/CMakeLists.txt"; then
	echo "!! version mismatch: haiku/CMakeLists.txt does not say VERSION $VERSION" >&2
	exit 1
fi
if ! grep -q "^version[[:space:]]*$VERSION-$REVISION\$" "$HERE/bigbubblemuff.PackageInfo"; then
	echo "!! version mismatch: PackageInfo is not $VERSION-$REVISION" >&2
	exit 1
fi

pkgman install -y cmake ninja gcc make || true
[ -d "$SDK" ] || { echo "!! VST3 SDK not found at $SDK (set VST3_SDK_DIR)" >&2; exit 1; }

cd "$ROOT"
rm -rf build "$STAGE"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DVST3_SDK_DIR="$SDK"
ninja -C build

BUNDLE=$(find build -type d -name 'BigBubbleMuff.vst3' | head -1)
[ -n "$BUNDLE" ] || { echo "!! BigBubbleMuff.vst3 not built" >&2; exit 1; }
mkdir -p "$STAGE/add-ons/media/VST3"
cp -r "$BUNDLE" "$STAGE/add-ons/media/VST3/"

# The MIT terms travel with the binary (the SDK's notice is reproduced in it).
mkdir -p "$STAGE/data/licenses/bigbubblemuff"
cp "$ROOT/LICENSE" "$STAGE/data/licenses/bigbubblemuff/LICENSE"

cp "$HERE/bigbubblemuff.PackageInfo" "$STAGE/.PackageInfo"
OUT="$HERE/bigbubblemuff-$VERSION-$REVISION-x86_64.hpkg"
package create -C "$STAGE" "$OUT"
echo ">> built $OUT"
