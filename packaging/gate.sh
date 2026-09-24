# BigBubbleMuff — checks on the PACKAGED (stripped) bundles, sourced by makedist.sh.
# Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#
# scripts/lv2-gate.sh and the SDK validator prove the build tree works; this proves
# that what a user receives is what was proven: the right files, the three (or one)
# entry points and nothing else exported, and only the libraries we mean to link.
# Everything is measured on the binaries, so it runs anywhere (no host, no display).
#
#   bbm_gate_bundles <BigBubbleMuff.vst3> <BigBubbleMuff.lv2>

# Libraries each module may link. The editor halves draw with cairo + FreeType
# into an X11 window; the LV2 DSP half draws nothing, so it gets the C/C++ runtime
# alone. (fontconfig, pixman and libpng arrive through cairo, not through us.)
BBM_RUNTIME_LIBS="libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6"
BBM_GUI_LIBS="libcairo.so.2 libfreetype.so.6 libX11.so.6 $BBM_RUNTIME_LIBS"

bbm_gate_fail() {
  echo "gate: FAIL  $1" >&2
  exit 1
}

# bbm_gate_module <file> <exact export list> <allowed NEEDED list>
bbm_gate_module() {
  local so="$1" want="$2" allowed="$3" name
  name="$(basename "$so")"
  [ -f "$so" ] || bbm_gate_fail "$name is missing"

  local got
  got="$(nm -D --defined-only "$so" | awk '$2 ~ /^[TDBVWRu]$/ { print $3 }' | sort | xargs)"
  [ "$got" = "$(echo "$want" | tr ' ' '\n' | sort | xargs)" ] ||
    bbm_gate_fail "$name exports [$got], not exactly [$want]"
  if readelf -sW --dyn-syms "$so" | grep -q 'UNIQUE'; then
    bbm_gate_fail "$name has STB_GNU_UNIQUE symbols (it could never be unloaded)"
  fi

  local lib
  for lib in $(readelf -d "$so" | awk '/NEEDED/ { gsub(/[][]/, "", $5); print $5 }'); do
    case " $allowed " in
      *" $lib "*) ;;
      *) bbm_gate_fail "$name links $lib, which is not on its list [$allowed]" ;;
    esac
  done
  if ldd "$so" | grep -q 'not found'; then
    ldd "$so" | grep 'not found' >&2
    bbm_gate_fail "$name has unresolved libraries on this machine"
  fi

  readelf -d "$so" | grep -q 'BIND_NOW' || bbm_gate_fail "$name is not linked -z now"
  readelf -lW "$so" | grep -q 'GNU_RELRO' || bbm_gate_fail "$name has no RELRO segment"
  readelf -lW "$so" | awk '$1 == "GNU_STACK" && $NF ~ /E/ { exit 1 }' ||
    bbm_gate_fail "$name has an executable stack"
  printf '  %-22s exports [%s]; links only allowed libraries\n' "$name" "$want"
}

bbm_gate_bundles() {
  local vst3="$1" lv2="$2" arch
  arch="$(uname -m)"
  echo "== packaged bundles =="

  # A VST3 bundle holds Contents/<arch>-linux and Contents/Resources, nothing else:
  # a build tree once configured for another platform keeps its old folder.
  local d
  for d in "$vst3"/Contents/*/; do
    case "$(basename "$d")" in
      Resources | "${arch}-linux") ;;
      *) bbm_gate_fail "BigBubbleMuff.vst3 carries Contents/$(basename "$d")/" ;;
    esac
  done
  bbm_gate_module "$vst3/Contents/${arch}-linux/BigBubbleMuff.so" \
    "GetPluginFactory ModuleEntry ModuleExit" "$BBM_GUI_LIBS"

  local files
  files="$(cd "$lv2" && find . -type f | sed 's|^\./||' | sort | xargs)"
  [ "$files" = "bigbubblemuff.so bigbubblemuff.ttl bigbubblemuff_ui.so manifest.ttl" ] ||
    bbm_gate_fail "BigBubbleMuff.lv2 holds [$files]"
  bbm_gate_module "$lv2/bigbubblemuff.so" "lv2_descriptor" "$BBM_RUNTIME_LIBS"
  bbm_gate_module "$lv2/bigbubblemuff_ui.so" "lv2ui_descriptor" "$BBM_GUI_LIBS"
  echo "  gate: PASSED"
}
