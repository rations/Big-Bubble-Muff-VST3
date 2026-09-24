# BigBubbleMuff — transcribed netlist

Source: the "V7/V8 Tall Font & Bubble Font Russian" Big Muff Pi schematic traced by
**Kit Rae** ([BigMuffPage](https://www.bigmuffpage.com)), kept locally as
`docs/schematic.jpg` (copyrighted, **not** redistributed). Version notes are from Kit Rae's
*Evolution of the Big Muff Pi Circuit — Part 3*. This file holds only factual component values
and connections, and it is the **single source of truth** for the circuit model
(`src/dsp/circuit/`) and the SPICE reference deck (`docs/bigmuff.cir`).

Every row below was read off the schematic at 2× zoom, with every wire traced to its nodes.
Nothing here is inferred from "typical Big Muff" values.

> **Numbering.** Kit Rae numbers Q1–Q4 and D1–D4 from **output to input** (the order printed
> on the vintage EHX PCBs). So **Q4 is the input booster** and **Q1 is the output stage**.

## Version-defining traits (V7C Green/Black Russian "Bubble Font", V8 Small Box Black Russian)

- **Feedback caps.** C10, C11 and C12 are single **470 pF** caps. The Tall Font version used
  two 1 nF caps in series (500 pF). This is the only schematic change from Tall Font.
- **Emitter resistors.** R22, R21 and R10 are **390 Ω**; some early black-box V7s used 430 Ω.
- **R8.** Normally **20 k**, rarely 39 k.
- **C9.** Normally 0.0039 µF. The schematic notes that two 6n8 caps in series were occasionally
  used here, annotated as "C9 = .0068µF". The model uses 0.0039 µF.
- **Transistors.** Russian **3102EM**, or 549C / 547C (NPN, silicon, TO-92).
- **Diodes.** **KD522B** in hardware. The schematic shows **1N914**; 1N914 / 1N4148 are close,
  not identical.
- **Pots.** Linear taper ("usually"; 150 k was sometimes used).

## Node names

`VCC` is the +9 V rail and `0` is ground.

| Node | Where |
|---|---|
| `IN` | input jack (engaged side of sw 1a) |
| `N1` | R2 / C1 junction |
| `B4 C4 E4` | Q4 (input booster) base / collector / emitter |
| `S3 S2 S1` | Sustain pot R24: pin 3 / wiper (pin 2) / pin 1 |
| `N5` | C5 / R19 junction |
| `B3 C3 E3` | Q3 (clip stage 1) base / collector / emitter |
| `D3N` | C6 / D3-D4 junction |
| `N13` | C13 / R12 junction |
| `B2 C2 E2` | Q2 (clip stage 2) base / collector / emitter |
| `D2N` | C7 / D1-D2 junction |
| `T3 T2 T1` | Tone pot R25: pin 3 / wiper / pin 1 |
| `B1 C1 E1` | Q1 (output stage) base / collector / emitter |
| `V3` | Volume pot R26 pin 3 (C2 output) |
| `OUT` | Volume wiper (output jack, engaged side of sw 1b) |

## Netlist

| Ref | Value | Nodes | Role |
|---|---|---|---|
| —   | 9 V | VCC–0 | Battery / DC supply |
| C14 | 22 µF 50 V | VCC–0 | Supply decoupling. It has no effect with an ideal supply, so it is not modelled. |
| R1  | 1.5 k | via sw 1c | LED series resistor (off-board). It only matters for the LED, so it is not modelled. |
| **Input** | | | |
| R2  | 39 k | IN–N1 | Input series resistor |
| C1  | **0.1 µF** | N1–B4 | Input coupling |
| **Q4 — input booster** | | | |
| Q4  | 3102EM / 549C / 547C | C4, B4, E4 | NPN common emitter with shunt feedback |
| R14 | 100 k | B4–0 | Base bias to ground |
| R9  | 470 k | C4–B4 | Collector→base feedback (bias and gain) |
| C10 | **470 pF** | C4–B4 | Feedback / Miller cap (HF roll-off). Bubble-Font trait. |
| R13 | 12 k | VCC–C4 | Collector load |
| R22 | 390 Ω | E4–0 | Emitter resistor (unbypassed) |
| C4  | 0.1 µF | C4–S3 | Output coupling into the Sustain pot |
| **SUSTAIN ("DIST")** | | | |
| R24 | 100 k lin | S3–S2–S1 | Sustain pot, used as a divider. Pin 3 = S3 (signal), pin 1 = S1. |
| R23 | **1 k** | S1–0 | Pot ground leg, which sets the minimum sustain level |
| C5  | 0.1 µF | S2–N5 | Coupling from the wiper |
| R19 | 10 k | N5–B3 | Series input resistor into Q3 (sets the stage gain with R17) |
| **Q3 — clipping stage 1** | | | |
| Q3  | 3102EM / 549C / 547C | C3, B3, E3 | NPN common emitter with shunt feedback |
| R20 | 100 k | B3–0 | Base bias to ground |
| R17 | 470 k | C3–B3 | Collector→base feedback |
| C12 | **470 pF** | C3–B3 | Feedback / Miller cap. Bubble-Font trait. |
| C6  | 0.047 µF | B3–D3N | **In series with the diode pair**, from base to collector |
| D3  | 1N914 (KD522B) | D3N→C3 | Clipping diode, anode D3N |
| D4  | 1N914 (KD522B) | C3→D3N | Clipping diode, anode C3 (antiparallel to D3) |
| R18 | 12 k | VCC–C3 | Collector load |
| R21 | 390 Ω | E3–0 | Emitter resistor (unbypassed) |
| C13 | 0.1 µF | C3–N13 | Inter-stage coupling |
| R12 | 10 k | N13–B2 | Series input resistor into Q2 |
| **Q2 — clipping stage 2** | | | |
| Q2  | 3102EM / 549C / 547C | C2, B2, E2 | NPN common emitter with shunt feedback |
| R16 | 100 k | B2–0 | Base bias to ground |
| R15 | 470 k | C2–B2 | Collector→base feedback |
| C11 | **470 pF** | C2–B2 | Feedback / Miller cap. Bubble-Font trait. |
| C7  | 0.047 µF | B2–D2N | **In series with the diode pair**, from base to collector |
| D2  | 1N914 (KD522B) | D2N→C2 | Clipping diode, anode D2N |
| D1  | 1N914 (KD522B) | C2→D2N | Clipping diode, anode C2 (antiparallel to D2) |
| R11 | 12 k | VCC–C2 | Collector load |
| R10 | 390 Ω | E2–0 | Emitter resistor (unbypassed) |
| **TONE (passive, driven directly from Q2's collector)** | | | |
| C9  | **0.0039 µF** | C2–T3 | High-pass (treble) branch series cap |
| R5  | 22 k | T3–0 | High-pass branch shunt to ground |
| R8  | 20 k | C2–T1 | Low-pass (bass) branch series resistor |
| C8  | **0.01 µF** | T1–0 | Low-pass branch shunt cap to ground |
| R25 | 100 k lin | T3–T2–T1 | Tone pot, blending the treble (pin 3) and bass (pin 1) branches |
| C3  | 0.1 µF | T2–B1 | Coupling from the wiper into Q1 |
| **Q1 — output stage** | | | |
| Q1  | 3102EM / 549C / 547C | C1, B1, E1 | NPN common emitter, **fixed divider bias** (no collector feedback) |
| R7  | **470 k** | VCC–B1 | Base bias from the rail |
| R3  | 100 k | B1–0 | Base bias to ground |
| R6  | 10 k | VCC–C1 | Collector load |
| R4  | **2.7 k** | E1–0 | Emitter resistor (unbypassed; sets the gain ≈ R6/R4) |
| C2  | **0.1 µF** | C1–V3 | Output coupling into the Volume pot |
| **VOLUME** | | | |
| R26 | 100 k lin | V3–OUT–0 | Volume pot, used as an output divider. Pin 3 = V3, wiper = OUT, pin 1 = ground. |
| **Switching** | | | |
| sw 1a/b/c | 3PDT | — | True bypass plus LED. The footswitch parameter models this. |

There is **no R25 "bias" resistor and no R11 in the tone stack**. The previous revision of this
file had those, and they were misreadings.

## Pot conventions (model)

The pots follow standard numbering. Pin 1 is the fully counter-clockwise (CCW) end and pin 3 the
fully clockwise (CW) end. The knob position k ∈ [0, 1] (0 = CCW, 1 = CW) splits a pot of value P as:

- pin 3 ↔ wiper: `(1 − k)·P`
- wiper ↔ pin 1: `k·P`

Resistances are clamped to at least 1 Ω so the matrices stay regular. So:

- **Sustain** up means more of Q4's output reaches Q3.
- **Tone** up means the wiper moves toward the treble (C9/R5) branch.
- **Volume** up means the wiper moves toward C2.

## Model boundary conditions (conventions, not circuit facts)

- **Input.** `IN` is driven by an ideal voltage source (a DI signal). The guitar pickup's own
  impedance is not modelled.
- **Output load.** 1 MΩ from `OUT` to ground, standing in for a typical amp input impedance.
- **Supply.** An ideal 9 V source, so C14 has no effect.
- **Level calibration.** `kVoltsPerFullScale = 1.0`: a digital sample of 1.0 is 1 V at `IN`, and
  1 V at `OUT` is a sample of 1.0. The same constant is used in both directions, so at full Volume
  the plug-in's level is the pedal's own.

## Device models

These are taken from manufacturer SPICE models (provenance in
`/home/human/third_party/refs/pedals/SOURCES.md`, "Big Muff Pi").

**NPN transistor — Philips BC547C** (Gummel-Poon, LTspice standard library). The schematic lists
547C as one of the three fitted types, and 549C is the low-noise selection of the same die. Full model:

```
.model BC547C NPN(IS=4.679E-14 NF=1.01 ISE=2.642E-15 NE=1.581 BF=458.7 IKF=0.1371
+ VAF=52.64 NR=1.019 ISC=2.337E-14 NC=1.164 BR=11.57 IKR=0.1144 VAR=364.5 RB=1
+ IRB=1.00E-06 RBM=1 RE=0.2598 RC=1 XTB=0 EG=1.11 XTI=3 CJE=1.229E-11 VJE=0.5591
+ MJE=0.3385 TF=4.689E-10 XTF=160 VTF=2.828 ITF=0.8842 PTF=0 CJC=4.42E-12 VJC=0.1994
+ MJC=0.2782 XCJC=0.6193 TR=1.00E-32 CJS=0 VJS=0.75 MJS=0.333 FC=0.7936)
```

The engine's **Ebers-Moll subset** is the transport-model core of that card, with every
second-order effect switched off: IS = 4.679e-14 A, NF = 1.01, NR = 1.019, BF = 458.7,
BR = 11.57. The effects left out are VAF/VAR (Early effect), IKF/IKR (high injection), ISE/ISC
(leakage), RB/RE/RC and the junction capacitances. `docs/bigmuff.cir` runs both cards, so the
error from the subset is measured rather than assumed. Measured beta spread is wide (BC549C hFE
420–800; 3102EM similar), and the real pedals "all sound slightly different from each other".

**Diodes — onsemi 1N914** (LTspice standard library): `Is=2.52n Rs=.568 N=1.752 Cjo=4p M=.4
tt=20n`. The engine uses Is = 2.52e-9 A and N = 1.752; Rs, Cjo and tt are omitted.
N·Vt = 45.3 mV at 300.15 K, which matches Yeh's measured effective Vt for this diode.

**Thermal voltage:** Vt = kT/q = **25.864917 mV** at T = 300.15 K (27 °C, SPICE's default TNOM and
TEMP), computed with ngspice-47's own constants (`const.h`: k = 1.38064852e-23, q =
1.6021766208e-19) so the engine and the reference deck use the identical value.

## Revision note (2026-09-24)

Corrections made against the schematic, relative to the first revision of this file:

- **C1** is 0.1 µF, not 1 µF.
- **R23** is 1 k (the Sustain pot's ground leg), not the tone pot.
- The tone pot is **R25**.
- **C8** is 0.01 µF, the low-pass shunt; **C9** is 0.0039 µF, the high-pass series cap.
- **R11** is Q2's collector load, not part of the tone stack.
- **R19 and R12** are 10 k series input resistors after **C5 and C13** (0.1 µF).
- **C6 and C7** are in series with the diode pairs from base to collector; they are not input
  coupling caps.
- **Q4** has its own 470 pF (C10), 12 k (R13) and 390 Ω (R22).
- **Q1** is divider-biased (R7 470 k from the rail, R3 100 k) with R6 10 k and R4 2.7 k.
- **C2** is 0.1 µF.
- The diode ideality was N = 1.752, not 1.
