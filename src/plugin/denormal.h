// BigBubbleMuff — flush-to-zero / denormals-are-zero for the audio thread.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Re-armed at the top of every process() call: a host is not required to set
// FTZ/DAZ on its audio threads and may call process() from a different thread than
// the one that activated the plug-in, and subnormals in the circuit's feedback and
// filter state stall the CPU badly enough to miss the real-time deadline.
//
// x86-64 needs two bits (FTZ flushes subnormal results, DAZ treats subnormal
// inputs as zero). AArch64 needs one: FPCR bit 24 (FZ) does both. Read-modify-write
// so the rounding mode and every other FPCR bit are left alone. The same pattern as
// rations-pedals src/common/denormal.h.
#pragma once

#if defined(__SSE__) || defined(__x86_64__)
#include <pmmintrin.h>
#include <xmmintrin.h>
#endif

namespace bbm {

inline void setDenormalMode() {
#if defined(__SSE__) || defined(__x86_64__)
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(__aarch64__)
  unsigned long fpcr = 0;
  __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
  const unsigned long armed = fpcr | (1UL << 24);
  if (armed != fpcr)
    __asm__ __volatile__("msr fpcr, %0" : : "r"(armed));
#endif
}

} // namespace bbm
