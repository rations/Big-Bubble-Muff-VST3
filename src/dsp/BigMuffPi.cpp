// BigBubbleMuff — top-level DSP engine implementation.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Signal chain (per the schematic, see docs/netlist.md):
//   IN -> [C1 high-pass] -> Q4 booster gain
//      -> SUSTAIN drive -> clip stage 1 (nodal BJT + diode pair, C6 coupling)
//      -> interstage    -> clip stage 2 (nodal BJT + diode pair, C7 coupling)
//      -> TONE stack (R23/C8/R5) -> Q1 recovery gain
//      -> [C2 high-pass / DC block] -> VOLUME -> OUT
// The two clipping stages are full nodal circuit models (Ebers-Moll BJT with the
// antiparallel diode pair jointly inside the collector->base feedback loop, see
// TransistorStage.h). Each stage carries its own input coupling capacitor, so no
// separate coupling high-pass is needed in front of it. The nonlinear core runs
// inside a 4x oversampled region to limit aliasing; everything reaching the
// output is finiteness-guarded.
#include "dsp/BigMuffPi.h"

#include "dsp/AudioMath.h"
#include "dsp/DiodeClipper.h"
#include "dsp/Filters.h"
#include "dsp/Netlist.h"
#include "dsp/NoiseGate.h"
#include "dsp/Oversampler.h"
#include "dsp/ToneStack.h"
#include "dsp/TransistorStage.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace bbm {

namespace {
constexpr int kOversampleFactor = 2; // 4x total (2^2) around the nonlinear core.

// All four Big Muff stages are real common-emitter transistor stages (see
// TransistorStage), each with ~19x gain. The two clip stages add an antiparallel
// diode-pair limiter (DiodeClipper) on their output, clamping the swing to
// ~+/-0.5 V — the Big Muff fuzz. The high stage gain is what gives the pedal its
// sustain: even a tiny input is amplified into the clippers. The only scalars are
// the SUSTAIN pot divider feeding clip 1 and a final scale to plug-in full scale.
constexpr float kOutputScale = 0.18f; // circuit volts -> ~unity at the output

// SUSTAIN maps to the divider feeding clip 1 (R24 pot): dMin..dMax, geometric
// (knob feel). With ~19x booster gain ahead of it, even a small fraction pushes
// the clippers into clipping at high settings (max sustain/compression); low
// settings keep quiet playing below the diode knee for a cleaner edge.
constexpr float kDrive1Min = 0.005f;
constexpr float kDrive1Max = 0.6f;

inline float sustainToDrive(float sustain01) {
  const float s = std::clamp(sustain01, 0.0f, 1.0f);
  return kDrive1Min * std::pow(kDrive1Max / kDrive1Min, s);
}

// Per-audio-channel circuit state (no heap once constructed). The four NPN
// stages of the real pedal, in order: input booster, two diode clippers, output
// recovery — each a nodal TransistorStage.
struct Channel {
  OnePole inputHP;         // C1 1uF input coupling / DC block
  TransistorStage booster; // Q4 input booster gain stage
  TransistorStage clip1;   // Q3 gain stage
  DiodeClipper clip1Diode; // D1 antiparallel pair across Q3 feedback
  TransistorStage clip2;   // Q2 gain stage
  DiodeClipper clip2Diode; // D2 antiparallel pair across Q2 feedback
  ToneStack tone;
  TransistorStage recovery; // Q1 output recovery gain stage
  OnePole outputHP;         // C2 1uF output coupling / DC block

  void prepare(double fs) {
    inputHP.prepare(fs, 15.0f);
    outputHP.prepare(fs, 15.0f);
    booster.prepare(fs);
    clip1.prepare(fs);
    clip1Diode.prepare();
    clip2.prepare(fs);
    clip2Diode.prepare();
    recovery.prepare(fs);
    tone.prepare(fs);
  }

  void reset() {
    inputHP.reset();
    outputHP.reset();
    booster.reset();
    clip1.reset();
    clip1Diode.reset();
    clip2.reset();
    clip2Diode.reset();
    recovery.reset();
    tone.reset();
  }

  inline float process(float x, float drive1, float tone01) noexcept {
    x = inputHP.processHighpass(x);

    // Q4 booster brings the guitar level up into the clippers.
    x = booster.process(x);

    // SUSTAIN pot divides the booster output into the first high-gain clip stage;
    // each clip stage amplifies then the antiparallel diode pair clamps the swing
    // (~+/-0.5 V) — the soft, sustaining Big Muff fuzz.
    x = clip1Diode.process(clip1.process(x * drive1));
    x = clip2Diode.process(clip2.process(x));

    // Passive tone stack, then the Q1 recovery stage makes up its loss.
    tone.setTone(tone01);
    x = tone.process(x);
    x = recovery.process(x);

    return outputHP.processHighpass(x) * kOutputScale;
  }
};
} // namespace

struct BigMuffPi::Impl {
  double sampleRate = 44100.0;
  std::size_t maxBlock = 0;

  // The Big Muff is a mono pedal, so the engine is mono: one oversampler, one
  // Channel, and a scratch buffer for the gated input.
  Oversampler4x oversampler;
  // The Channel holds circuit objects with internal references -> non-movable; own
  // it via unique_ptr and construct in place.
  std::unique_ptr<Channel> channel;
  std::vector<float> monoBuf; // gated input / output, base rate
  NoiseGate gate;             // pre-gain input gate (base rate, pre-oversample)

  LinearSmoother drive1{sustainToDrive(0.75f)};
  LinearSmoother tone{0.5f};
  LinearSmoother volume{0.5f};
  LinearSmoother outputGain{1.0f};
  float gateAmount = 0.4f; // set from setControls (audio thread), applied per block

  void prepare(double fs, int maxBlockSamples) {
    sampleRate = fs;
    maxBlock = static_cast<std::size_t>(std::max(1, maxBlockSamples));
    const double osRate = fs * (1 << kOversampleFactor);

    // One mono oversampler / circuit (see above).
    oversampler.prepare(maxBlock);

    monoBuf.assign(maxBlock, 0.0f);

    // The gate runs on the clean mono input at the base rate, before oversampling.
    gate.prepare(sampleRate);

    channel = std::make_unique<Channel>();
    channel->prepare(osRate); // circuit runs at the oversampled rate

    const double smooth = 0.02; // 20 ms control smoothing
    // drive/tone/volume are consumed inside the oversampled loop; outputGain at
    // base rate. Reset each at the rate it is advanced so 20 ms is accurate.
    drive1.reset(osRate, smooth);
    tone.reset(osRate, smooth);
    volume.reset(osRate, smooth);
    outputGain.reset(sampleRate, smooth);

    reset();
    settle();
  }

  // Cheap, real-time-safe: zero filter/oversampler state. Safe to call from a
  // host's audio-thread reset().
  void reset() {
    oversampler.reset();
    if (channel != nullptr)
      channel->reset();
    gate.reset();
  }

  // Flush the turn-on transient. The stages' DC operating points relax through
  // the AC-coupled cascade (and are amplified by the recovery stage) over
  // ~100 ms; run silence through so the first audio block starts settled and the
  // plug-in emits no turn-on pop. Expensive — call only from prepare() on the
  // message thread, never from the audio thread.
  void settle() {
    const float d = drive1.getTargetValue();
    const float t = tone.getTargetValue();
    const int n =
        static_cast<int>(sampleRate * static_cast<double>(1 << kOversampleFactor) * 0.2);
    for (int i = 0; i < n; ++i)
      channel->process(0.0f, d, t);
  }

  // One chunk of numSamp samples, guaranteed to fit the prepared block size.
  void processChunk(const float *in, float *out, std::size_t numSamp) noexcept {
    // The pre-gain noise gate acts on the clean input, before the high-gain stages
    // amplify its noise floor into hiss (see NoiseGate.h).
    gate.setAmount(gateAmount);
    float *mono = monoBuf.data();
    for (std::size_t n = 0; n < numSamp; ++n)
      mono[n] = gate.process(sanitise(in[n]));

    // Solve the circuit at the oversampled rate.
    float *up = oversampler.processSamplesUp(mono, numSamp);
    if (up == nullptr)
      return;
    const std::size_t upSamp = numSamp * Oversampler4x::kFactor;
    for (std::size_t n = 0; n < upSamp; ++n) {
      const float d = drive1.getNextValue();
      const float t = tone.getNextValue();
      const float vol = volume.getNextValue();
      const float x = channel->process(sanitise(up[n]), d, t);
      up[n] = sanitise(x * vol);
    }
    oversampler.processSamplesDown(mono, numSamp);

    // Output trim at base rate.
    for (std::size_t n = 0; n < numSamp; ++n)
      out[n] = sanitise(mono[n] * outputGain.getNextValue());
  }
};

BigMuffPi::BigMuffPi() : impl_(std::make_unique<Impl>()) {}
BigMuffPi::~BigMuffPi() = default;

void BigMuffPi::prepare(double sampleRate, int maxBlock) {
  impl_->prepare(sampleRate, maxBlock);
}

void BigMuffPi::reset() {
  impl_->reset();
}

void BigMuffPi::setControls(const Controls &c) {
  impl_->drive1.setTargetValue(sustainToDrive(c.sustain));
  impl_->tone.setTargetValue(std::clamp(c.tone, 0.0f, 1.0f));
  impl_->volume.setTargetValue(std::clamp(c.volume, 0.0f, 1.0f));
  impl_->outputGain.setTargetValue(
      decibelsToGain(std::clamp(c.outputTrimDb, -24.0f, 24.0f)));
  impl_->gateAmount = std::clamp(c.gate, 0.0f, 1.0f);
}

double BigMuffPi::latencySamples() {
  return Oversampler4x::kLatencySamples;
}

void BigMuffPi::process(const float *in, float *out, std::size_t numSamples) noexcept {
  if (in == nullptr || out == nullptr || numSamples == 0)
    return;
  if (impl_->channel == nullptr || impl_->maxBlock == 0)
    return;

  // Hosts must respect the block size prepare() was given, but a stray oversized
  // block must not run past the scratch buffers — walk it in prepared-size chunks
  // instead (still allocation-free).
  for (std::size_t done = 0; done < numSamples;) {
    const std::size_t n = std::min(numSamples - done, impl_->maxBlock);
    impl_->processChunk(in + done, out + done, n);
    done += n;
  }
}

} // namespace bbm
