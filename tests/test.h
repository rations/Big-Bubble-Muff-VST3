// BigBubbleMuff — a minimal, dependency-free test harness.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// No third-party test framework (supply-chain minimalism). A test case is a
// function registered by TEST_CASE; CHECK records a failure and carries on, so one
// run reports every broken expectation. The runner (test_main.cpp) exits non-zero
// if any check failed.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace bbmtest {

struct Case {
  const char *suite;
  const char *name;
  void (*fn)();
};

std::vector<Case> &registry();
void fail(const char *expr, const char *file, int line, const std::string &msg);
void log(const std::string &msg);

// Global allocation counter (test_main.cpp replaces operator new). Armed by
// AllocationGuard; counts every allocation made while armed.
long allocationCount();
void armAllocationCounter(bool armed);

struct AllocationGuard {
  long start;
  AllocationGuard() : start(allocationCount()) { armAllocationCounter(true); }
  ~AllocationGuard() { armAllocationCounter(false); }
  AllocationGuard(const AllocationGuard &) = delete;
  AllocationGuard &operator=(const AllocationGuard &) = delete;
  long allocations() const { return allocationCount() - start; }
};

struct Registrar {
  Registrar(const char *suite, const char *name, void (*fn)()) {
    registry().push_back({suite, name, fn});
  }
};

} // namespace bbmtest

#define BBM_TEST_CAT2(a, b) a##b
#define BBM_TEST_CAT(a, b) BBM_TEST_CAT2(a, b)

#define TEST_CASE(suite, name)                                                           \
  static void BBM_TEST_CAT(bbmTestFn_, __LINE__)();                                      \
  static const bbmtest::Registrar BBM_TEST_CAT(bbmTestReg_, __LINE__){                   \
      suite, name, &BBM_TEST_CAT(bbmTestFn_, __LINE__)};                                 \
  static void BBM_TEST_CAT(bbmTestFn_, __LINE__)()

#define CHECK(expr) CHECK_MSG(expr, "")
#define CHECK_MSG(expr, msg)                                                             \
  do {                                                                                   \
    if (!(expr))                                                                         \
      bbmtest::fail(#expr, __FILE__, __LINE__, (msg));                                   \
  } while (0)
