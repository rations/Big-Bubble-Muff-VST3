// BigBubbleMuff — Linux/Haiku engine parity harness.
// Copyright (C) 2026  BigBubbleMuff contributors. GPL-3.0-or-later (see COPYING).
//
// DEVELOPMENT HOST ONLY. This program links JUCE in order to run the shipping
// Linux engine (bbm::BigMuffPi) side by side with the JUCE-free engine that the
// Haiku plug-in uses (bbmh::BigMuffPi). Because it links JUCE it is GPL, it lives
// outside haiku/, and it is never distributed or referenced by any shipping
// artefact. Nothing under haiku/ includes it.
//
// The two engines solve the identical circuit from identical source files; the one
// deliberate difference is the anti-alias filtering of the 4x oversampled region,
// where the Haiku build uses half-band filters designed from scratch
// (tools/design_halfband.cpp) rather than JUCE's. This harness measures what that
// costs instead of assuming it costs nothing:
//
//   1. The circuit core itself — must be bit-identical, no tolerance.
//   2. Reported oversampling latency, both engines.
//   3. Full-chain difference across sample rates, knob settings and signal types.
//   4. Alias floor under a hard-clipping tone — the Haiku build must be no worse.
#include "../haiku/source/dsp/BigMuffPi.h"
#include "dsp/BigMuffPi.h"

// Test 1 compares the circuit stages themselves, so both copies of the circuit
// headers must be visible in this one translation unit. They declare the same
// names in the same namespace, so the Haiku copies are pulled in with `bbm`
// renamed -- the same trick tests/CMakeLists.txt uses to link both engines into
// one binary. The src/ copies go first, so that the "dsp/..." includes inside
// the Haiku copies resolve to files #pragma once has already seen instead of
// quietly pulling in a second, renamed set.
#include "dsp/DiodeClipper.h"
#include "dsp/Filters.h"
#include "dsp/Netlist.h"
#include "dsp/ToneStack.h"
#include "dsp/TransistorStage.h"

// clang-format off
// Order is load-bearing here: each header below needs the renamed namespace its
// predecessors declare, and alphabetising them would break the build.
#define bbm bbm_haiku_circuit
#include "../haiku/source/dsp/Netlist.h"
#include "../haiku/source/dsp/Filters.h"
#include "../haiku/source/dsp/TransistorStage.h"
#include "../haiku/source/dsp/DiodeClipper.h"
#include "../haiku/source/dsp/ToneStack.h"
#undef bbm
// clang-format on

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

namespace {

constexpr double kPi = std::numbers::pi;
constexpr int kBlock = 512;

// Deterministic uniform noise, so a failure is always reproducible.
class Lcg {
public:
  explicit Lcg(std::uint32_t seed) : state_(seed) {}
  float next() {
    state_ = state_ * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<double>(state_ >> 8) / 8388608.0 - 1.0);
  }

private:
  std::uint32_t state_;
};

// Runs the nonlinear core -- the part that IS the Big Muff -- through both copies
// of the circuit headers and returns the largest absolute disagreement. The two
// copies are verbatim apart from their licence header, so the only tolerable
// answer is exactly zero; anything else means they have drifted or are being
// compiled differently, which no amount of filter tuning downstream would excuse.
double circuitCoreMaxDiff(double sampleRate, int numSamples) {
  bbm::TransistorStage jStage;
  bbm::DiodeClipper jClip;
  bbm::ToneStack jTone;
  bbm_haiku_circuit::TransistorStage hStage;
  bbm_haiku_circuit::DiodeClipper hClip;
  bbm_haiku_circuit::ToneStack hTone;

  jStage.prepare(sampleRate);
  jClip.prepare();
  jTone.prepare(sampleRate);
  hStage.prepare(sampleRate);
  hClip.prepare();
  hTone.prepare(sampleRate);

  double worst = 0.0;
  Lcg rng(0x5EED1234u);
  for (float tone : {0.0f, 0.5f, 1.0f}) {
    jTone.setTone(tone);
    hTone.setTone(tone);
    for (int n = 0; n < numSamples; ++n) {
      // Drive hard enough to sit well inside the clipping region, where the
      // Newton solves in both stages are most sensitive.
      const float x = 0.8f * rng.next();
      const float j = jTone.process(jClip.process(jStage.process(x)));
      const float h = hTone.process(hClip.process(hStage.process(x)));
      worst = std::max(worst, std::abs(static_cast<double>(j) - static_cast<double>(h)));
    }
  }
  return worst;
}

enum class Signal { Sweep, Noise, Silence };

std::vector<float> makeSignal(Signal kind, double sampleRate, int numSamples) {
  std::vector<float> v(static_cast<std::size_t>(numSamples), 0.0f);
  switch (kind) {
  case Signal::Silence:
    break;
  case Signal::Noise: {
    Lcg rng(0x9E3779B9u);
    for (float &s : v)
      s = 0.25f * rng.next();
    break;
  }
  case Signal::Sweep: {
    // Exponential sweep 20 Hz -> 20 kHz over the whole buffer.
    const double f0 = 20.0, f1 = 20000.0;
    const double t1 = numSamples / sampleRate;
    const double k = std::log(f1 / f0);
    for (int n = 0; n < numSamples; ++n) {
      const double t = n / sampleRate;
      const double phase = 2.0 * kPi * f0 * t1 / k * (std::exp(k * t / t1) - 1.0);
      v[static_cast<std::size_t>(n)] = 0.25f * static_cast<float>(std::sin(phase));
    }
    break;
  }
  }
  return v;
}

const char *signalName(Signal s) {
  switch (s) {
  case Signal::Sweep:
    return "sweep";
  case Signal::Noise:
    return "noise";
  case Signal::Silence:
    return "silence";
  }
  return "?";
}

// --- running the two engines -------------------------------------------------

std::vector<float> runJuce(bbm::BigMuffPi &engine, const std::vector<float> &in) {
  std::vector<float> out(in.size(), 0.0f);
  juce::AudioBuffer<float> buf(1, kBlock);
  for (std::size_t pos = 0; pos < in.size(); pos += kBlock) {
    const int n = static_cast<int>(std::min<std::size_t>(kBlock, in.size() - pos));
    buf.clear();
    std::copy_n(in.data() + pos, n, buf.getWritePointer(0));
    juce::dsp::AudioBlock<float> block(buf);
    engine.process(block.getSubBlock(0, static_cast<std::size_t>(n)));
    std::copy_n(buf.getReadPointer(0), n, out.data() + pos);
  }
  return out;
}

std::vector<float> runHaiku(bbmh::BigMuffPi &engine, const std::vector<float> &in) {
  std::vector<float> out(in.size(), 0.0f);
  std::vector<float> scratch(static_cast<std::size_t>(kBlock), 0.0f);
  float *chans[1] = {scratch.data()};
  for (std::size_t pos = 0; pos < in.size(); pos += kBlock) {
    const std::size_t n = std::min<std::size_t>(kBlock, in.size() - pos);
    std::fill(scratch.begin(), scratch.end(), 0.0f);
    std::copy_n(in.data() + pos, n, scratch.data());
    engine.process(chans, 1, n);
    std::copy_n(scratch.data(), n, out.data() + pos);
  }
  return out;
}

// --- measurement -------------------------------------------------------------

double rms(const std::vector<float> &v, std::size_t from, std::size_t to) {
  double acc = 0.0;
  for (std::size_t i = from; i < to; ++i)
    acc += static_cast<double>(v[i]) * v[i];
  return std::sqrt(acc / static_cast<double>(std::max<std::size_t>(1, to - from)));
}

double toDb(double x) {
  return 20.0 * std::log10(std::max(x, 1e-300));
}

struct Difference {
  double relDb = 0.0;  // RMS difference relative to the reference RMS
  double peak = 0.0;   // max |difference|
  double refRms = 0.0; // reference RMS, so silence cases can be recognised
  int shift = 0;       // integer alignment that minimised the difference
};

// Compares two outputs, allowing a small integer alignment in case the engines'
// oversampler latencies differ. Reports the best alignment it used.
Difference compare(const std::vector<float> &ref, const std::vector<float> &test,
                   std::size_t skip) {
  const std::size_t guard = 8;
  double bestRms = 0.0, bestPeak = 0.0;
  int bestShift = 0;
  bool first = true;
  for (int shift = -4; shift <= 4; ++shift) {
    double acc = 0.0, peak = 0.0;
    std::size_t count = 0;
    for (std::size_t i = skip; i + guard < ref.size(); ++i) {
      const std::ptrdiff_t j = static_cast<std::ptrdiff_t>(i) + shift;
      if (j < 0 || static_cast<std::size_t>(j) >= test.size())
        continue;
      const double d = static_cast<double>(ref[i]) - test[static_cast<std::size_t>(j)];
      acc += d * d;
      peak = std::max(peak, std::abs(d));
      ++count;
    }
    const double diffRms =
        std::sqrt(acc / static_cast<double>(std::max<std::size_t>(1, count)));
    if (first || diffRms < bestRms) {
      bestRms = diffRms;
      bestPeak = peak;
      bestShift = shift;
      first = false;
    }
  }

  Difference best;
  best.refRms = rms(ref, skip, ref.size() - guard);
  best.relDb = toDb(bestRms / std::max(best.refRms, 1e-30));
  best.peak = bestPeak;
  best.shift = bestShift;
  return best;
}

// Naive DFT magnitude — the harness runs once on a development host, so clarity
// beats speed here.
std::vector<double> magnitudeSpectrum(const std::vector<float> &x, std::size_t offset,
                                      std::size_t n) {
  std::vector<double> mag(n / 2, 0.0);
  for (std::size_t k = 0; k < n / 2; ++k) {
    double re = 0.0, im = 0.0;
    const double w = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
      const double s = x[offset + i];
      re += s * std::cos(w * static_cast<double>(i));
      im += s * std::sin(w * static_cast<double>(i));
    }
    mag[k] = std::sqrt(re * re + im * im) / static_cast<double>(n);
  }
  return mag;
}

// Energy that is not a harmonic of the test tone, relative to the fundamental.
// With a bin-exact fundamental whose bin index is coprime with the transform
// length, every aliased harmonic folds onto its own bin, so what is left after
// removing the in-band harmonics is alias plus noise.
double aliasFloorDb(const std::vector<double> &mag, std::size_t fundamentalBin) {
  const std::size_t bins = mag.size();
  std::vector<bool> isHarmonic(bins, false);
  for (std::size_t k = 1; k * fundamentalBin < bins; ++k)
    isHarmonic[k * fundamentalBin] = true;

  double alias = 0.0;
  // Skip the lowest bins: the engine's 15 Hz DC blockers leave a little residue
  // there that is not aliasing.
  for (std::size_t k = 4; k < bins; ++k)
    if (!isHarmonic[k])
      alias += mag[k] * mag[k];

  return toDb(std::sqrt(alias) / std::max(mag[fundamentalBin], 1e-300));
}

} // namespace

int main() {
  bool pass = true;

  // ---- 1. the circuit core, bit for bit -------------------------------------
  std::printf("circuit core (transistor -> clipper -> tone stack), max |diff|:\n");
  for (double sampleRate : {44100.0, 48000.0, 96000.0}) {
    const double diff = circuitCoreMaxDiff(sampleRate, 20000);
    std::printf("  %6.0f Hz: %.17g\n", sampleRate, diff);
    if (diff > 0.0) { // max |diff| is non-negative, so this is "not exactly zero"
      std::printf("  FAIL: the circuit copies do not agree exactly\n");
      pass = false;
    }
  }

  // ---- 2. reported latency --------------------------------------------------
  {
    bbm::BigMuffPi juceEngine;
    bbmh::BigMuffPi haikuEngine;
    juce::dsp::ProcessSpec jspec{48000.0, static_cast<juce::uint32>(kBlock), 1};
    bbmh::ProcessSpec hspec{48000.0, static_cast<std::uint32_t>(kBlock), 1};
    juceEngine.prepare(jspec);
    haikuEngine.prepare(hspec);
    const int jl = juceEngine.getOversamplingLatencySamples();
    const int hl = haikuEngine.getOversamplingLatencySamples();
    std::printf("oversampling latency: JUCE %d samples, Haiku %d samples\n", jl, hl);
    if (jl != hl) {
      std::printf("  NOTE: reported latencies differ\n");
    }
  }

  // ---- 3. full-chain difference --------------------------------------------
  std::printf("\nfull-chain difference (RMS relative to the JUCE output):\n");
  double worstRelDb = -1000.0;
  std::string worstCase;

  for (double sampleRate : {44100.0, 48000.0, 96000.0}) {
    bbm::BigMuffPi juceEngine;
    bbmh::BigMuffPi haikuEngine;
    juce::dsp::ProcessSpec jspec{sampleRate, static_cast<juce::uint32>(kBlock), 1};
    bbmh::ProcessSpec hspec{sampleRate, static_cast<std::uint32_t>(kBlock), 1};
    juceEngine.prepare(jspec);
    haikuEngine.prepare(hspec);

    const int numSamples = static_cast<int>(sampleRate * 0.25);
    const std::size_t skip = static_cast<std::size_t>(sampleRate * 0.05);

    for (Signal sig : {Signal::Sweep, Signal::Noise, Signal::Silence}) {
      const std::vector<float> in = makeSignal(sig, sampleRate, numSamples);
      for (float sustain : {0.0f, 0.5f, 1.0f}) {
        for (float tone : {0.0f, 0.5f, 1.0f}) {
          for (float volume : {0.25f, 1.0f}) {
            bbm::Controls jc;
            jc.sustain = sustain;
            jc.tone = tone;
            jc.volume = volume;
            jc.outputTrimDb = 0.0f;
            jc.gate = 0.0f;
            bbmh::Controls hc;
            hc.sustain = sustain;
            hc.tone = tone;
            hc.volume = volume;
            hc.outputTrimDb = 0.0f;
            hc.gate = 0.0f;
            juceEngine.setControls(jc);
            haikuEngine.setControls(hc);

            const std::vector<float> jout = runJuce(juceEngine, in);
            const std::vector<float> hout = runHaiku(haikuEngine, in);
            const Difference d = compare(jout, hout, skip);

            // Silence in, silence out: there is no reference level to normalise
            // against, so check the absolute peak instead.
            if (sig == Signal::Silence) {
              if (d.peak > 1e-6) {
                std::printf("  FAIL silence: peak %.3e at sr=%.0f sus=%.2f\n", d.peak,
                            sampleRate, sustain);
                pass = false;
              }
              continue;
            }
            if (d.relDb > worstRelDb) {
              worstRelDb = d.relDb;
              char buf[160];
              std::snprintf(buf, sizeof buf,
                            "%s sr=%.0f sustain=%.2f tone=%.2f volume=%.2f shift=%d",
                            signalName(sig), sampleRate, sustain, tone, volume, d.shift);
              worstCase = buf;
            }
          }
        }
      }
    }
    std::printf("  %.0f Hz done\n", sampleRate);
  }
  std::printf("  worst case: %.2f dB  (%s)\n", worstRelDb, worstCase.c_str());

  // ---- 4. alias floor -------------------------------------------------------
  std::printf(
      "\nalias floor under a hard-clipping tone (relative to the fundamental):\n");
  {
    constexpr std::size_t kFftSize = 8192;
    constexpr std::size_t kFundamentalBin = 451; // coprime with 8192
    const double sampleRate = 48000.0;
    const double f0 =
        static_cast<double>(kFundamentalBin) * sampleRate / static_cast<double>(kFftSize);

    const int numSamples = static_cast<int>(sampleRate * 0.5);
    std::vector<float> in(static_cast<std::size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
      in[static_cast<std::size_t>(n)] =
          0.5f * static_cast<float>(std::sin(2.0 * kPi * f0 * n / sampleRate));

    bbm::BigMuffPi juceEngine;
    bbmh::BigMuffPi haikuEngine;
    juce::dsp::ProcessSpec jspec{sampleRate, static_cast<juce::uint32>(kBlock), 1};
    bbmh::ProcessSpec hspec{sampleRate, static_cast<std::uint32_t>(kBlock), 1};
    juceEngine.prepare(jspec);
    haikuEngine.prepare(hspec);

    bbm::Controls jc;
    jc.sustain = 1.0f;
    jc.tone = 0.5f;
    jc.volume = 1.0f;
    jc.gate = 0.0f;
    bbmh::Controls hc;
    hc.sustain = 1.0f;
    hc.tone = 0.5f;
    hc.volume = 1.0f;
    hc.gate = 0.0f;
    juceEngine.setControls(jc);
    haikuEngine.setControls(hc);

    const std::vector<float> jout = runJuce(juceEngine, in);
    const std::vector<float> hout = runHaiku(haikuEngine, in);

    // Analyse a window well past the smoothing ramps.
    const std::size_t offset = static_cast<std::size_t>(sampleRate * 0.25);
    const double jAlias =
        aliasFloorDb(magnitudeSpectrum(jout, offset, kFftSize), kFundamentalBin);
    const double hAlias =
        aliasFloorDb(magnitudeSpectrum(hout, offset, kFftSize), kFundamentalBin);
    std::printf("  tone %.1f Hz: JUCE %.2f dB, Haiku %.2f dB\n", f0, jAlias, hAlias);
    if (hAlias > jAlias + 1.0) {
      std::printf("  FAIL: Haiku alias floor is more than 1 dB worse\n");
      pass = false;
    }
  }

  std::printf("\n%s\n", pass ? "PARITY OK" : "PARITY FAILED");
  return pass ? 0 : 1;
}
