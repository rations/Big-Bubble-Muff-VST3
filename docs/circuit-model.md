# BigBubbleMuff — the circuit model and how it is checked

The engine simulates the whole pedal, from the input jack to the Volume wiper, as one circuit:
every element of [netlist.md](netlist.md), solved together at 4× the host rate. There is no
per-stage approximation, and no gain or voicing constant that is not a component value.

## Method

- **Nodal DK method.** Yeh, *Digital Implementation of Musical Distortion Circuits by Analysis
  and Simulation* (Stanford, 2009), §4.5; Holters & Zölzer, EUSIPCO 2015.
  - The circuit has 25 unknown node voltages, 13 capacitors and 2 sources (IN, +9 V).
  - It has 10 nonlinear ports: V_BE and V_BC of each BJT, and the two antiparallel diode pairs.
  - Capacitors use trapezoidal companions (G = 2C/T).
  - Per sample, the linear part is a matrix-vector product. The 10 port equations are solved by
    Newton, with SPICE's pn-junction limiter and the previous Jacobian reused while it still
    contracts.
- **Pots.** Each pot is two variable resistors. They enter through a rank-6 Woodbury update
  (Holters & Zölzer, DAFx-11), so the 25×25 factorisation happens once per sample rate. A knob
  move costs about 6k flops, and the smoothed pots are applied every 32 samples.
- **Start-up.** The DC operating point is solved in `prepare()` by nodal Newton with a supply
  ramp, and the capacitors start charged to it. There is no turn-on transient to flush.
- **Devices.** The Ebers-Moll core of the BC547C card and the Shockley core of the 1N914 card,
  exactly the device equations ngspice-47 evaluates for [spice/models-em.lib](spice/models-em.lib).
  netlist.md "Device models" measures what the omitted Gummel-Poon terms would change.
- **Code.** [src/dsp/circuit/](../src/dsp/circuit/), in double precision. It uses the standard
  library only, and nothing allocates after `prepare()`.

## Verification

The references are ngspice-47 runs of [spice/bigmuff.cir](spice/bigmuff.cir), made by
`tools/spice/gen_goldens.sh` and stored in `tests/data/spice/`. The checks live in
`tests/circuit_tests.cpp` and `tests/dsp_tests.cpp`. Figures are from 2026-09-24, measured on an
i7-12700F.

| Check | Result | Gate |
|---|---|---|
| DC operating point, 24 nodes | worst 0.004 mV | < 1 mV |
| Small-signal gain, 5×5 Sustain/Tone grid, 20 Hz–10 kHz | worst 0.29 dB | < 0.5 dB |
| Transient waveform error, 1 kHz sine at 0.01, 0.1 and 1 V (sus 0.75) | −66, −50, −44 dB | < −30 dB |
| Transient waveform error, 100 Hz sine at 0.1 V, sus 0 / 0.5 / 1 | −84, −101, −90 dB | < −30 dB |
| Transient waveform error, decaying 196 Hz pluck | −68 dB | < −30 dB |
| In-band (≤ 20 kHz) aliasing, full Sustain, bright Tone, 0.1 V | −71 dB at worst (44.1 kHz, 6 kHz tone) | < −60 dB |
| CPU, whole engine at 48 kHz (4× internally) | 8.4 % of one core | < 10 % (reported) |

A few notes on these figures:

- **Transient errors** are dominated by trapezoidal frequency warping at 192 kHz. The discrete
  model runs at a fixed step, while ngspice's reference steps are 16 times finer.
- **Aliases above 20 kHz** are harmonics just above Nyquist. The last half-band stage's
  transition band folds them back only as far as 20–22 kHz, so they stay above the audio band.
- **At a hot 1 V input with full Sustain,** Q1 saturates and the output becomes rounder, with a
  THD of 6 %. ngspice shows the same THD, and the same 2.74 V fundamental.

## Voicing check against a real pedal

We compared the engine against a NAM capture of a Green Russian (`Big Muff Green Russian.nam`,
by frank67 on TONE3000), rendered with NeuralAmpModelerCore's `render` tool. The engine was
rendered with `bbm_render` at the default knobs (0.75 / 0.5 / 0.5). The capture records neither
its knob settings nor its input level, so this is a sanity check, not a calibration.

Harmonics relative to the fundamental for a 187.5 Hz sine at −20 dBFS:

|        | h3 | h5 | h7 | h9 | h2 | h4 |
|---|---|---|---|---|---|---|
| NAM    | −15.9 | −23.2 | −27.7 | −30.0 | −42.6 | −47.1 |
| engine | −15.0 | −22.4 | −26.8 | −29.3 | −59.4 | −64.1 |

- **Odd harmonics** agree within 1 dB at every level from −40 to −10 dBFS.
- **Compression** is the same: over that 30 dB input sweep, the output rises 5.9 dB for the
  capture and 5.8 dB for the engine.
- **Even harmonics** are stronger in the capture. That fits a real unit's asymmetries, such as
  mismatched transistors and diodes, which the ideal symmetric netlist does not have.

The final judgement is by ear, in the DAW.
