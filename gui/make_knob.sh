#!/usr/bin/env bash
# BigBubbleMuff — square, centred knob asset for clean rotary rotation.
# Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#
# dialmufffinal.png is 220x147 (the knob sits off-centre on a wide canvas), which
# rotates badly about the image centre. This trims to the knob and pads it to a
# centred square so the editor can rotate it about the geometric centre. The pointer points straight up at value 0.5. Run once; commit dialknob.png.
set -euo pipefail
cd "$(dirname "$0")"

# Trim to the knob, then pad symmetrically to a square with a small margin so the
# pointer never clips the canvas as it sweeps. 113x113 content -> 128x128 square.
magick dialmufffinal.png -fuzz 6% -trim +repage \
  -background none -gravity center -extent 128x128 \
  -depth 8 dialknob.png

identify dialknob.png
echo "Wrote dialknob.png (square, centred knob)."
