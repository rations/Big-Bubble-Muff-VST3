#!/bin/sh
# BigBubbleMuff — uninstaller (mirror of install.sh).
# Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#
# Removes the VST3 and LV2 bundles. As root it cleans the system locations;
# otherwise the per-user ones. User presets (~/.config/BigBubbleMuff) are kept.
set -eu

APP_NAME="BigBubbleMuff"

if [ "$(id -u)" -eq 0 ]; then
  VST3_DIR="/usr/lib/vst3"
  LV2_DIR="/usr/lib/lv2"
else
  VST3_DIR="$HOME/.vst3"
  LV2_DIR="$HOME/.lv2"
fi

remove() {
  if [ -e "$1" ]; then
    rm -rf "$1"
    echo "Removed $1"
  fi
}

remove "$VST3_DIR/$APP_NAME.vst3"
remove "$LV2_DIR/$APP_NAME.lv2"

echo "Done."
