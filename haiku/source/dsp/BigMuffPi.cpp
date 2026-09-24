// BigBubbleMuff — top-level DSP engine implementation (JUCE-free build).
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
//
// Transliterated from the Linux engine with the framework removed. The circuit
// files it pulls in are unchanged, so the two builds solve the identical circuit.
#include "dsp/BigMuffPi.h"

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

namespace bbmh {

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
  bbm::OnePole inputHP;         // C1 1uF input coupling / DC block
  bbm::TransistorStage booster; // Q4 input booster gain stage
  bbm::TransistorStage clip1;   // Q3 gain stage
  bbm::DiodeClipper clip1Diode; // D1 antiparallel pair across Q3 feedback
  bbm::TransistorStage clip2;   // Q2 gain stage
  bbm::DiodeClipper clip2Diode; // D2 antiparallel pair across Q2 feedback
  bbm::ToneStack tone;
  bbm::TransistorStage recovery; // Q1 output recovery gain stage
  bbm::OnePole outputHP;         // C2 1uF output coupling / DC block

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

  // The Big Muff is a mono pedal: one input jack, one circuit, one output jack.
  // We solve a single mono circuit (the expensive Newton nodal solve) and fan the
  // result out to however many output channels the host wants. Processing per
  // channel would just solve the identical guitar signal twice. Hence one mono
  // oversampler, one Channel, and a mono scratch buffer for the downmixed input.
  Oversampler4x oversampler;
  // The Channel holds circuit objects with internal references -> non-movable; own
  // it via unique_ptr and construct in place.
  std::unique_ptr<Channel> channel;
  std::vector<float> monoBuf; // downmixed mono input/output, base rate
  bbm::NoiseGate gate;        // pre-gain input gate (base rate, pre-oversample)

  LinearSmoother drive1{sustainToDrive(0.75f)};
  LinearSmoother tone{0.5f};
  LinearSmoother volume{0.5f};
  LinearSmoother outputGain{1.0f};
  float gateAmount = 0.4f; // set from setControls (audio thread), applied per block

  void prepare(const ProcessSpec &spec) {
    sampleRate = spec.sampleRate;
    maxBlock = spec.maximumBlockSize;
    const double osRate = spec.sampleRate * (1 << kOversampleFactor);

    // One mono oversampler / circuit (see above).
    oversampler.prepare(maxBlock);

    monoBuf.assign(maxBlock, 0.0f);

    // The gate runs on the clean mono input at the base rate, before oversampling.
    gate.prepare(spec.sampleRate);

    channel = std::make_unique<Channel>();
    channel->prepare(osRate); // circuit runs at the oversampled rate

    const double smooth = 0.02; // 20 ms control smoothing
    // drive/tone/volume are consumed inside the oversampled loop; outputGain at
    // base rate. Reset each at the rate it is advanced so 20 ms is accurate.
    drive1.reset(osRate, smooth);
    tone.reset(osRate, smooth);
    volume.reset(osRate, smooth);
    outputGain.reset(spec.sampleRate, smooth);

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

  // One chunk of numSamp samples starting at `offset`, guaranteed to fit the
  // prepared block size.
  void processChunk(const float *const *in, float *const *out, std::size_t numCh,
                    std::size_t offset, std::size_t numSamp) noexcept {
    // Downmix to a single mono input (a Big Muff has one input jack). Averaging the
    // channels keeps a mono-duplicated stereo feed identical to a true mono feed,
    // and avoids the +6 dB a naive sum would add into the clippers. The pre-gain
    // noise gate then acts on this clean input, before the high-gain stages amplify
    // its noise floor into hiss (see NoiseGate.h).
    gate.setAmount(gateAmount);
    float *mono = monoBuf.data();
    const float invCh = 1.0f / static_cast<float>(numCh);
    for (std::size_t n = 0; n < numSamp; ++n) {
      float acc = 0.0f;
      for (std::size_t ch = 0; ch < numCh; ++ch)
        acc += in[ch][offset + n];
      mono[n] = gate.process(sanitise(acc * invCh));
    }

    // Solve the one mono circuit at the oversampled rate.
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

    // Output trim at base rate, then fan the mono result out to every output
    // channel (dual-mono on a stereo bus).
    for (std::size_t n = 0; n < numSamp; ++n) {
      const float g = outputGain.getNextValue();
      const float o = sanitise(mono[n] * g);
      for (std::size_t ch = 0; ch < numCh; ++ch)
        out[ch][offset + n] = o;
    }
  }
};

BigMuffPi::BigMuffPi() : impl_(std::make_unique<Impl>()) {}
BigMuffPi::~BigMuffPi() = default;

void BigMuffPi::prepare(const ProcessSpec &spec) {
  impl_->prepare(spec);
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

int BigMuffPi::getOversamplingLatencySamples() const {
  return static_cast<int>(impl_->oversampler.getLatencyInSamples());
}

void BigMuffPi::process(float *const *channels, std::size_t numChannels,
                        std::size_t numSamples) {
  process(const_cast<const float *const *>(channels), channels, numChannels, numSamples);
}

void BigMuffPi::process(const float *const *in, float *const *out,
                        std::size_t numChannels, std::size_t numSamples) {
  if (in == nullptr || out == nullptr || numChannels == 0 || numSamples == 0)
    return;
  if (impl_->channel == nullptr || impl_->maxBlock == 0)
    return;

  // Hosts must respect the block size prepare() was given, but a stray oversized
  // block must not run past the scratch buffers — walk it in prepared-size chunks
  // instead (still allocation-free).
  for (std::size_t done = 0; done < numSamples;) {
    const std::size_t n = std::min(numSamples - done, impl_->maxBlock);
    impl_->processChunk(in, out, numChannels, done, n);
    done += n;
  }
}

} // namespace bbmh
