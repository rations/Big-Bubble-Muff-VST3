// BigBubbleMuff — the whole pedal as one nodal DK state-space model (implementation).
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Formulation (Yeh 2009 sec. 4.5; Holters & Zolzer 2015), with v the 25 unknown node
// voltages, u = [v(IN), v(VCC)], x the 13 capacitor histories and i the 10 port
// currents:
//
//   M v = Bu u + Nx' x - Nn' i                       (nodal analysis, one sample)
//
// M holds every resistor and each capacitor's trapezoidal companion conductance
// G = 2C/T. A capacitor's current is G v_c - x and its history advances as
// x <- 2 G v_c - x. Nx, Nn and No pick capacitor voltages, port voltages and OUT out
// of v. Multiplying through by M^-1 gives, per sample,
//
//   p     = D x + E u                   (port voltages if no port conducted)
//   v_n   = p + F i(v_n)                (the 10 nonlinear equations: Newton)
//   x'    = A x + B u + C i
//   v_out = Do x + Eo u + Fo i
//
// all read out of one matrix Big = L M^-1 R, with L = [2G Nx; Nn; No] and
// R = [Nx', Bu, -Nn'] (A = Big_xx - I). The pots enter M as M0 + Q diag(1/R_pot) Q',
// so by Woodbury Big = Big0 - P (R_pot + S)^-1 Qr with P = L M0^-1 Q,
// Qr = Q' M0^-1 R and S = Q' M0^-1 Q, all fixed per sample rate.
//
// Device equations are exactly ngspice-47's for the model cards in
// docs/spice/models-em.lib (bjtload.c / dioload.c with every Gummel-Poon extension at
// its default): cbe = IS (exp(vbe / NF Vt) - 1), cbc = IS (exp(vbc / NR Vt) - 1),
// Ic = cbe - cbc (1 + 1/BR), Ib = cbe/BF + cbc/BR. The per-junction GMIN (1e-12 S) and
// the cubic reverse-bias approximation below -3 N Vt are left out: both change
// currents by picoamps.
#include "dsp/circuit/BigMuffCircuit.h"

#include <algorithm>
#include <cmath>

namespace bbm::circuit {

namespace {

using bbm::at;

// Column blocks of R (and of Big): states, sources, port currents.
constexpr std::size_t kColU = kStates;
constexpr std::size_t kColI = kStates + kInputs;
// Row blocks of L (and of Big): states, ports, the output.
constexpr std::size_t kRowN = kStates;
constexpr std::size_t kRowOut = kStates + kPorts;

// Newton, per sample: the step at which the ports count as solved, and the cap. Once
// the step is d the remaining error is of order d^2 / (N Vt), about 1e-11 V here.
constexpr double kTolVolts = 1e-6;
constexpr int kMaxIterations = 50;
// A reused Jacobian is kept while each step is at most this fraction of the last.
constexpr double kChordRatio = 0.1;
// DC operating point: node-voltage step tolerance, cap, and supply-ramp steps.
constexpr double kDcTolVolts = 1e-9;
constexpr int kDcMaxIterations = 200;
constexpr int kDcRampSteps = 30;

// exp() with a linear continuation above kExpMax, so a wild Newton trial cannot
// overflow. e^50 * IS is 2.6e8 A, far beyond any current this circuit carries.
constexpr double kExpMax = 50.0;
inline void expd(double x, double &e, double &de) noexcept {
  if (x <= kExpMax) {
    e = std::exp(x);
    de = e;
  } else {
    const double em = std::exp(kExpMax);
    e = em * (1.0 + x - kExpMax);
    de = em;
  }
}

// Junction thermal voltages (N Vt) and SPICE's critical voltages N Vt ln(N Vt /
// (sqrt2 IS)) (ngspice bjttemp.c / diotemp.c), used by the limiter below.
const double kVtF = kBjtModel.nf * kThermalVolts;
const double kVtR = kBjtModel.nr * kThermalVolts;
const double kVtD = kDiodeModel.n * kThermalVolts;
const double kVcritF = kVtF * std::log(kVtF / (std::sqrt(2.0) * kBjtModel.is));
const double kVcritR = kVtR * std::log(kVtR / (std::sqrt(2.0) * kBjtModel.is));
const double kVcritD = kVtD * std::log(kVtD / (std::sqrt(2.0) * kDiodeModel.is));

// SPICE3's pn-junction step limiter, following ngspice-47 devsup.c DEVpnjlim
// (Copyright 1990 Regents of the University of California; Modified BSD licence,
// reproduced in NOTICE): a forward step larger than 2 N Vt above the critical
// voltage is compressed logarithmically, and a reverse step is bounded. A
// convergence aid only; the solution is unchanged.
inline double pnjlim(double vnew, double vold, double vt, double vcrit,
                     bool &limited) noexcept {
  if (vnew > vcrit && std::fabs(vnew - vold) > vt + vt) {
    limited = true;
    if (vold > 0.0) {
      const double arg = (vnew - vold) / vt;
      return arg > 0.0 ? vold + vt * (2.0 + std::log(arg - 2.0))
                       : vold - vt * (2.0 + std::log(2.0 - arg));
    }
    return vt * std::log(vnew / vt);
  }
  if (vnew < 0.0) {
    const double floor = vold > 0.0 ? -vold - 1.0 : 2.0 * vold - 1.0;
    if (vnew < floor) {
      limited = true;
      return floor;
    }
  }
  return vnew;
}

// A port as a pair of nodes: v_port = v(plus) - v(minus).
struct PortNodes {
  int plus;
  int minus;
};
constexpr std::array<PortNodes, kPorts> makePortNodes() {
  std::array<PortNodes, kPorts> p{};
  for (std::size_t j = 0; j < kBjts.size(); ++j) {
    at(p, 2 * j) = {at(kBjts, j).b, at(kBjts, j).e};     // base-emitter
    at(p, 2 * j + 1) = {at(kBjts, j).b, at(kBjts, j).c}; // base-collector
  }
  for (std::size_t d = 0; d < kDiodePairs.size(); ++d)
    at(p, 2 * kBjts.size() + d) = {at(kDiodePairs, d).anode, at(kDiodePairs, d).cathode};
  return p;
}
constexpr std::array<PortNodes, kPorts> kPortNodes = makePortNodes();

// The pot halves as node pairs: pin3-wiper, then wiper-pin1, for each pot.
constexpr std::array<PortNodes, kVariable> makeHalfNodes() {
  std::array<PortNodes, kVariable> h{};
  for (std::size_t k = 0; k < kPots.size(); ++k) {
    at(h, 2 * k) = {at(kPots, k).pin3, at(kPots, k).wiper};
    at(h, 2 * k + 1) = {at(kPots, k).wiper, at(kPots, k).pin1};
  }
  return h;
}
constexpr std::array<PortNodes, kVariable> kHalfNodes = makeHalfNodes();

static_assert(kPorts == 2 * kBjts.size() + kDiodePairs.size());
static_assert(kVariable == 2 * kPots.size());

constexpr int sourceIndex(int node) {
  return node == IN ? 0 : (node == VCC ? 1 : -1);
}

// Adds a conductance g between nodes a and b: into M for unknown nodes, and into Bu
// where the other end is a source.
template <std::size_t N>
void stamp(Mat<N, N> &m, Mat<N, kInputs> &bu, int a, int b, double g) noexcept {
  const auto ua = static_cast<std::size_t>(a);
  const auto ub = static_cast<std::size_t>(b);
  if (a >= 0)
    m(ua, ua) += g;
  if (b >= 0)
    m(ub, ub) += g;
  if (a >= 0 && b >= 0) {
    m(ua, ub) -= g;
    m(ub, ua) -= g;
  }
  if (a >= 0 && sourceIndex(b) >= 0)
    bu(ua, static_cast<std::size_t>(sourceIndex(b))) += g;
  if (b >= 0 && sourceIndex(a) >= 0)
    bu(ub, static_cast<std::size_t>(sourceIndex(a))) += g;
}

// v(a) - v(b) for column c of a node-indexed matrix (ground and sources read 0: the
// matrices are responses, and sources enter only through Bu).
template <std::size_t C>
double across(const Mat<kNodes, C> &m, int a, int b, std::size_t c) noexcept {
  const double va = a >= 0 ? m(static_cast<std::size_t>(a), c) : 0.0;
  const double vb = b >= 0 ? m(static_cast<std::size_t>(b), c) : 0.0;
  return va - vb;
}
double across(const Vec<kNodes> &v, int a, int b) noexcept {
  const double va = a >= 0 ? at(v, a) : 0.0;
  const double vb = b >= 0 ? at(v, b) : 0.0;
  return va - vb;
}

// The fixed conductances (resistors and the load), with no capacitors or pots.
void stampResistors(Mat<kNodes, kNodes> &m, Mat<kNodes, kInputs> &bu) noexcept {
  for (const Resistor &r : kResistors)
    stamp(m, bu, r.a, r.b, 1.0 / r.ohms);
  stamp(m, bu, kLoad.a, kLoad.b, 1.0 / kLoad.ohms);
}

double clamp01(double k) noexcept {
  return k > 0.0 ? (k < 1.0 ? k : 1.0) : 0.0; // NaN -> 0
}

bool finite(double x) noexcept {
  return std::isfinite(x);
}

} // namespace

bool BigMuffCircuit::prepare(double fs) noexcept {
  mReady = false;
  mDcValid = false;
  if (!(fs > 0.0) || !finite(fs))
    return false;

  Mat<kNodes, kNodes> m0{};
  Mat<kNodes, kInputs> bu{};
  stampResistors(m0, bu);
  for (std::size_t k = 0; k < kStates; ++k) {
    const Capacitor &c = at(kCapacitors, k);
    at(mCapG, k) = 2.0 * c.farads * fs;
    stamp(m0, bu, c.a, c.b, at(mCapG, k));
  }
  Lu<kNodes> lu;
  if (!lu.factor(m0))
    return false;

  // Solve M0 against every column of R and of Q at once.
  constexpr std::size_t kRhs = kCols + kVariable;
  Mat<kNodes, kRhs> rhs{};
  const auto put = [&rhs](int node, std::size_t col, double value) {
    if (node >= 0)
      rhs(static_cast<std::size_t>(node), col) += value;
  };
  for (std::size_t k = 0; k < kStates; ++k) {
    put(at(kCapacitors, k).a, k, 1.0);
    put(at(kCapacitors, k).b, k, -1.0);
  }
  for (std::size_t r = 0; r < kNodes; ++r)
    for (std::size_t s = 0; s < kInputs; ++s)
      rhs(r, kColU + s) = bu(r, s);
  for (std::size_t p = 0; p < kPorts; ++p) {
    put(at(kPortNodes, p).plus, kColI + p, -1.0);
    put(at(kPortNodes, p).minus, kColI + p, 1.0);
  }
  for (std::size_t h = 0; h < kVariable; ++h) {
    put(at(kHalfNodes, h).plus, kCols + h, 1.0);
    put(at(kHalfNodes, h).minus, kCols + h, -1.0);
  }
  const Mat<kNodes, kRhs> sol = lu.solve(rhs);

  // Row r of L applied to column c of M0^-1 [R Q].
  const auto lrow = [&](std::size_t r, std::size_t c) {
    if (r < kRowN) {
      const Capacitor &cap = at(kCapacitors, r);
      return 2.0 * at(mCapG, r) * across(sol, cap.a, cap.b, c);
    }
    if (r < kRowOut)
      return across(sol, at(kPortNodes, r - kRowN).plus, at(kPortNodes, r - kRowN).minus,
                    c);
    return across(sol, OUT, GND, c);
  };
  for (std::size_t r = 0; r < kRows; ++r) {
    for (std::size_t c = 0; c < kCols; ++c)
      mBig0(r, c) = lrow(r, c);
    for (std::size_t h = 0; h < kVariable; ++h)
      mP(r, h) = lrow(r, kCols + h);
  }
  for (std::size_t h = 0; h < kVariable; ++h) {
    const PortNodes &hn = at(kHalfNodes, h);
    for (std::size_t c = 0; c < kCols; ++c)
      mQr(h, c) = across(sol, hn.plus, hn.minus, c);
    for (std::size_t g = 0; g < kVariable; ++g)
      mS(h, g) = across(sol, hn.plus, hn.minus, kCols + g);
  }

  mReady = true;
  updateSystem();
  mStats = {};
  if (!settleToDc()) {
    mReady = false;
    return false;
  }
  return true;
}

void BigMuffCircuit::setKnobs(const Knobs &k) noexcept {
  const std::array<double, kPots.size()> knob{clamp01(k.sustain), clamp01(k.tone),
                                              clamp01(k.volume)};
  std::array<double, kVariable> ohms{};
  for (std::size_t p = 0; p < kPots.size(); ++p) {
    const double total = at(kPots, p).ohms;
    at(ohms, 2 * p) = std::max(kPotMinOhms, (1.0 - at(knob, p)) * total);
    at(ohms, 2 * p + 1) = std::max(kPotMinOhms, at(knob, p) * total);
  }
  if (ohms == mPotOhms)
    return;
  mPotOhms = ohms;
  if (mReady)
    updateSystem();
}

void BigMuffCircuit::updateSystem() noexcept {
  Mat<kVariable, kVariable> k = mS;
  for (std::size_t h = 0; h < kVariable; ++h)
    k(h, h) += at(mPotOhms, h);
  Lu<kVariable> lu;
  if (!lu.factor(k))
    return; // R + S is positive definite for positive R; kept for robustness
  const Mat<kVariable, kCols> y = lu.solve(mQr);
  for (std::size_t r = 0; r < kRows; ++r)
    for (std::size_t c = 0; c < kCols; ++c) {
      double b = mBig0(r, c);
      for (std::size_t h = 0; h < kVariable; ++h)
        b -= mP(r, h) * y(h, c);
      mBigT(c, r) = b;
    }
}

void BigMuffCircuit::evalPorts(const Vec<kPorts> &v, Vec<kPorts> &i,
                               PortJacobian &j) noexcept {
  const BjtModel &q = kBjtModel;
  const double af = 1.0 + 1.0 / q.bf;
  const double ar = 1.0 + 1.0 / q.br;
  for (std::size_t n = 0; n < kBjts.size(); ++n) {
    double ef = 0.0;
    double def = 0.0;
    double er = 0.0;
    double der = 0.0;
    expd(at(v, 2 * n) / kVtF, ef, def);
    expd(at(v, 2 * n + 1) / kVtR, er, der);
    const double cbe = q.is * (ef - 1.0);
    const double cbc = q.is * (er - 1.0);
    const double gbe = q.is / kVtF * def;
    const double gbc = q.is / kVtR * der;
    // Port currents: base -> emitter (= -Ie) and base -> collector (= -Ic).
    at(i, 2 * n) = af * cbe - cbc;
    at(i, 2 * n + 1) = ar * cbc - cbe;
    at(j.bjt, n) = {af * gbe, -gbc, -gbe, ar * gbc};
  }
  const DiodeModel &d = kDiodeModel;
  for (std::size_t n = 0; n < kDiodePairs.size(); ++n) {
    const std::size_t p = 2 * kBjts.size() + n;
    const double x = at(v, p) / kVtD;
    double ep = 0.0;
    double dep = 0.0;
    double en = 0.0;
    double den = 0.0;
    if (std::fabs(x) <= kExpMax) {
      ep = std::exp(x); // the partner's exp(-x) is its reciprocal
      dep = ep;
      en = 1.0 / ep;
      den = en;
    } else {
      expd(x, ep, dep);
      expd(-x, en, den);
    }
    at(i, p) = d.is * (ep - en);
    at(j.diode, n) = d.is / kVtD * (dep + den);
  }
}

bool BigMuffCircuit::limitPorts(Vec<kPorts> &vnew, const Vec<kPorts> &vold) noexcept {
  bool limited = false;
  for (std::size_t n = 0; n < kBjts.size(); ++n) {
    at(vnew, 2 * n) = pnjlim(at(vnew, 2 * n), at(vold, 2 * n), kVtF, kVcritF, limited);
    at(vnew, 2 * n + 1) =
        pnjlim(at(vnew, 2 * n + 1), at(vold, 2 * n + 1), kVtR, kVcritR, limited);
  }
  // An antiparallel pair: limit whichever diode the new voltage forward-biases.
  for (std::size_t n = 0; n < kDiodePairs.size(); ++n) {
    const std::size_t p = 2 * kBjts.size() + n;
    const double s = at(vnew, p) >= 0.0 ? 1.0 : -1.0;
    at(vnew, p) = s * pnjlim(s * at(vnew, p), s * at(vold, p), kVtD, kVcritD, limited);
  }
  return limited;
}

bool BigMuffCircuit::solveDc(Vec<kNodes> &v, bool ramp) const noexcept {
  // Capacitors open: resistors, the load and the pots at the current knobs.
  Mat<kNodes, kNodes> g{};
  Mat<kNodes, kInputs> bu{};
  stampResistors(g, bu);
  for (std::size_t h = 0; h < kVariable; ++h)
    stamp(g, bu, at(kHalfNodes, h).plus, at(kHalfNodes, h).minus, 1.0 / at(mPotOhms, h));

  const auto portVoltages = [](const Vec<kNodes> &nodes) {
    Vec<kPorts> vp{};
    for (std::size_t p = 0; p < kPorts; ++p)
      at(vp, p) = across(nodes, at(kPortNodes, p).plus, at(kPortNodes, p).minus);
    return vp;
  };

  Vec<kPorts> vj = portVoltages(v);
  const int steps = ramp ? kDcRampSteps : 1;
  for (int s = 1; s <= steps; ++s) {
    const Vec<kInputs> u{0.0, kSupplyVolts * s / steps};
    bool converged = false;
    for (int it = 0; it < kDcMaxIterations && !converged; ++it) {
      // SPICE's scheme: limit the junction voltages the nodes imply, linearise the
      // devices there, and solve the companion network for the next node voltages.
      Vec<kPorts> vl = portVoltages(v);
      const bool limited = limitPorts(vl, vj);
      vj = vl;
      Vec<kPorts> i{};
      PortJacobian jac{};
      evalPorts(vj, i, jac);

      Mat<kNodes, kNodes> a = g;
      Vec<kNodes> rhs = mul(bu, u);
      const auto addJ = [&a](std::size_t p, std::size_t q, double value) {
        const PortNodes &np = at(kPortNodes, p);
        const PortNodes &nq = at(kPortNodes, q);
        const std::array<int, 2> rows{np.plus, np.minus};
        const std::array<int, 2> cols{nq.plus, nq.minus};
        for (std::size_t x = 0; x < 2; ++x)
          for (std::size_t y = 0; y < 2; ++y)
            if (at(rows, x) >= 0 && at(cols, y) >= 0)
              a(static_cast<std::size_t>(at(rows, x)),
                static_cast<std::size_t>(at(cols, y))) += (x == y ? value : -value);
      };
      Vec<kPorts> ieq = i; // i - J vj: the companion source of each port
      for (std::size_t n = 0; n < kBjts.size(); ++n) {
        const std::array<double, 4> &b = at(jac.bjt, n);
        const std::size_t pe = 2 * n;
        const std::size_t pc = 2 * n + 1;
        addJ(pe, pe, b[0]);
        addJ(pe, pc, b[1]);
        addJ(pc, pe, b[2]);
        addJ(pc, pc, b[3]);
        at(ieq, pe) -= b[0] * at(vj, pe) + b[1] * at(vj, pc);
        at(ieq, pc) -= b[2] * at(vj, pe) + b[3] * at(vj, pc);
      }
      for (std::size_t n = 0; n < kDiodePairs.size(); ++n) {
        const std::size_t p = 2 * kBjts.size() + n;
        addJ(p, p, at(jac.diode, n));
        at(ieq, p) -= at(jac.diode, n) * at(vj, p);
      }
      for (std::size_t p = 0; p < kPorts; ++p) {
        const PortNodes &np = at(kPortNodes, p);
        if (np.plus >= 0)
          at(rhs, np.plus) -= at(ieq, p);
        if (np.minus >= 0)
          at(rhs, np.minus) += at(ieq, p);
      }

      Lu<kNodes> lu;
      if (!lu.factor(a))
        return false;
      const Vec<kNodes> next = lu.solve(rhs);
      double step = 0.0;
      for (std::size_t n = 0; n < kNodes; ++n) {
        if (!finite(at(next, n)))
          return false;
        step = std::max(step, std::fabs(at(next, n) - at(v, n)));
      }
      v = next;
      converged = !limited && step < kDcTolVolts;
    }
    if (!converged)
      return false;
  }
  return true;
}

bool BigMuffCircuit::settleToDc() noexcept {
  if (!mReady)
    return false;
  // From the last operating point first (a knob move shifts it only a little), then
  // from a cold start with the supply ramped up (source-stepping homotopy).
  Vec<kNodes> v = mDcNodes;
  bool ok = mDcValid && solveDc(v, false);
  if (!ok) {
    v = {};
    ok = solveDc(v, true);
  }
  if (!ok)
    return false;

  mDcNodes = v;
  mDcValid = true;
  // At rest no capacitor carries current: G v_c - x = 0.
  for (std::size_t k = 0; k < kStates; ++k)
    at(mX, k) = at(mCapG, k) * across(v, at(kCapacitors, k).a, at(kCapacitors, k).b);
  for (std::size_t p = 0; p < kPorts; ++p)
    at(mVn, p) = across(v, at(kPortNodes, p).plus, at(kPortNodes, p).minus);
  mOut = at(v, OUT);
  mLuValid = false;
  return true;
}

double BigMuffCircuit::process(double vin) noexcept {
  if (!mReady)
    return 0.0;
  ++mStats.samples;

  Vec<kColI> z{};
  for (std::size_t k = 0; k < kStates; ++k)
    at(z, k) = at(mX, k);
  at(z, kColU) = finite(vin) ? vin : 0.0;
  at(z, kColU + 1) = kSupplyVolts;

  // Every row of Big times the known part of z = [x, u, i]; the port rows are then
  // p, the port voltages were no port to conduct.
  Vec<kRows> y{};
  for (std::size_t c = 0; c < kColI; ++c) {
    const double zc = at(z, c);
    for (std::size_t r = 0; r < kRows; ++r)
      at(y, r) += mBigT(c, r) * zc;
  }
  Vec<kPorts> p{};
  for (std::size_t r = 0; r < kPorts; ++r)
    at(p, r) = at(y, kRowN + r);

  // Predictor: one chord step with last sample's Jacobian, v -= J^-1 (p - p_prev),
  // which is exact for the linear part of the change.
  Vec<kPorts> v = mVn;
  double prevStep = 0.0;
  if (mLuValid) {
    Vec<kPorts> dp{};
    for (std::size_t r = 0; r < kPorts; ++r)
      at(dp, r) = at(p, r) - at(mPrevP, r);
    const Vec<kPorts> step = mulTransposed(mJinvT, dp);
    Vec<kPorts> guess{};
    for (std::size_t r = 0; r < kPorts; ++r)
      at(guess, r) = at(v, r) - at(step, r);
    limitPorts(guess, v);
    for (std::size_t r = 0; r < kPorts; ++r)
      prevStep = std::max(prevStep, std::fabs(at(guess, r) - at(v, r)));
    v = guess;
  }

  // Newton, but keeping the last factorised Jacobian (a chord step) for as long as
  // each step shrinks by kChordRatio or better: at 4x oversampling the Jacobian moves
  // little between samples, and a fresh LU costs more than the rest of the sample.
  Vec<kPorts> i{};
  PortJacobian jac{};
  bool converged = false;
  bool refactor = !mLuValid;
  int it = 0;
  while (it < kMaxIterations) {
    ++it;
    evalPorts(v, i, jac);
    // Residual g = p + F i - v and Jacobian J = F dI/dV - I.
    // F(r, k) is Big(kRowN + r, kColI + k), i.e. mBigT(kColI + k, kRowN + r).
    Vec<kPorts> g{};
    for (std::size_t r = 0; r < kPorts; ++r)
      at(g, r) = at(p, r) - at(v, r);
    for (std::size_t k = 0; k < kPorts; ++k) {
      const double ik = at(i, k);
      for (std::size_t r = 0; r < kPorts; ++r)
        at(g, r) += mBigT(kColI + k, kRowN + r) * ik;
    }
    if (refactor) {
      Mat<kPorts, kPorts> jm{};
      for (std::size_t r = 0; r < kPorts; ++r) {
        for (std::size_t n = 0; n < kBjts.size(); ++n) {
          const std::array<double, 4> &b = at(jac.bjt, n);
          const double fe = mBigT(kColI + 2 * n, kRowN + r);
          const double fc = mBigT(kColI + 2 * n + 1, kRowN + r);
          jm(r, 2 * n) = fe * b[0] + fc * b[2];
          jm(r, 2 * n + 1) = fe * b[1] + fc * b[3];
        }
        for (std::size_t n = 0; n < kDiodePairs.size(); ++n) {
          const std::size_t q = 2 * kBjts.size() + n;
          jm(r, q) = mBigT(kColI + q, kRowN + r) * at(jac.diode, n);
        }
        jm(r, r) -= 1.0;
      }
      Lu<kPorts> lu;
      mLuValid = lu.factor(jm);
      if (!mLuValid)
        break;
      mJinvT = lu.inverseTransposed();
      ++mStats.factorisations;
    }
    const Vec<kPorts> dv = mulTransposed(mJinvT, g);
    Vec<kPorts> next{};
    for (std::size_t r = 0; r < kPorts; ++r)
      at(next, r) = at(v, r) - at(dv, r);
    const bool limited = limitPorts(next, v);
    double step = 0.0;
    for (std::size_t r = 0; r < kPorts; ++r)
      step = std::max(step, std::fabs(at(next, r) - at(v, r)));
    if (!limited && step < kTolVolts) {
      // Solved: take the currents at the final point from the linearisation.
      for (std::size_t n = 0; n < kBjts.size(); ++n) {
        const std::array<double, 4> &b = at(jac.bjt, n);
        const double de = at(next, 2 * n) - at(v, 2 * n);
        const double dc = at(next, 2 * n + 1) - at(v, 2 * n + 1);
        at(i, 2 * n) += b[0] * de + b[1] * dc;
        at(i, 2 * n + 1) += b[2] * de + b[3] * dc;
      }
      for (std::size_t n = 0; n < kDiodePairs.size(); ++n) {
        const std::size_t q = 2 * kBjts.size() + n;
        at(i, q) += at(jac.diode, n) * (at(next, q) - at(v, q));
      }
      v = next;
      converged = true;
      break;
    }
    if (!refactor && step > kChordRatio * prevStep)
      refactor = true; // the old Jacobian no longer contracts fast enough
    prevStep = step;
    v = next;
  }
  mStats.iterations += it;
  if (!converged) {
    ++mStats.unconverged;
    evalPorts(v, i, jac); // currents consistent with where Newton stopped
  }

  // The port currents complete z; finish every row (the port rows are unused now).
  for (std::size_t k = 0; k < kPorts; ++k) {
    const double ik = at(i, k);
    for (std::size_t r = 0; r < kRows; ++r)
      at(y, r) += mBigT(kColI + k, r) * ik;
  }
  const double out = at(y, kRowOut);
  Vec<kStates> x{};
  bool ok = finite(out);
  for (std::size_t k = 0; k < kStates; ++k) {
    at(x, k) = at(y, k) - at(mX, k);
    ok = ok && finite(at(x, k));
  }
  for (std::size_t r = 0; r < kPorts; ++r)
    ok = ok && finite(at(v, r));
  if (!ok) {
    // Keep the last good state; the next sample starts from it again.
    ++mStats.rejected;
    mLuValid = false;
    return mOut;
  }
  mX = x;
  mVn = v;
  mPrevP = p;
  mOut = out;
  return out;
}

} // namespace bbm::circuit
