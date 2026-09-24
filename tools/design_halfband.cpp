// BigBubbleMuff — half-band polyphase-IIR designer (development host tool).
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Designs the two half-band filters used by the Haiku build's 4x oversampler and
// prints them as a ready-to-paste constexpr table. Standard library only: no JUCE,
// no third-party code, nothing GPL. Run on the development host, never shipped.
//
// Structure (the classic two-path polyphase half-band, Valenzuela-Constantinides
// form found in any multirate text):
//
//     H(z) = 1/2 * [ A0(z^2) + z^-1 * A1(z^2) ],  A_p(w) = prod_i (a_i + w^-1)
//                                                            / (1 + a_i * w^-1)
//
// Each section is a first-order allpass in w = z^2, so its poles sit at
// z = +/-j*sqrt(a_i) on the imaginary axis and 0 < a_i < 1 keeps it stable. The
// half-band property |H(w)|^2 + |H(pi-w)|^2 = 1 means designing the stopband also
// fixes the passband, so the design problem is simply
//
//     minimise  max |H(e^jw)|  over  w in [ws, pi]
//
// over the M coefficients, with ws = 2*pi*(0.25 + tw/2). That minimax problem is
// solved here directly by Nelder-Mead with a log-sum-exp softmax continuation
// (p = 2, 4, ... 512, which tends to the true max), then the result is VERIFIED by
// evaluating the response: the tool reports achieved stopband attenuation and
// passband ripple, and only a design that meets its spec is printed.
//
// The minimax-optimal half-band of a given order and transition width is unique,
// so this converges on the same numbers any correct design procedure produces --
// which is exactly why filter coefficients are facts about a filter rather than
// anybody's expression.
//
// Build & run:
//   c++ -std=c++20 -O2 -o /tmp/design_halfband tools/design_halfband.cpp && \
//     /tmp/design_halfband

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace {

using Complex = std::complex<double>;
constexpr double kPi = std::numbers::pi;

// --- the filter under design -------------------------------------------------

struct HalfBand {
  std::vector<double> direct;  // A0 sections
  std::vector<double> delayed; // A1 sections (A1 also carries the z^-1)
};

// Sorted coefficients alternate between the two paths, smallest first into the
// direct path -- the assignment that makes the pair a lowpass/highpass complement.
HalfBand split(std::vector<double> a) {
  std::sort(a.begin(), a.end());
  HalfBand hb;
  for (std::size_t i = 0; i < a.size(); ++i)
    (i % 2 == 0 ? hb.direct : hb.delayed).push_back(a[i]);
  return hb;
}

Complex allpassChain(const std::vector<double> &coeffs, Complex wInv) {
  Complex acc{1.0, 0.0};
  for (double a : coeffs)
    acc *= (a + wInv) / (1.0 + a * wInv);
  return acc;
}

Complex response(const HalfBand &hb, double omega) {
  const Complex wInv = std::exp(Complex(0.0, -2.0 * omega)); // z^-2
  const Complex zInv = std::exp(Complex(0.0, -omega));
  return 0.5 * (allpassChain(hb.direct, wInv) + zInv * allpassChain(hb.delayed, wInv));
}

// JUCE reports oversampler latency as -phase(w0)/w0 at a near-DC probe (normalised
// frequency 1e-4). Use the identical probe so the two builds' numbers compare.
double dcDelaySamples(const HalfBand &hb) {
  constexpr double kProbe = 0.0001; // cycles/sample
  const double omega = 2.0 * kPi * kProbe;
  return -std::arg(response(hb, omega)) / omega;
}

// --- measurement -------------------------------------------------------------

struct Quality {
  double stopbandDb;   // worst (least negative) stopband magnitude, dB
  double passbandDb;   // worst deviation from unity in the passband, dB
  double delaySamples; // group delay at DC, in samples at the filter's own rate
};

Quality measure(const HalfBand &hb, double tw) {
  const double wp = 2.0 * kPi * (0.25 - 0.5 * tw);
  const double ws = 2.0 * kPi * (0.25 + 0.5 * tw);
  constexpr int kGrid = 40000;

  double worstStop = 0.0;
  for (int i = 0; i <= kGrid; ++i) {
    const double w = ws + (kPi - ws) * static_cast<double>(i) / kGrid;
    worstStop = std::max(worstStop, std::abs(response(hb, w)));
  }
  double worstPass = 0.0;
  for (int i = 0; i <= kGrid; ++i) {
    const double w = wp * static_cast<double>(i) / kGrid;
    worstPass = std::max(worstPass, std::abs(std::abs(response(hb, w)) - 1.0));
  }
  return {20.0 * std::log10(std::max(worstStop, 1e-300)),
          20.0 * std::log10(std::max(worstPass, 1e-300)), dcDelaySamples(hb)};
}

// --- optimiser ---------------------------------------------------------------

// Unconstrained parameterisation: a = sigmoid(t) keeps every coefficient in (0,1).
double sigmoid(double t) {
  return 1.0 / (1.0 + std::exp(-t));
}

struct Objective {
  std::vector<double> omegas; // stopband grid
  double p = 2.0;

  double operator()(const std::vector<double> &t) const {
    std::vector<double> a(t.size());
    for (std::size_t i = 0; i < t.size(); ++i)
      a[i] = sigmoid(t[i]);
    const HalfBand hb = split(std::move(a));

    // log-sum-exp softmax of log|H| -- smooth, and tends to max|H| as p grows.
    std::vector<double> logs(omegas.size());
    double m = -1e300;
    for (std::size_t i = 0; i < omegas.size(); ++i) {
      logs[i] = std::log(std::max(std::abs(response(hb, omegas[i])), 1e-300));
      m = std::max(m, logs[i]);
    }
    double acc = 0.0;
    for (double l : logs)
      acc += std::exp(p * (l - m));
    return m + std::log(acc) / p;
  }
};

template <typename Fn>
std::vector<double> nelderMead(const Fn &f, std::vector<double> x0, int maxIter,
                               double initialStep = 0.35) {
  const std::size_t n = x0.size();
  std::vector<std::vector<double>> simplex(n + 1, x0);
  for (std::size_t i = 0; i < n; ++i)
    simplex[i + 1][i] += initialStep;

  std::vector<double> fv(n + 1);
  for (std::size_t i = 0; i <= n; ++i)
    fv[i] = f(simplex[i]);

  auto centroidExcluding = [&](std::size_t worst) {
    std::vector<double> c(n, 0.0);
    for (std::size_t i = 0; i <= n; ++i) {
      if (i == worst)
        continue;
      for (std::size_t j = 0; j < n; ++j)
        c[j] += simplex[i][j];
    }
    for (double &v : c)
      v /= static_cast<double>(n);
    return c;
  };
  auto combine = [&](const std::vector<double> &c, const std::vector<double> &x,
                     double t) {
    std::vector<double> r(n);
    for (std::size_t j = 0; j < n; ++j)
      r[j] = c[j] + t * (x[j] - c[j]);
    return r;
  };

  for (int iter = 0; iter < maxIter; ++iter) {
    std::size_t best = 0, worst = 0, second = 0;
    for (std::size_t i = 0; i <= n; ++i) {
      if (fv[i] < fv[best])
        best = i;
      if (fv[i] > fv[worst])
        worst = i;
    }
    for (std::size_t i = 0; i <= n; ++i)
      if (i != worst && (second == worst || fv[i] > fv[second]))
        second = i;

    // Terminate on simplex SIZE, not on the spread of function values: near a
    // minimum the objective is flat, so a value-based test stops far too early.
    double extent = 0.0;
    for (std::size_t i = 0; i <= n; ++i)
      for (std::size_t j = 0; j < n; ++j)
        extent = std::max(extent, std::abs(simplex[i][j] - simplex[best][j]));
    if (extent < 1e-14)
      break;

    const std::vector<double> c = centroidExcluding(worst);
    const std::vector<double> xr = combine(c, simplex[worst], -1.0); // reflect
    const double fr = f(xr);

    if (fr < fv[best]) {
      const std::vector<double> xe = combine(c, simplex[worst], -2.0); // expand
      const double fe = f(xe);
      simplex[worst] = (fe < fr) ? xe : xr;
      fv[worst] = std::min(fe, fr);
    } else if (fr < fv[second]) {
      simplex[worst] = xr;
      fv[worst] = fr;
    } else {
      const std::vector<double> xc = combine(c, simplex[worst], 0.5); // contract
      const double fc = f(xc);
      if (fc < fv[worst]) {
        simplex[worst] = xc;
        fv[worst] = fc;
      } else { // shrink
        for (std::size_t i = 0; i <= n; ++i) {
          if (i == best)
            continue;
          for (std::size_t j = 0; j < n; ++j)
            simplex[i][j] = simplex[best][j] + 0.5 * (simplex[i][j] - simplex[best][j]);
          fv[i] = f(simplex[i]);
        }
      }
    }
  }

  std::size_t best = 0;
  for (std::size_t i = 0; i <= n; ++i)
    if (fv[i] < fv[best])
      best = i;
  return simplex[best];
}

std::vector<double> makeGrid(double tw, int points) {
  const double ws = 2.0 * kPi * (0.25 + 0.5 * tw);
  std::vector<double> g;
  g.reserve(static_cast<std::size_t>(points) + 1);
  for (int i = 0; i <= points; ++i)
    g.push_back(ws + (kPi - ws) * static_cast<double>(i) / points);
  return g;
}

HalfBand toHalfBand(const std::vector<double> &t) {
  std::vector<double> a(t.size());
  for (std::size_t i = 0; i < t.size(); ++i)
    a[i] = sigmoid(t[i]);
  return split(std::move(a));
}

// Phase 1: softmax continuation. Cheap, and reliably lands in the basin of the
// minimax optimum; accurate to roughly four digits on its own.
std::vector<double> designCoarse(std::size_t m, double tw, std::mt19937 &rng) {
  Objective obj;
  obj.omegas = makeGrid(tw, 512);

  std::uniform_real_distribution<double> jitter(-0.8, 0.8);
  std::vector<double> best;
  double bestStop = 0.0;

  for (int restart = 0; restart < 6; ++restart) {
    std::vector<double> t(m);
    for (std::size_t i = 0; i < m; ++i) {
      // Spread the initial coefficients across (0,1); jitter all but the first try.
      const double a =
          std::pow((static_cast<double>(i) + 0.5) / static_cast<double>(m), 2.0);
      t[i] = std::log(a / (1.0 - a)) + (restart == 0 ? 0.0 : jitter(rng));
    }
    for (double p : {2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 256.0, 512.0}) {
      obj.p = p;
      t = nelderMead(obj, std::move(t), 4000);
    }

    const double stop = measure(toHalfBand(t), tw).stopbandDb;
    if (best.empty() || stop < bestStop) {
      best = std::move(t);
      bestStop = stop;
    }
  }
  return best;
}

// Phase 2: keep pushing the softmax towards the true maximum on a much finer grid,
// restarting the simplex with a smaller step each round. The restarts are what
// matter: a single Nelder-Mead run stalls once the simplex is small relative to the
// curvature, and phase 1 leaves the coefficients only about four digits correct.
// Only the design that is actually going to be used needs this.
//
// Guarded: a polish round is kept only if it genuinely lowers the stopband, so a
// bad step can never make the shipped filter worse than what phase 1 found.
std::vector<double> polishDesign(std::vector<double> t, double tw) {
  Objective obj;
  // Grid resolution is the accuracy limit here, not the optimiser: a coarse grid
  // systematically under-reads each ripple peak, and the coefficients absorb the
  // bias. This grid is fine enough that the bias falls below float precision.
  obj.omegas = makeGrid(tw, 40000);

  double best = measure(toHalfBand(t), tw).stopbandDb;
  for (double p : {2048.0, 32768.0, 524288.0}) {
    obj.p = p;
    for (double step : {0.01, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7, 1e-8}) {
      std::vector<double> candidate = nelderMead(obj, t, 2500, step);
      const double stop = measure(toHalfBand(candidate), tw).stopbandDb;
      if (stop < best) {
        best = stop;
        t = std::move(candidate);
      }
    }
  }
  std::fprintf(stderr, "  polish: stopband %.4f dB\n", best);
  return t;
}

void emit(const std::string &name, const HalfBand &hb, double tw, const Quality &q) {
  std::printf("// %s: transition width %.3f, %zu sections "
              "(overall order %zu)\n",
              name.c_str(), tw, hb.direct.size() + hb.delayed.size(),
              2 * (hb.direct.size() + hb.delayed.size()) + 1);
  std::printf("//   measured stopband %.2f dB, passband ripple %.2e dB, "
              "DC delay %.6f samples\n",
              q.stopbandDb, q.passbandDb, q.delaySamples);
  std::printf("inline constexpr std::array<float, %zu> k%sDirect{\n    ",
              hb.direct.size(), name.c_str());
  for (double a : hb.direct)
    std::printf("%.17gf, ", a);
  std::printf("\n};\n");
  std::printf("inline constexpr std::array<float, %zu> k%sDelayed{\n    ",
              hb.delayed.size(), name.c_str());
  for (double a : hb.delayed)
    std::printf("%.17gf, ", a);
  std::printf("\n};\n");
  std::printf("inline constexpr float k%sDelay = %.17gf;\n\n", name.c_str(),
              q.delaySamples);
}

void run(const std::string &name, double tw, double targetDb, std::mt19937 &rng) {
  std::fprintf(stderr, "=== %s: tw=%.3f target=%.1f dB ===\n", name.c_str(), tw,
               targetDb);
  for (std::size_t m = 1; m <= 10; ++m) {
    std::vector<double> t = designCoarse(m, tw, rng);
    Quality q = measure(toHalfBand(t), tw);
    std::fprintf(stderr, "  M=%zu  stopband %8.2f dB  passband %10.2e dB  delay %8.5f\n",
                 m, q.stopbandDb, q.passbandDb, q.delaySamples);
    if (q.stopbandDb <= -targetDb) {
      t = polishDesign(std::move(t), tw);
      const HalfBand hb = toHalfBand(t);
      q = measure(hb, tw);
      std::fprintf(stderr, "  final stopband %8.2f dB  passband %10.2e dB  delay %8.5f\n",
                   q.stopbandDb, q.passbandDb, q.delaySamples);
      emit(name, hb, tw, q);
      return;
    }
  }
  std::fprintf(stderr, "  !! no design up to M=10 met %.1f dB\n", targetDb);
}

} // namespace

int main() {
  std::mt19937 rng(20260729);
  // The four specifications the Linux build's 4x oversampler is configured for, so
  // that the Haiku engine's anti-alias filtering matches it rather than merely
  // resembling it. Each 2x stage uses a tighter filter going up than coming down.
  run("Stage0Up", 0.05, 90.0, rng);   // base rate -> 2x
  run("Stage0Down", 0.06, 75.0, rng); // 2x -> base rate
  run("Stage1Up", 0.10, 80.0, rng);   // 2x -> 4x
  run("Stage1Down", 0.12, 65.0, rng); // 4x -> 2x
  return 0;
}
