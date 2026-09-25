#!/usr/bin/env bash
# BigBubbleMuff -- regenerate the ngspice golden data the circuit tests compare against.
# SPDX-License-Identifier: MIT
#
# Offline, dev-host only. ngspice is an oracle, never a build dependency: the outputs are
# committed under tests/data/spice/ and the test suite only reads them.
#
#   tools/spice/gen_goldens.sh [path/to/ngspice]
#
# Default ngspice: /home/human/third_party/ngspice/bin/ngspice (ngspice-47, see
# /home/human/third_party/refs/pedals/SOURCES.md).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NGSPICE="${1:-/home/human/third_party/ngspice/bin/ngspice}"
DECK="$ROOT/docs/spice"
OUT="$ROOT/tests/data/spice"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"

# The circuit core runs at 4 x 48 kHz; transient goldens are sampled on that grid.
FS=192000
TSTEP=$(python3 -c "print(1.0/$FS)")
TMAX=$(python3 -c "print(1.0/($FS*16))")

run() { # name, body
  local name="$1" body="$2"
  cat >"$WORK/$name.cir" <<DECKEOF
* $name
.include $DECK/bigmuff.cir
$body
.end
DECKEOF
  "$NGSPICE" -b "$WORK/$name.cir" >"$WORK/$name.log" 2>&1 || {
    cat "$WORK/$name.log" >&2
    exit 1
  }
}

# --- DC operating point (both model sets) -----------------------------------------
for m in em gp; do
  run "op_$m" "
.include $DECK/models-$m.lib
VCC VCC 0 9
VIN IN 0 0
XBM IN OUT VCC bigmuff
.control
op
set wr_vecnames
set wr_singlescale
echo \"node,volts\" > $OUT/op_$m.csv
foreach n b4 c4 e4 s1 s2 s3 n5 b3 c3 e3 d3n n13 b2 c2 e2 d2n t1 t2 t3 b1 c1 e1 v3 out
  let v = v(xbm.\$n)
  echo \"\$n,\$&v\" >> $OUT/op_$m.csv
end
.endc"
done

# --- Small-signal AC, IN -> OUT, knob grid (EM models; volume at max) ------------------
for s in 0 0.25 0.5 0.75 1; do
  for t in 0 0.25 0.5 0.75 1; do
    name="ac_s${s}_t${t}"
    run "$name" "
.include $DECK/models-em.lib
VCC VCC 0 9
VIN IN 0 DC 0 AC 1
XBM IN OUT VCC bigmuff sus=$s tone=$t vol=1
.control
ac dec 20 10 20k
let mag = vdb(out)
set wr_singlescale
set wr_vecnames
option numdgt=9
wrdata $WORK/$name.dat mag
.endc"
    { echo "hz,db"; awk 'NR>1 {printf "%.6e,%.6f\n", $1, $2}' "$WORK/$name.dat"; } >"$OUT/$name.csv"
  done
done

# --- Transients on the 192 kHz grid ---------------------------------------------------
tran() { # name, models, sus, tone, vol, source-spec, seconds
  local name="$1" m="$2" s="$3" t="$4" v="$5" src="$6" dur="$7"
  run "$name" "
.include $DECK/models-$m.lib
.options interp reltol=1e-6 abstol=1e-15 vntol=1e-9 method=trap
VCC VCC 0 9
VIN IN 0 $src
XBM IN OUT VCC bigmuff sus=$s tone=$t vol=$v
.control
tran $TSTEP $dur 0 $TMAX
set wr_singlescale
set wr_vecnames
option numdgt=9
wrdata $WORK/$name.dat v(in) v(out)
.endc"
  # One column (v(out) in volts), sample k at t = k / FS. The header records the stimulus;
  # the input is regenerated exactly by the test from the same SIN() formula.
  {
    echo "# fs=$FS models=$m sus=$s tone=$t vol=$v src=$src"
    awk -v fs="$FS" 'NR>1 {
      k = $1 * fs
      if (k - int(k + 0.5) > 1e-6 || int(k + 0.5) - k > 1e-6) { print "off-grid t=" $1 > "/dev/stderr"; exit 1 }
      printf "%.8e\n", $3
    }' "$WORK/$name.dat"
  } >"$OUT/$name.txt"
}

for m in em gp; do
  for a in 0.01 0.1 1; do
    tran "tran_${m}_sin1k_a${a}_s0.75" "$m" 0.75 0.5 1 "SIN(0 $a 1000)" 0.03
  done
done
for s in 0 0.5 1; do
  tran "tran_em_sin100_a0.1_s${s}" em "$s" 0.5 1 "SIN(0 0.1 100)" 0.05
done
# A plucked-string-like decaying G3 (196 Hz, 0.5 V, 8/s damping) at the default knobs.
tran "tran_em_pluck196_s0.75" em 0.75 0.5 1 "SIN(0 0.5 196 0 8)" 0.1

echo "goldens written to $OUT"
