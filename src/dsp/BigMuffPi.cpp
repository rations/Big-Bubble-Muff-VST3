// BigBubbleMuff — top-level DSP engine implementation.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Signal chain:
//   in -> noise gate (base rate) -> 4x up -> the pedal circuit -> 4x down -> trim
// The circuit (circuit/BigMuffCircuit.h) is the whole schematic, input jack to
// Volume wiper, solved as one nodal DK system; SUSTAIN, TONE and VOLUME are its three
// pots. The gate and the output trim are the plug-in's own additions and sit outside
// it. Everything reaching the output is finiteness-guarded.
#include "dsp/BigMuffPi.h"

#include "dsp/AudioMath.h"
#include "dsp/NoiseGate.h"
#include "dsp/Oversampler.h"
#include "dsp/circuit/BigMuffCircuit.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace bbm {

namespace {
// The pots are smoothed per oversampled sample, and the circuit's knob-dependent
// matrices re-derived from the smoothed positions every this many samples (a rank-6
// update, ~6k flops; skipped when nothing moved).
constexpr int kKnobInterval = 32;
constexpr double kSmoothSeconds = 0.02;
} // namespace

struct BigMuffPi::Impl {
  double sampleRate = 44100.0;
  std::size_t maxBlock = 0;

  // The Big Muff is a mono pedal, so the engine is mono: one oversampler, one
  // circuit, and a scratch buffer for the gated input.
  Oversampler4x oversampler;
  std::unique_ptr<circuit::BigMuffCircuit> circuit; // ~20 KB of matrices
  std::vector<float> monoBuf;                       // gated input / output, base rate
  NoiseGate gate; // pre-gain input gate (base rate, pre-oversample)

  LinearSmoother sustain{0.75f};
  LinearSmoother tone{0.5f};
  LinearSmoother volume{0.5f};
  LinearSmoother outputGain{1.0f};
  float gateAmount = 0.4f; // set from setControls (audio thread), applied per block
  int knobCountdown = 0;

  circuit::Knobs targetKnobs() const {
    return {sustain.getTargetValue(), tone.getTargetValue(), volume.getTargetValue()};
  }

  void prepare(double fs, int maxBlockSamples) {
    sampleRate = fs;
    maxBlock = static_cast<std::size_t>(std::max(1, maxBlockSamples));
    const double osRate = fs * Oversampler4x::kFactor;

    oversampler.prepare(maxBlock);
    monoBuf.assign(maxBlock, 0.0f);
    gate.prepare(sampleRate);

    // Each smoother is reset at the rate it is advanced, which also snaps it to its
    // target, so the circuit starts at rest at the knobs it will run at.
    sustain.reset(osRate, kSmoothSeconds);
    tone.reset(osRate, kSmoothSeconds);
    volume.reset(osRate, kSmoothSeconds);
    outputGain.reset(sampleRate, kSmoothSeconds);

    // Builds the matrices and solves the DC operating point: the pedal starts as if
    // switched on long ago, so there is no turn-on transient to flush.
    circuit = std::make_unique<circuit::BigMuffCircuit>();
    circuit->setKnobs(targetKnobs());
    circuit->prepare(osRate);
    knobCountdown = 0;

    oversampler.reset();
    gate.reset();
  }

  // Real-time-safe: back to rest at the current knobs (a bounded DC solve, warm-
  // started from the last one).
  void reset() {
    oversampler.reset();
    gate.reset();
    sustain.setCurrentAndTargetValue(sustain.getTargetValue());
    tone.setCurrentAndTargetValue(tone.getTargetValue());
    volume.setCurrentAndTargetValue(volume.getTargetValue());
    if (circuit != nullptr) {
      circuit->setKnobs(targetKnobs());
      circuit->settleToDc();
    }
    knobCountdown = 0;
  }

  // One chunk of numSamp samples, guaranteed to fit the prepared block size.
  void processChunk(const float *in, float *out, std::size_t numSamp) noexcept {
    // The pre-gain noise gate acts on the clean input, before the high-gain stages
    // amplify its noise floor into hiss (see NoiseGate.h).
    gate.setAmount(gateAmount);
    float *mono = monoBuf.data();
    for (std::size_t n = 0; n < numSamp; ++n)
      mono[n] = gate.process(sanitise(in[n]));

    float *up = oversampler.processSamplesUp(mono, numSamp);
    if (up == nullptr)
      return;
    const std::size_t upSamp = numSamp * Oversampler4x::kFactor;
    for (std::size_t n = 0; n < upSamp; ++n) {
      const circuit::Knobs k{sustain.getNextValue(), tone.getNextValue(),
                             volume.getNextValue()};
      if (--knobCountdown <= 0) {
        knobCountdown = kKnobInterval;
        circuit->setKnobs(k);
      }
      const double vin = static_cast<double>(up[n]) * circuit::kVoltsPerFullScale;
      const double vout = circuit->process(vin) / circuit::kVoltsPerFullScale;
      up[n] = sanitise(static_cast<float>(vout));
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
  impl_->sustain.setTargetValue(std::clamp(c.sustain, 0.0f, 1.0f));
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
  if (impl_->circuit == nullptr || impl_->maxBlock == 0)
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
