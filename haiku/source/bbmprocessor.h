// BigBubbleMuff (Haiku) — audio processor.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Wraps the JUCE-free circuit engine (dsp/BigMuffPi.h) in a raw VST3
// IAudioProcessor. The pedal is mono: whatever comes in is downmixed, solved
// once, and fanned back out to every output channel.
//
// Real-time contract: process() never allocates, locks, logs or touches the
// Interface Kit. Every buffer is sized in setupProcessing, and FTZ/DAZ are
// re-armed on each call because JACK does not set them on client threads.
#pragma once

#include "bbmids.h"
#include "dsp/BigMuffPi.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <atomic>

namespace bbmh {

//------------------------------------------------------------------------
class BigMuffProcessor : public Steinberg::Vst::AudioEffect {
public:
  BigMuffProcessor();
  ~BigMuffProcessor() override;

  static Steinberg::FUnknown *createInstance(void *) {
    return static_cast<Steinberg::Vst::IAudioProcessor *>(new BigMuffProcessor());
  }

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API
  setBusArrangements(Steinberg::Vst::SpeakerArrangement *inputs, Steinberg::int32 numIns,
                     Steinberg::Vst::SpeakerArrangement *outputs,
                     Steinberg::int32 numOuts) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize)
      SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup &setup)
      SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData &data) SMTG_OVERRIDE;

  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream *state) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream *state) SMTG_OVERRIDE;

  Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE;

private:
  void handleParameterChanges(Steinberg::Vst::IParameterChanges *changes);
  void pushControls();

  // Normalised parameter values. Written both by RT parameter handling and by
  // setState on the message thread, so the accesses are atomic.
  std::atomic<double> mSustain{ranges::kSustainDefault};
  std::atomic<double> mTone{ranges::kToneDefault};
  std::atomic<double> mVolume{ranges::kVolumeDefault};
  std::atomic<double> mOutput{0.5}; // normalised; plain 0 dB
  std::atomic<double> mGate{ranges::kGateDefault};
  std::atomic<double> mBypass{0.0};

  BigMuffPi mEngine;
  std::atomic<Steinberg::uint32> mLatency{0};

  double mSampleRate = 48000.0;
  Steinberg::int32 mMaxBlockSize = 2048;
};

} // namespace bbmh
