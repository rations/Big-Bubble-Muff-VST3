// BigBubbleMuff — DSP engine unit tests.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#include "test.h"

#include "dsp/BigMuffPi.h"
#include "dsp/DiodeClipper.h"
#include "dsp/NoiseGate.h"
#include "dsp/ToneStack.h"
#include "dsp/TransistorStage.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

bool allFinite(const std::vector<float> &b) {
  return std::all_of(b.begin(), b.end(), [](float x) { return std::isfinite(x); });
}

float magnitude(const std::vector<float> &b) {
  float m = 0.0f;
  for (float x : b)
    m = std::max(m, std::abs(x));
  return m;
}

// Fills a buffer with a sine, advancing `phase` so successive blocks stay
// phase-continuous (no per-block discontinuity that would ring the oversampler).
void fillSine(std::vector<float> &b, double fs, double freq, float amp, double &phase) {
  const double inc = kTwoPi * freq / fs;
  for (float &s : b) {
    s = amp * static_cast<float>(std::sin(phase));
    phase += inc;
    if (phase > kTwoPi)
      phase -= kTwoPi;
  }
}

// Uniform in [-1, 1), deterministic across platforms (minstd_rand is fully
// specified by the standard).
struct Noise {
  std::minstd_rand rng;
  explicit Noise(unsigned seed) : rng(seed) {}
  float next() {
    const auto u = static_cast<double>(rng() - std::minstd_rand::min()) /
                   static_cast<double>(std::minstd_rand::max() - std::minstd_rand::min());
    return static_cast<float>(2.0 * u - 1.0);
  }
};

// |X[k]| of a real block, by direct summation (only a few bins are needed).
double dftMagnitude(const std::vector<float> &x, int k) {
  std::complex<double> acc{0.0, 0.0};
  const double w = -kTwoPi * k / static_cast<double>(x.size());
  for (std::size_t n = 0; n < x.size(); ++n)
    acc += static_cast<double>(x[n]) * std::polar(1.0, w * static_cast<double>(n));
  return std::abs(acc);
}

// Linear RMS gain of the tone stack at a single frequency.
float measureToneGain(float tone, double fs, double freq) {
  bbm::ToneStack ts;
  ts.prepare(fs);
  ts.setTone(tone);
  const double inc = kTwoPi * freq / fs;
  double phase = 0.0;
  double sumIn = 0.0, sumOut = 0.0;
  const int warmup = static_cast<int>(fs * 0.05);
  const int measure = static_cast<int>(fs * 0.2);
  for (int n = 0; n < warmup + measure; ++n) {
    const float x = static_cast<float>(std::sin(phase));
    const float y = ts.process(x);
    phase += inc;
    if (n >= warmup) {
      sumIn += static_cast<double>(x) * x;
      sumOut += static_cast<double>(y) * y;
    }
  }
  return static_cast<float>(std::sqrt(sumOut / std::max(1.0e-12, sumIn)));
}

// Ratio of harmonic energy to fundamental energy for a pure sine into the engine.
// f0 is aligned to a DFT bin (no leakage). Higher = more distortion.
float measureHarmonicRatio(float sustain, double fs) {
  constexpr int N = 4096;
  constexpr int k = 20; // fundamental bin
  const double f0 = fs * k / N;

  bbm::BigMuffPi engine;
  engine.prepare(fs, N);
  bbm::Controls c;
  c.sustain = sustain;
  c.tone = 0.5f;
  c.volume = 1.0f;
  engine.setControls(c);

  std::vector<float> buf(N);
  double phase = 0.0;
  for (int i = 0; i < 6; ++i) { // warm up oversampler + smoothers
    fillSine(buf, fs, f0, 0.3f, phase);
    engine.process(buf.data(), buf.data(), buf.size());
  }
  fillSine(buf, fs, f0, 0.3f, phase);
  engine.process(buf.data(), buf.data(), buf.size());

  const double fund = dftMagnitude(buf, k);
  double harm = 0.0;
  for (int h = 2; h * k < N / 2; ++h) {
    const double m = dftMagnitude(buf, h * k);
    harm += m * m;
  }
  return static_cast<float>(std::sqrt(harm) / std::max(1.0e-9, fund));
}

} // namespace

TEST_CASE("BigMuffPi engine", "prepare + process stays finite across sample rates") {
  for (double fs : {44100.0, 48000.0, 96000.0}) {
    bbm::BigMuffPi engine;
    engine.prepare(fs, 512);
    bbm::Controls c;
    c.sustain = 1.0f;
    c.tone = 0.5f;
    c.volume = 1.0f;
    engine.setControls(c);

    std::vector<float> buf(512);
    double phase = 0.0;
    for (int i = 0; i < 8; ++i) {
      fillSine(buf, fs, 220.0, 0.9f, phase);
      engine.process(buf.data(), buf.data(), buf.size());
      if (!allFinite(buf))
        break;
    }
    CHECK_MSG(allFinite(buf), "non-finite output at fs=" + std::to_string(fs));
  }
}

TEST_CASE("BigMuffPi engine", "rejects non-finite input without propagating NaN/Inf") {
  bbm::BigMuffPi engine;
  engine.prepare(48000.0, 256);
  std::vector<float> buf(256, 0.0f);
  buf[10] = std::numeric_limits<float>::infinity();
  buf[20] = std::numeric_limits<float>::quiet_NaN();
  engine.process(buf.data(), buf.data(), buf.size());
  CHECK_MSG(allFinite(buf), "NaN/Inf leaked through the engine");
}

TEST_CASE("BigMuffPi engine", "output stays bounded at full drive") {
  bbm::BigMuffPi engine;
  engine.prepare(48000.0, 512);
  bbm::Controls c;
  c.sustain = 1.0f;
  c.volume = 1.0f;
  c.outputTrimDb = 24.0f; // ~15.85x — bounded output must track this, not blow up.
  engine.setControls(c);
  std::vector<float> buf(512);
  double phase = 0.0;
  float peak = 0.0f;
  for (int i = 0; i < 16; ++i) {
    fillSine(buf, 48000.0, 110.0, 1.0f, phase);
    engine.process(buf.data(), buf.data(), buf.size());
    peak = std::max(peak, magnitude(buf));
  }
  // 24 dB gain on a unit sine ~= 16; allow oversampler ripple headroom.
  CHECK_MSG(peak < 32.0f, "output magnitude implausibly large (runaway?)");
}

TEST_CASE("BigMuffPi engine", "silence in -> silence out") {
  bbm::BigMuffPi engine;
  engine.prepare(44100.0, 128);
  std::vector<float> buf(128);
  // prepare() settles the cascade, so even the first block is silent (no turn-on
  // pop). Re-clear every block: reusing the previous output as input would form an
  // artificial feedback loop through the cascade's very high gain.
  float silMax = 0.0f;
  for (int i = 0; i < 8; ++i) {
    std::fill(buf.begin(), buf.end(), 0.0f);
    engine.process(buf.data(), buf.data(), buf.size());
    silMax = std::max(silMax, magnitude(buf));
  }
  bbmtest::log("silence residue max = " + std::to_string(silMax));
  CHECK_MSG(silMax < 1.0e-4f, "engine produced output from silence");
}

TEST_CASE("BigMuffPi engine", "distortion increases with the Sustain knob") {
  const float lo = measureHarmonicRatio(0.05f, 48000.0);
  const float hi = measureHarmonicRatio(1.0f, 48000.0);
  bbmtest::log("harmonic ratio: low sustain=" + std::to_string(lo) +
               " high sustain=" + std::to_string(hi));
  CHECK_MSG(std::isfinite(lo) && std::isfinite(hi), "non-finite THD measurement");
  CHECK_MSG(hi > lo, "more Sustain should produce more harmonic distortion");
  CHECK_MSG(hi > 0.1f, "max Sustain should be strongly distorted (fuzz)");
}

TEST_CASE("BigMuffPi engine", "process() does not allocate") {
  bbm::BigMuffPi engine;
  engine.prepare(48000.0, 256);
  std::vector<float> buf(256);
  double phase = 0.0;
  fillSine(buf, 48000.0, 220.0, 0.5f, phase);
  engine.process(buf.data(), buf.data(), buf.size()); // warm-up
  std::vector<float> big(1000, 0.1f);                 // longer than the prepared block
  bbmtest::AllocationGuard guard;
  for (int i = 0; i < 16; ++i) {
    bbm::Controls c;
    c.sustain = static_cast<float>(i) / 16.0f;
    engine.setControls(c);
    fillSine(buf, 48000.0, 220.0, 0.5f, phase);
    engine.process(buf.data(), buf.data(), buf.size());
  }
  // A block longer than prepared is walked in chunks, still without allocating.
  engine.process(big.data(), big.data(), big.size());
  CHECK_MSG(guard.allocations() == 0, "process/setControls allocated");
}

TEST_CASE("Big Muff tone stack", "tone fully CCW favours bass over treble") {
  const double fs = 48000.0;
  CHECK_MSG(measureToneGain(0.0f, fs, 100.0) > measureToneGain(0.0f, fs, 5000.0),
            "CCW tone should attenuate treble vs bass");
}

TEST_CASE("Big Muff tone stack", "tone fully CW favours treble over bass") {
  const double fs = 48000.0;
  CHECK_MSG(measureToneGain(1.0f, fs, 5000.0) > measureToneGain(1.0f, fs, 100.0),
            "CW tone should attenuate bass vs treble");
}

TEST_CASE("Big Muff tone stack", "centre tone has a mid-scoop notch") {
  const double fs = 48000.0;
  const float low = measureToneGain(0.5f, fs, 120.0);
  const float mid = measureToneGain(0.5f, fs, 900.0);
  const float high = measureToneGain(0.5f, fs, 4000.0);
  bbmtest::log("tone@0.5 gains  low=" + std::to_string(low) +
               " mid=" + std::to_string(mid) + " high=" + std::to_string(high));
  CHECK_MSG(mid < low && mid < high, "mids should be scooped vs low and high");
}

TEST_CASE("Phase B transistor stage", "DC operating point is physically sane") {
  bbm::TransistorStage stage;
  stage.prepare(192000.0);
  const auto op = stage.operatingPoint();
  bbmtest::log("DC bias  vb=" + std::to_string(op.vb) + " vc=" + std::to_string(op.vc) +
               " ve=" + std::to_string(op.ve));
  CHECK_MSG(op.vb > 0.0f && op.vb < bbm::netlist::kSupplyV, "base between rails");
  CHECK_MSG(op.vc > 0.0f && op.vc < bbm::netlist::kSupplyV, "collector between rails");
  CHECK_MSG(op.ve > 0.0f && op.ve < bbm::netlist::kSupplyV, "emitter between rails");
  CHECK_MSG((op.vb - op.ve) > 0.4f && (op.vb - op.ve) < 0.8f,
            "Vbe should be a forward junction drop");
  CHECK_MSG(op.vc > 2.0f && op.vc < 7.0f, "collector biases near mid-rail (high gain)");
  CHECK_MSG((op.vc - op.vb) > 1.0f, "BC junction reverse-biased (not saturated)");
}

TEST_CASE("Phase B transistor stage", "zero input -> settled (near-zero AC) output") {
  bbm::TransistorStage stage;
  stage.prepare(192000.0);
  float maxAbs = 0.0f;
  for (int n = 0; n < 4096; ++n)
    maxAbs = std::max(maxAbs, std::abs(stage.process(0.0f)));
  CHECK_MSG(maxAbs < 1.0e-3f, "no self-oscillation / drift at rest");
}

TEST_CASE("Phase B transistor stage",
          "large drive saturates against the rails, finite/bounded/asymmetric") {
  const double fs = 192000.0;
  bbm::TransistorStage stage;
  stage.prepare(fs);
  double sumPos = 0.0, sumNeg = 0.0;
  float peak = 0.0f;
  double phase = 0.0;
  const double inc = kTwoPi * 300.0 / fs;
  bool finite = true;
  for (int n = 0; n < 8192; ++n) {
    const float x = 0.8f * static_cast<float>(std::sin(phase));
    const float y = stage.process(x);
    phase += inc;
    finite = finite && std::isfinite(y);
    peak = std::max(peak, std::abs(y));
    if (n > 2048) {
      if (y > 0.0f)
        sumPos += y;
      else
        sumNeg += -y;
    }
  }
  CHECK_MSG(finite, "stage produced non-finite output");
  CHECK_MSG(peak < 20.0f, "collector swing bounded by the rails");
  const double asym = std::abs(sumPos - sumNeg) / std::max(1.0e-9, sumPos + sumNeg);
  bbmtest::log("rail-clip asymmetry = " + std::to_string(asym));
  CHECK_MSG(asym > 0.01, "rail clipping should be asymmetric (even harmonics)");
}

TEST_CASE("Antiparallel diode clipper",
          "clamps large drive to ~a diode drop, stays finite") {
  bbm::DiodeClipper clip;
  clip.prepare();
  float peak = 0.0f;
  bool finite = true;
  for (int n = 0; n < 4096; ++n) {
    const float x = (n % 2 == 0 ? 1.0f : -1.0f) * 12.0f;
    const float y = clip.process(x);
    finite = finite && std::isfinite(y);
    peak = std::max(peak, std::abs(y));
  }
  bbmtest::log("diode clamp peak = " + std::to_string(peak));
  CHECK_MSG(finite, "clipper produced non-finite output");
  CHECK_MSG(peak > 0.3f && peak < 0.8f, "clamp lands near a diode forward drop");
}

TEST_CASE("Antiparallel diode clipper", "near-transparent below the knee") {
  bbm::DiodeClipper clip;
  clip.prepare();
  double sumErr = 0.0;
  const double inc = kTwoPi * 1000.0 / 192000.0;
  double phase = 0.0;
  for (int n = 0; n < 2048; ++n) {
    const float x = 0.05f * static_cast<float>(std::sin(phase));
    const float y = clip.process(x);
    phase += inc;
    sumErr += std::abs(static_cast<double>(y - x));
  }
  CHECK_MSG(sumErr / 2048.0 < 0.01, "small signals pass nearly unclipped");
}

TEST_CASE("Pre-gain noise gate", "passes a loud signal nearly unchanged when open") {
  constexpr double fs = 48000.0;
  bbm::NoiseGate gate;
  gate.prepare(fs);
  gate.setAmount(0.4f); // open threshold ~ -50 dBFS
  double maxErr = 0.0;
  const double inc = kTwoPi * 220.0 / fs;
  double phase = 0.0;
  for (int n = 0; n < 8192; ++n) {
    const float x = 0.2f * static_cast<float>(std::sin(phase)); // ~ -14 dBFS
    const float y = gate.process(x);
    phase += inc;
    if (n > 4096) // after attack: gate fully open
      maxErr = std::max(maxErr, std::abs(static_cast<double>(y - x)));
  }
  CHECK_MSG(maxErr < 1.0e-3, "open gate is transparent to a loud signal");
}

TEST_CASE("Pre-gain noise gate", "closes to true silence on a quiet idle floor") {
  bbm::NoiseGate gate;
  gate.prepare(48000.0);
  gate.setAmount(0.4f);
  Noise noise(1234);
  // ~1.5 s of quiet noise (-60 dBFS-ish): well past hold + release.
  double tailRms = 0.0;
  int tailN = 0;
  for (int n = 0; n < 72000; ++n) {
    const float y = gate.process(1.0e-3f * noise.next());
    if (n > 67000) { // steady state: gate fully closed
      tailRms += static_cast<double>(y) * y;
      ++tailN;
    }
  }
  tailRms = std::sqrt(tailRms / std::max(1, tailN));
  bbmtest::log("gated idle tail RMS = " + std::to_string(tailRms));
  CHECK_MSG(tailRms < 1.0e-6, "gate mutes a quiet idle floor");
}

TEST_CASE("Pre-gain noise gate", "amount = 0 disables the gate (bit-transparent)") {
  bbm::NoiseGate gate;
  gate.prepare(48000.0);
  gate.setAmount(0.0f);
  Noise noise(99);
  float maxDiff = 0.0f;
  for (int n = 0; n < 4096; ++n) {
    const float x = 1.0e-3f * noise.next();
    maxDiff = std::max(maxDiff, std::abs(gate.process(x) - x));
  }
  // <= avoids -Wfloat-equal; true only when every sample passed bit-exact.
  CHECK_MSG(maxDiff <= 0.0f, "disabled gate passes the input unchanged");
}
