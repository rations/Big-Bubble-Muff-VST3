#!/usr/bin/env bash
# BigBubbleMuff — generate chrome stompbox footswitch art (ImageMagick 7).
# Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#
# Produces two registered RGBA PNGs (transparent outside the button):
#   footswitch_up.png    raised button   -> pedal BYPASSED (off)
#   footswitch_down.png  pressed button   -> pedal ENGAGED  (on)
#
# Rendered at 2x (300x300) for crisp downscaling by the GUI. Both states share
# the collar geometry so they register pixel-perfect when swapped at runtime.
# Run once; the produced PNGs are the committed assets the build embeds.
set -euo pipefail
cd "$(dirname "$0")"

S=300            # canvas size
C=150            # centre (S/2)
COLLAR_R=140     # metal collar radius
BTN_R=108        # button (chrome dome) radius

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Reusable circular alpha mask of radius $1 -> $2 file.
mask() { # radius outfile
  magick -size ${S}x${S} xc:black -fill white \
    -draw "circle $C,$C $C,$((C - $1))" "$2"
}

mask "$COLLAR_R" "$tmp/collar_mask.png"

# ---- Collar (shared base): worn olive-green ring, only a touch darker than the
# faceplate (face green ~#7b8868) so it clearly reads as green at small sizes —
# not near-black. Keep the gradient shallow so the visible outer ring stays olive.
magick -size ${S}x${S} -define gradient:center=150,128 -define gradient:radii=185,185 \
  radial-gradient:'#6c7650'-'#414929' \
  "$tmp/collar_mask.png" -alpha off -compose CopyOpacity -composite \
  "$tmp/collar.png"

# Crisp dark-olive rim around the collar for separation from the faceplate.
magick "$tmp/collar.png" -fill none -stroke '#2c331c' -strokewidth 3 \
  -draw "circle $C,$C $C,$((C - COLLAR_R))" "$tmp/collar.png"

# ---- UP state: raised, brightly-lit chrome dome + drop shadow --------------
mask "$BTN_R" "$tmp/btn_mask.png"

# Lit dome: highlight biased to upper-left, falling to dark lower-right.
magick -size ${S}x${S} -define gradient:center=116,104 -define gradient:radii=210,210 \
  radial-gradient:'#fbfcfd'-'#4f5358' \
  "$tmp/btn_mask.png" -alpha off -compose CopyOpacity -composite \
  "$tmp/dome_up.png"

# Tight specular hot-spot near the top-left.
magick -size ${S}x${S} xc:black \
  -fill white -draw "ellipse 112,96 34,24 0,360" -blur 0x12 \
  "$tmp/btn_mask.png" -alpha off -compose CopyOpacity -composite \
  "$tmp/spec_up.png"

# Drop shadow cast below the raised button. Forced to RGBA (PNG32): this is the
# base layer of the up composite, and an all-neutral grayscale PNG here would
# drag the whole canvas to the Gray colorspace and desaturate the olive collar.
magick -size ${S}x${S} xc:none \
  -fill '#000000' -draw "circle $C,$((C + 12)) $C,$((C + 12 - BTN_R))" \
  -channel A -evaluate multiply 0.45 +channel -blur 0x10 \
  "PNG32:$tmp/shadow.png"

magick "$tmp/shadow.png" \
  "$tmp/collar.png"  -compose over -composite \
  "$tmp/dome_up.png" -compose over -composite \
  -fill none -stroke '#1f2415' -strokewidth 2 \
    -draw "circle $C,$C $C,$((C - BTN_R))" \
  \( "$tmp/spec_up.png" \) -compose screen -composite \
  -depth 8 footswitch_up.png

# ---- DOWN state: pressed, recessed dome + inner contact shadow -------------
BTN_RD=$((BTN_R - 3))
mask "$BTN_RD" "$tmp/btn_mask_d.png"

# Flatter, dimmer chrome dome (lower contrast, highlight nearer centre =>
# reads as pushed in, but still clearly silver — not a black void).
magick -size ${S}x${S} -define gradient:center=138,130 -define gradient:radii=235,235 \
  radial-gradient:'#d7dadd'-'#565a5e' \
  "$tmp/btn_mask_d.png" -alpha off -compose CopyOpacity -composite \
  "$tmp/dome_dn.png"

# Inner contact shadow: soft dark ring hugging the rim, CLIPPED to the button
# with DstIn (multiply alpha) so it only darkens the recess — never the centre.
magick -size ${S}x${S} xc:none \
  -fill none -stroke black -strokewidth 16 \
  -draw "circle $C,$C $C,$((C - BTN_RD))" -blur 0x8 \
  \( "$tmp/btn_mask_d.png" \) -compose DstIn -composite \
  -channel A -evaluate multiply 0.6 +channel \
  "$tmp/recess.png"

# Faint, tight specular (much weaker than the up state).
magick -size ${S}x${S} xc:black \
  -fill white -draw "ellipse 150,128 28,20 0,360" -blur 0x14 \
  "$tmp/btn_mask_d.png" -alpha off -compose CopyOpacity -composite \
  -channel A -evaluate multiply 0.5 +channel \
  "$tmp/spec_dn.png"

magick "$tmp/collar.png" \
  "$tmp/dome_dn.png" -compose over -composite \
  "$tmp/recess.png"  -compose over -composite \
  -fill none -stroke '#171b0f' -strokewidth 2 \
    -draw "circle $C,$C $C,$((C - BTN_RD))" \
  \( "$tmp/spec_dn.png" \) -compose screen -composite \
  -depth 8 footswitch_down.png

identify footswitch_up.png footswitch_down.png
echo "Wrote footswitch_up.png (off) and footswitch_down.png (on)."
