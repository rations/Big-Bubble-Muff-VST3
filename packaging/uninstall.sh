#!/bin/sh
# BigBubbleMuff — uninstaller (mirror of install.sh).
# Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#
# Removes the VST3 and LV2 bundles from the current user's ~/.vst3 and ~/.lv2.
# User presets (~/.config/BigBubbleMuff) are kept.
set -eu

APP_NAME="BigBubbleMuff"

if [ "$(id -u)" -eq 0 ]; then
  echo "error: run ./uninstall.sh as yourself, not as root or with sudo." >&2
  exit 1
fi
VST3_DIR="$HOME/.vst3"
LV2_DIR="$HOME/.lv2"

remove() {
  if [ -e "$1" ]; then
    rm -rf "$1"
    echo "Removed $1"
  fi
}

remove "$VST3_DIR/$APP_NAME.vst3"
remove "$LV2_DIR/$APP_NAME.lv2"

echo "Done."
