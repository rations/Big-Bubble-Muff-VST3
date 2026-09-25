// BigBubbleMuff — test runner.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#include "test.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
int g_failures = 0;
std::atomic<long> g_allocations{0};
std::atomic<bool> g_counting{false};
} // namespace

namespace bbmtest {

std::vector<Case> &registry() {
  static std::vector<Case> cases;
  return cases;
}

void fail(const char *expr, const char *file, int line, const std::string &msg) {
  ++g_failures;
  std::printf("  FAILED: %s  (%s:%d)%s%s\n", expr, file, line, msg.empty() ? "" : "  ",
              msg.c_str());
}

void log(const std::string &msg) {
  std::printf("    %s\n", msg.c_str());
}

long allocationCount() {
  return g_allocations.load();
}

void armAllocationCounter(bool armed) {
  g_counting.store(armed);
}

} // namespace bbmtest

// Counting replacements for the global allocation functions: every allocation
// made while an AllocationGuard is alive is counted. This is how the real-time
// contract (no allocation in process()) is proved rather than asserted.
void *operator new(std::size_t size) {
  if (g_counting.load(std::memory_order_relaxed))
    g_allocations.fetch_add(1, std::memory_order_relaxed);
  if (void *p = std::malloc(size == 0 ? 1 : size))
    return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t size) {
  return ::operator new(size);
}
void operator delete(void *p) noexcept {
  std::free(p);
}
void operator delete[](void *p) noexcept {
  std::free(p);
}
void operator delete(void *p, std::size_t) noexcept {
  std::free(p);
}
void operator delete[](void *p, std::size_t) noexcept {
  std::free(p);
}

int main() {
  const char *suite = "";
  for (const bbmtest::Case &c : bbmtest::registry()) {
    if (std::string(suite) != c.suite) {
      suite = c.suite;
      std::printf("[%s]\n", suite);
    }
    std::printf("  - %s\n", c.name);
    c.fn();
  }
  if (g_failures > 0) {
    std::printf("FAILED: %d check(s)\n", g_failures);
    return 1;
  }
  std::printf("All BigBubbleMuff tests passed (%zu cases).\n",
              bbmtest::registry().size());
  return 0;
}
