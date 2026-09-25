// BigBubbleMuff — DSP engine unit tests.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#include "test.h"

#include "dsp/BigMuffPi.h"
#include "dsp/NoiseGate.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <random>
#include <utility>
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

// Ratio of harmonic energy to fundamental energy for a pure sine into the engine.
// f0 is aligned to a DFT bin (no leakage). Higher = more distortion.
float measureHarmonicRatio(float sustain, float amp, double fs) {
  constexpr int N = 4096;
  constexpr int k = 20; // fundamental bin
  const double f0 = fs * k / N;

  bbm::BigMuffPi engine;
  engine.prepare(fs, N);
  bbm::Controls c;
  c.sustain = sustain;
  c.tone = 0.5f;
  c.volume = 1.0f;
  c.gate = 0.0f; // the plug-in's gate is not the pedal; keep it out of the measurement
  engine.setControls(c);

  std::vector<float> buf(N);
  double phase = 0.0;
  for (int i = 0; i < 6; ++i) { // warm up oversampler + smoothers
    fillSine(buf, fs, f0, amp, phase);
    engine.process(buf.data(), buf.data(), buf.size());
  }
  fillSine(buf, fs, f0, amp, phase);
  engine.process(buf.data(), buf.data(), buf.size());

  const double fund = dftMagnitude(buf, k);
  double harm = 0.0;
  for (int h = 2; h * k < N / 2; ++h) {
    const double m = dftMagnitude(buf, h * k);
    harm += m * m;
  }
  return static_cast<float>(std::sqrt(harm) / std::max(1.0e-9, fund));
}

// In-place radix-2 FFT; x.size() must be a power of two.
void fft(std::vector<std::complex<double>> &x) {
  const std::size_t n = x.size();
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; (j & bit) != 0; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j)
      std::swap(x[i], x[j]);
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const std::complex<double> w = std::polar(1.0, -kTwoPi / static_cast<double>(len));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<double> wn = 1.0;
      for (std::size_t j = 0; j < len / 2; ++j) {
        const std::complex<double> u = x[i + j];
        const std::complex<double> v = x[i + j + len / 2] * wn;
        x[i + j] = u + v;
        x[i + j + len / 2] = u - v;
        wn *= w;
      }
    }
  }
}

// The largest in-band (<= 20 kHz) non-harmonic component, relative to the
// fundamental, for a 0.1 V sine near `hz` at full Sustain and bright Tone. The
// fundamental sits on an odd bin of a power-of-two DFT, so every alias of every
// harmonic lands on a bin that is not a harmonic.
double worstAliasDb(double fs, double hz) {
  constexpr int N = 16384;
  const int k0 = static_cast<int>(hz * N / fs) | 1;
  const double f0 = k0 * fs / N;
  bbm::BigMuffPi engine;
  bbm::Controls c;
  c.sustain = 1.0f;
  c.tone = 1.0f;
  c.volume = 1.0f;
  c.gate = 0.0f;
  engine.setControls(c);
  engine.prepare(fs, N);
  std::vector<float> buf(N);
  double phase = 0.0;
  for (int i = 0; i < 4; ++i) {
    fillSine(buf, fs, f0, 0.1f, phase);
    engine.process(buf.data(), buf.data(), buf.size());
  }
  std::vector<std::complex<double>> x(buf.begin(), buf.end());
  fft(x);
  double worst = 0.0;
  for (int k = 1; k < N / 2 && k * fs / N <= 20000.0; ++k)
    if (k % k0 != 0)
      worst = std::max(worst, std::abs(x[static_cast<std::size_t>(k)]));
  return 20.0 * std::log10(worst / std::abs(x[static_cast<std::size_t>(k0)]));
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
  bbm::Controls c;
  c.sustain = 1.0f;
  c.volume = 1.0f;
  c.outputTrimDb = 24.0f;
  engine.setControls(c);
  engine.prepare(48000.0, 512);
  std::vector<float> buf(512);
  double phase = 0.0;
  float peak = 0.0f;
  for (int i = 0; i < 16; ++i) {
    fillSine(buf, 48000.0, 110.0, 1.0f, phase);
    engine.process(buf.data(), buf.data(), buf.size());
    peak = std::max(peak, magnitude(buf));
  }
  // OUT is C2-coupled from Q1's collector, which lives between 0 V and the 9 V rail,
  // so |OUT| < 9 V; times the +24 dB trim, with headroom for oversampler ripple.
  bbmtest::log("peak at full drive, +24 dB: " + std::to_string(peak));
  CHECK_MSG(peak < 1.2f * 9.0f * std::pow(10.0f, 24.0f / 20.0f),
            "output beyond the supply rail (runaway?)");
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
  // A gently picked 1 mV note: at hot inputs both clip stages saturate whatever the
  // pot says (the real pedal is never clean; checked against ngspice at 1 V), so
  // the knob's effect on the distortion is measured where it has one.
  const float lo = measureHarmonicRatio(0.05f, 0.001f, 48000.0);
  const float hi = measureHarmonicRatio(1.0f, 0.001f, 48000.0);
  bbmtest::log("harmonic ratio: low sustain=" + std::to_string(lo) +
               " high sustain=" + std::to_string(hi));
  CHECK_MSG(std::isfinite(lo) && std::isfinite(hi), "non-finite THD measurement");
  CHECK_MSG(hi > 10.0f * lo, "more Sustain should produce more harmonic distortion");
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

TEST_CASE("BigMuffPi engine", "in-band aliasing at full Sustain stays below -60 dB") {
  // Harmonics just above Nyquist fold back only as far as the half-band's
  // transition band (above 20 kHz), so the audible band is what is checked.
  for (const auto &[fs, hz] : {std::pair{44100.0, 1000.0}, std::pair{44100.0, 3000.0},
                               std::pair{44100.0, 6000.0}, std::pair{48000.0, 3000.0}}) {
    const double db = worstAliasDb(fs, hz);
    bbmtest::log("alias floor fs=" + std::to_string(int(fs)) + " f0~" +
                 std::to_string(int(hz)) + ": " + std::to_string(db) + " dB");
    CHECK(db < -60.0);
  }
}

TEST_CASE("BigMuffPi engine", "whole-engine cost at 48 kHz (reported)") {
  constexpr double fs = 48000.0;
  bbm::BigMuffPi engine;
  engine.prepare(fs, 256);
  std::vector<float> buf(256);
  double phase = 0.0;
  double busy = 0.0;
  for (int b = 0; b < static_cast<int>(fs) / 256; ++b) {
    fillSine(buf, fs, 196.0, 0.3f, phase);
    const auto t0 = std::chrono::steady_clock::now();
    engine.process(buf.data(), buf.data(), buf.size());
    busy += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  }
  CHECK(allFinite(buf));
  bbmtest::log("engine cost: " + std::to_string(100.0 * busy) +
               "% of one core (48 kHz, 4x, full circuit)");
}
