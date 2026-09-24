// BigBubbleMuff — audio processor.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Wraps the circuit engine (dsp/BigMuffPi.h) in a raw VST3 IAudioProcessor.
//
// SIGNAL PATH
//   host in -> [dry copy, delayed by the reported latency] ----------------+
//           -> mono average -> engine (gate, 4x circuit, trim) -> wet ---- mix -> out
//   mix = footswitch ramp (8 ms) x (1 - host bypass ramp (5 ms))
// The pedal is mono: every output channel carries the one wet signal; the dry
// path stays per channel, so a bypassed stereo feed passes through as stereo.
// The footswitch is the pedal's own true-bypass switch; kBypassId is the host's.
// Both ramp, neither steps. Once the footswitch has fully faded out, the engine is
// skipped and costs nothing.
//
// REAL-TIME CONTRACT: process() never allocates, locks, logs or performs I/O.
// Every buffer is sized in setupProcessing; FTZ/DAZ are re-armed on every call.
#pragma once

#include "dsp/BigMuffPi.h"
#include "plugin/ids.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <vector>

namespace bbm {

class Processor : public Steinberg::Vst::AudioEffect {
public:
  Processor();
  ~Processor() override;
  Processor(const Processor &) = delete;
  Processor &operator=(const Processor &) = delete;
  Processor(Processor &&) = delete;
  Processor &operator=(Processor &&) = delete;

  static Steinberg::FUnknown *createInstance(void *) {
    return static_cast<Steinberg::Vst::IAudioProcessor *>(new Processor());
  }

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) override;
  Steinberg::tresult PLUGIN_API setBusArrangements(
      Steinberg::Vst::SpeakerArrangement *inputs, Steinberg::int32 numIns,
      Steinberg::Vst::SpeakerArrangement *outputs, Steinberg::int32 numOuts) override;
  Steinberg::tresult PLUGIN_API
  canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;
  Steinberg::tresult PLUGIN_API
  setupProcessing(Steinberg::Vst::ProcessSetup &setup) override;
  Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
  Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData &data) override;
  Steinberg::uint32 PLUGIN_API getLatencySamples() override;
  Steinberg::uint32 PLUGIN_API getTailSamples() override;

  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream *state) override;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream *state) override;

  // Host-facing latency: the oversampler's DC group delay, rounded.
  static Steinberg::uint32 latency();

private:
  static constexpr int kMaxChannels = 2;

  void handleParameterChanges(Steinberg::Vst::IParameterChanges *changes);
  void pushControls();
  void processChunk(float *const *in, float *const *out, Steinberg::int32 inCh,
                    Steinberg::int32 outCh, Steinberg::int32 offset,
                    Steinberg::int32 n) noexcept;

  // Normalised values. Written by the audio thread (parameter queues) and by
  // setState on the message thread, hence atomic.
  std::array<std::atomic<double>, kParamCount> mNorm{};
  std::atomic<double> mHostBypass{0.0};

  BigMuffPi mEngine;

  double mSampleRate = 48000.0;
  Steinberg::int32 mMaxBlock = 0;

  std::vector<float> mMono; // averaged input, then wet output
  std::array<std::vector<float>, kMaxChannels> mDry{};
  // Per-channel dry delay line of latency() samples (ring buffer), so the dry and
  // wet paths are time-aligned through every crossfade.
  std::array<std::vector<float>, kMaxChannels> mDryDelay{};
  std::size_t mDryPos = 0;

  double mSwitchMix = 1.0; // 1 = pedal engaged
  double mBypassMix = 1.0; // 1 = host-bypassed; starts bypassed and ramps in
  double mSwitchStep = 1.0;
  double mBypassStep = 1.0;
};

} // namespace bbm
