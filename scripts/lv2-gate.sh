#!/bin/bash
# lv2-gate.sh — the LV2 bundle's gate: its shape, what a host sees, and whether it runs.
# Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#
# After the owner's rations-amp scripts/lv2-gate.sh. Three parts, cheapest first:
#
#   1. shape          each module exports its one entry point and nothing else, no
#                     STB_GNU_UNIQUE symbols, the DSP half links neither X11 nor cairo,
#                     and RELRO / BIND_NOW / a non-executable stack are set.
#   2. bbm_lv2check   lilv's view of the bundle, plus instantiate / run / bypass /
#                     state / UI refusal (tools/bbm_lv2check.cpp), and with $DISPLAY
#                     set, the UI embedded in an unmapped window. ctest runs it
#                     headless.
#   3. sord_validate  the RDF schema check, when the tool is available. Build it from
#                     ~/third_party/source-lv2/sord (meson setup build -Dtools=enabled)
#                     or point $BBM_SORD_VALIDATE at it.
#
# It reads the BUILD tree and installs nothing.
#
#   scripts/lv2-gate.sh [build dir]        (default: build)

set -u
root=$(cd "$(dirname "$0")/.." && pwd)
build="${1:-$root/build}"
bundle="$build/lv2/BigBubbleMuff.lv2"
specdir=$(pkg-config --variable=lv2dir lv2 2>/dev/null || echo /usr/lib/lv2)
fail=0
note() { printf '\n== %s ==\n' "$1"; }
bad() { printf 'lv2-gate: FAIL  %s\n' "$1" >&2; fail=1; }

if [ ! -d "$bundle" ]; then
  echo "lv2-gate: no bundle at $bundle; build first (cmake --build $build)" >&2
  exit 2
fi

note "shape"
for f in manifest.ttl bigbubblemuff.ttl bigbubblemuff.so bigbubblemuff_ui.so; do
  [ -f "$bundle/$f" ] || bad "the bundle has no $f"
done
for pair in "bigbubblemuff.so lv2_descriptor" "bigbubblemuff_ui.so lv2ui_descriptor"; do
  set -- $pair
  so="$bundle/$1"
  got=$(nm -D --defined-only "$so" 2>/dev/null | awk '$2 ~ /^[TDBVWRu]$/ { print $3 }')
  if [ "$got" != "$2" ]; then
    bad "$1 exports [$(echo $got)] rather than exactly $2"
  else
    printf '  %-20s exports %s and nothing else\n' "$1" "$2"
  fi
  readelf -d "$so" | grep -q 'BIND_NOW' || bad "$1 is not linked -z now"
  readelf -lW "$so" | grep -q 'GNU_RELRO' || bad "$1 has no RELRO segment"
  readelf -lW "$so" | awk '$1 == "GNU_STACK" && $NF ~ /E/ { exit 1 }' ||
    bad "$1 has an executable stack"
done
if readelf -d "$bundle/bigbubblemuff.so" | grep -qE 'libX11|libcairo|libfreetype'; then
  bad "bigbubblemuff.so links X11, cairo or FreeType; the DSP half draws nothing"
else
  printf '  %-20s links no X11, cairo or FreeType\n' "bigbubblemuff.so"
fi

note "what a host sees, and whether it runs"
if [ -x "$build/bbm_lv2check" ]; then
  # With a display, also embed the UI in an UNMAPPED parent window (nothing is
  # shown). The suppression covers one cairo teardown leak; see the file.
  x11=()
  [ -n "${DISPLAY:-}" ] && x11=(--x11)
  LSAN_OPTIONS="suppressions=$root/tools/lsan-x11.supp${LSAN_OPTIONS:+:$LSAN_OPTIONS}" \
    "$build/bbm_lv2check" "$specdir" "$bundle" "${x11[@]}" || fail=1
else
  bad "bbm_lv2check was not built (it needs lilv-0's development files)"
fi

note "RDF schema"
sordv="${BBM_SORD_VALIDATE:-$(command -v sord_validate 2>/dev/null ||
  echo "$HOME/third_party/source-lv2/sord/build/sord_validate")}"
if [ ! -x "$sordv" ]; then
  echo "  skip  sord_validate not found (see the header of this script)"
else
  # Every vocabulary the bundle uses, plus the RDF/OWL/DOAP/FOAF schemas; only the
  # lines naming THIS plug-in count, since the spec bundles carry notes of their own.
  specs=""
  for d in core ui units options buf-size state urid atom port-props parameters \
    resize-port time midi schemas; do
    [ -d "$specdir/$d.lv2" ] && specs="$specs $(ls "$specdir/$d.lv2"/*.ttl)"
  done
  uri=$(sed -n 's/^<\(https:[^#>]*\)>$/\1/p' "$bundle/manifest.ttl" | head -1)
  out=$("$sordv" "$bundle"/*.ttl $specs 2>&1)
  mine=$(printf '%s\n' "$out" | grep -c "$uri")
  if [ "$mine" -ne 0 ]; then
    bad "sord_validate reports $mine line(s) against <$uri>:"
    printf '%s\n' "$out" | grep -B1 -A3 "$uri" >&2
  else
    echo "  clean  (nothing reported against <$uri>)"
  fi
fi

note "result"
if [ "$fail" -ne 0 ]; then
  echo "lv2-gate: FAILED" >&2
  exit 1
fi
echo "lv2-gate: PASSED"
echo
echo "Not covered here: embedding the UI in a real host (XEmbed behaviour differs"
echo "between hosts). Load the bundle in Ardour / Carla / jalv to close that."
