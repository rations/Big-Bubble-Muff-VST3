// BigBubbleMuff — fixed-size dense linear algebra for the circuit solver.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Sizes are compile-time, storage is inline (std::array), nothing allocates, so every
// routine here is real-time-safe. Row-major. Only what the DK solver needs:
// LU factorisation with partial pivoting, and solving with it.
#pragma once

#include "dsp/Checked.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace bbm::circuit {

template <std::size_t R, std::size_t C> struct Mat {
  std::array<double, R * C> a{};

  constexpr double &operator()(std::size_t r, std::size_t c) noexcept {
    return bbm::at(a, r * C + c);
  }
  constexpr double operator()(std::size_t r, std::size_t c) const noexcept {
    return bbm::at(a, r * C + c);
  }
  static constexpr std::size_t rows() { return R; }
  static constexpr std::size_t cols() { return C; }
};

template <std::size_t N> using Vec = std::array<double, N>;

// y = M x
template <std::size_t R, std::size_t C>
constexpr Vec<R> mul(const Mat<R, C> &m, const Vec<C> &x) noexcept {
  Vec<R> y{};
  for (std::size_t r = 0; r < R; ++r) {
    double s = 0.0;
    for (std::size_t c = 0; c < C; ++c)
      s += m(r, c) * bbm::at(x, c);
    bbm::at(y, r) = s;
  }
  return y;
}

// y = M' x, for M stored transposed (each input's column contiguous): independent
// accumulators, so it vectorises without reassociating a sum.
template <std::size_t R, std::size_t C>
constexpr Vec<C> mulTransposed(const Mat<R, C> &mt, const Vec<R> &x) noexcept {
  Vec<C> y{};
  for (std::size_t k = 0; k < R; ++k) {
    const double xk = bbm::at(x, k);
    for (std::size_t r = 0; r < C; ++r)
      bbm::at(y, r) += mt(k, r) * xk;
  }
  return y;
}

// P = A B
template <std::size_t R, std::size_t K, std::size_t C>
Mat<R, C> mul(const Mat<R, K> &a, const Mat<K, C> &b) noexcept {
  Mat<R, C> p{};
  for (std::size_t r = 0; r < R; ++r)
    for (std::size_t k = 0; k < K; ++k) {
      const double ark = a(r, k);
      for (std::size_t c = 0; c < C; ++c)
        p(r, c) += ark * b(k, c);
    }
  return p;
}

// LU factorisation with partial pivoting of an N x N matrix, PA = LU. The factors are
// kept column-major (column j of L and U contiguous), so both the elimination and
// the substitutions run as independent column sweeps (axpy) rather than serial dot
// products; pivots are stored as reciprocals so solving never divides. factor()
// returns false for a (numerically) singular matrix.
template <std::size_t N> struct Lu {
  std::array<double, N * N> t{}; // t[c * N + r] = (L\U)(r, c)
  std::array<double, N> invPivot{};
  std::array<std::size_t, N> perm{};

  double &lu(std::size_t r, std::size_t c) noexcept { return bbm::at(t, c * N + r); }
  double lu(std::size_t r, std::size_t c) const noexcept { return bbm::at(t, c * N + r); }

  bool factor(const Mat<N, N> &src) noexcept {
    for (std::size_t r = 0; r < N; ++r) {
      bbm::at(perm, r) = r;
      for (std::size_t c = 0; c < N; ++c)
        lu(r, c) = src(r, c);
    }
    for (std::size_t k = 0; k < N; ++k) {
      std::size_t p = k;
      double best = std::fabs(lu(k, k));
      for (std::size_t r = k + 1; r < N; ++r)
        if (std::fabs(lu(r, k)) > best) {
          best = std::fabs(lu(r, k));
          p = r;
        }
      if (!(best > 1e-300))
        return false;
      if (p != k) {
        for (std::size_t c = 0; c < N; ++c)
          std::swap(lu(k, c), lu(p, c));
        std::swap(bbm::at(perm, k), bbm::at(perm, p));
      }
      const double inv = 1.0 / lu(k, k);
      bbm::at(invPivot, k) = inv;
      for (std::size_t r = k + 1; r < N; ++r)
        lu(r, k) *= inv;
      for (std::size_t c = k + 1; c < N; ++c) {
        const double ukc = lu(k, c);
        for (std::size_t r = k + 1; r < N; ++r)
          lu(r, c) -= lu(r, k) * ukc;
      }
    }
    return true;
  }

  // Solve (the factored matrix) x = b.
  Vec<N> solve(const Vec<N> &b) const noexcept {
    Vec<N> x{};
    for (std::size_t i = 0; i < N; ++i)
      bbm::at(x, i) = bbm::at(b, bbm::at(perm, i));
    for (std::size_t j = 0; j < N; ++j) {
      const double xj = bbm::at(x, j);
      for (std::size_t i = j + 1; i < N; ++i)
        bbm::at(x, i) -= lu(i, j) * xj;
    }
    for (std::size_t j = N; j-- > 0;) {
      const double xj = bbm::at(x, j) * bbm::at(invPivot, j);
      bbm::at(x, j) = xj;
      for (std::size_t i = 0; i < j; ++i)
        bbm::at(x, i) -= lu(i, j) * xj;
    }
    return x;
  }

  // The explicit inverse, transposed: row k of the result is column k of A^-1, so
  // A^-1 b is a sweep over contiguous rows (see mulTransposed).
  Mat<N, N> inverseTransposed() const noexcept {
    Mat<N, N> it{};
    for (std::size_t k = 0; k < N; ++k) {
      Vec<N> e{};
      bbm::at(e, k) = 1.0;
      const Vec<N> col = solve(e);
      for (std::size_t r = 0; r < N; ++r)
        it(k, r) = bbm::at(col, r);
    }
    return it;
  }

  // Solve for every column of B at once: returns X with (matrix) X = B.
  template <std::size_t C> Mat<N, C> solve(const Mat<N, C> &b) const noexcept {
    Mat<N, C> x{};
    for (std::size_t c = 0; c < C; ++c) {
      Vec<N> col{};
      for (std::size_t r = 0; r < N; ++r)
        bbm::at(col, r) = b(r, c);
      const Vec<N> s = solve(col);
      for (std::size_t r = 0; r < N; ++r)
        x(r, c) = bbm::at(s, r);
    }
    return x;
  }
};

} // namespace bbm::circuit
