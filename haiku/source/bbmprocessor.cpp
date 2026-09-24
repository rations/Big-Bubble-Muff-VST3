// BigBubbleMuff (Haiku) — audio processor implementation.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// See bbmprocessor.h for the threading and real-time contract.
#include "bbmprocessor.h"
#include "bbmids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cstring>

// Flush-to-zero / denormals-are-zero, re-armed on every process() call: JACK
// does not set FTZ/DAZ on client process threads, and subnormals in the
// circuit's feedback and filter state would stall the CPU and blow the RT
// deadline.
#if defined(__SSE__) || defined(__x86_64__)
#include <pmmintrin.h>
#include <xmmintrin.h>
#define BBM_HAVE_SSE_DENORMAL 1
#endif

namespace {

inline void bbm_set_denormal_mode() {
#ifdef BBM_HAVE_SSE_DENORMAL
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}

inline double denorm(double norm, double min, double max) {
  return min + norm * (max - min);
}

inline double clamp01(double v) {
  return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

} // namespace

using namespace Steinberg;

namespace bbmh {

//------------------------------------------------------------------------
BigMuffProcessor::BigMuffProcessor() {
  setControllerClass(FUID::fromTUID(BigMuffControllerUID));
}

BigMuffProcessor::~BigMuffProcessor() = default;

//------------------------------------------------------------------------
tresult PLUGIN_API BigMuffProcessor::initialize(FUnknown *context) {
  const tresult result = AudioEffect::initialize(context);
  if (result != kResultOk)
    return result;

  addAudioInput(STR16("Input"), Vst::SpeakerArr::kStereo);
  addAudioOutput(STR16("Output"), Vst::SpeakerArr::kStereo);
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API BigMuffProcessor::setBusArrangements(Vst::SpeakerArrangement *inputs,
                                                        int32 numIns,
                                                        Vst::SpeakerArrangement *outputs,
                                                        int32 numOuts) {
  // Mono or stereo, and the two sides must match — the same rule the Linux
  // build applies. The circuit itself is mono either way.
  if (numIns != 1 || numOuts != 1)
    return kResultFalse;
  if (inputs[0] != outputs[0])
    return kResultFalse;
  if (inputs[0] != Vst::SpeakerArr::kMono && inputs[0] != Vst::SpeakerArr::kStereo)
    return kResultFalse;
  return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

//------------------------------------------------------------------------
tresult PLUGIN_API BigMuffProcessor::canProcessSampleSize(int32 symbolicSampleSize) {
  return symbolicSampleSize == Vst::kSample32 ? kResultTrue : kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API BigMuffProcessor::setupProcessing(Vst::ProcessSetup &setup) {
  const tresult result = AudioEffect::setupProcessing(setup);
  if (result != kResultOk)
    return result;

  mSampleRate = setup.sampleRate;
  mMaxBlockSize = std::max<int32>(1, setup.maxSamplesPerBlock);

  ProcessSpec spec;
  spec.sampleRate = mSampleRate;
  spec.maximumBlockSize = static_cast<std::uint32_t>(mMaxBlockSize);
  spec.numChannels = 2;

  // Allocation and the expensive turn-on settle happen here, on the message
  // thread, never in process().
  mEngine.prepare(spec);
  pushControls();
  mLatency.store(static_cast<uint32>(mEngine.getOversamplingLatencySamples()),
                 std::memory_order_relaxed);
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API BigMuffProcessor::setActive(TBool state) {
  if (state)
    mEngine.reset();
  return AudioEffect::setActive(state);
}

//------------------------------------------------------------------------
uint32 PLUGIN_API BigMuffProcessor::getLatencySamples() {
  return mLatency.load(std::memory_order_relaxed);
}

//------------------------------------------------------------------------
void BigMuffProcessor::handleParameterChanges(Vst::IParameterChanges *changes) {
  if (!changes)
    return;
  const int32 numParams = changes->getParameterCount();
  for (int32 i = 0; i < numParams; ++i) {
    Vst::IParamValueQueue *queue = changes->getParameterData(i);
    if (!queue)
      continue;
    Vst::ParamValue value = 0.0;
    int32 sampleOffset = 0;
    const int32 numPoints = queue->getPointCount();
    if (numPoints < 1 ||
        queue->getPoint(numPoints - 1, sampleOffset, value) != kResultTrue)
      continue;
    switch (queue->getParameterId()) {
    case kSustainId:
      mSustain.store(value, std::memory_order_relaxed);
      break;
    case kToneId:
      mTone.store(value, std::memory_order_relaxed);
      break;
    case kVolumeId:
      mVolume.store(value, std::memory_order_relaxed);
      break;
    case kOutputId:
      mOutput.store(value, std::memory_order_relaxed);
      break;
    case kGateId:
      mGate.store(value, std::memory_order_relaxed);
      break;
    case kBypassId:
      mBypass.store(value, std::memory_order_relaxed);
      break;
    default:
      break;
    }
  }
}

//------------------------------------------------------------------------
void BigMuffProcessor::pushControls() {
  Controls c;
  c.sustain = static_cast<float>(clamp01(mSustain.load(std::memory_order_relaxed)));
  c.tone = static_cast<float>(clamp01(mTone.load(std::memory_order_relaxed)));
  c.volume = static_cast<float>(clamp01(mVolume.load(std::memory_order_relaxed)));
  c.outputTrimDb =
      static_cast<float>(denorm(clamp01(mOutput.load(std::memory_order_relaxed)),
                                ranges::kOutputMin, ranges::kOutputMax));
  c.gate = static_cast<float>(clamp01(mGate.load(std::memory_order_relaxed)));
  mEngine.setControls(c);
}

//------------------------------------------------------------------------
tresult PLUGIN_API BigMuffProcessor::process(Vst::ProcessData &data) {
  bbm_set_denormal_mode();

  handleParameterChanges(data.inputParameterChanges);

  if (data.numSamples <= 0)
    return kResultOk;
  if (data.numInputs < 1 || data.numOutputs < 1)
    return kResultOk;
  float *const *in = data.inputs[0].channelBuffers32;
  float *const *out = data.outputs[0].channelBuffers32;
  if (!in || !out)
    return kResultOk;

  const int32 numSamples = data.numSamples;
  const int32 numOutCh = data.outputs[0].numChannels;
  const int32 numCh = std::min(data.inputs[0].numChannels, numOutCh);
  if (numCh < 1)
    return kResultOk;

  if (mBypass.load(std::memory_order_relaxed) >= 0.5) {
    // Unlike the Linux build (which relies on JUCE handing it one buffer for
    // both directions), a VST3 host may pass out-of-place buffers, so bypass
    // has to copy rather than simply return.
    for (int32 ch = 0; ch < numCh; ++ch)
      if (out[ch] && in[ch] && out[ch] != in[ch])
        std::memcpy(out[ch], in[ch], static_cast<size_t>(numSamples) * sizeof(float));
  } else {
    pushControls();
    mEngine.process(in, out, static_cast<std::size_t>(numCh),
                    static_cast<std::size_t>(numSamples));
  }

  // Any output channel beyond the processed set gets the same mono result.
  for (int32 ch = numCh; ch < numOutCh; ++ch)
    if (out[ch] && out[0] && out[ch] != out[0])
      std::memcpy(out[ch], out[0], static_cast<size_t>(numSamples) * sizeof(float));

  data.outputs[0].silenceFlags = 0;
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API BigMuffProcessor::setState(IBStream *state) {
  // Untrusted input: a project file authored by anyone. Check the version,
  // check every read, and clamp every value before it reaches the engine.
  if (!state)
    return kResultFalse;
  IBStreamer streamer(state, kLittleEndian);

  int32 version = 0;
  if (!streamer.readInt32(version) || version < 1 || version > kStateVersion)
    return kResultFalse;

  double values[6] = {0.0};
  for (double &v : values)
    if (!streamer.readDouble(v))
      return kResultFalse;

  mSustain.store(clamp01(values[0]), std::memory_order_relaxed);
  mTone.store(clamp01(values[1]), std::memory_order_relaxed);
  mVolume.store(clamp01(values[2]), std::memory_order_relaxed);
  mOutput.store(clamp01(values[3]), std::memory_order_relaxed);
  mGate.store(clamp01(values[4]), std::memory_order_relaxed);
  mBypass.store(clamp01(values[5]) >= 0.5 ? 1.0 : 0.0, std::memory_order_relaxed);
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API BigMuffProcessor::getState(IBStream *state) {
  if (!state)
    return kResultFalse;
  IBStreamer streamer(state, kLittleEndian);

  streamer.writeInt32(kStateVersion);
  streamer.writeDouble(mSustain.load(std::memory_order_relaxed));
  streamer.writeDouble(mTone.load(std::memory_order_relaxed));
  streamer.writeDouble(mVolume.load(std::memory_order_relaxed));
  streamer.writeDouble(mOutput.load(std::memory_order_relaxed));
  streamer.writeDouble(mGate.load(std::memory_order_relaxed));
  streamer.writeDouble(mBypass.load(std::memory_order_relaxed));
  return kResultOk;
}

} // namespace bbmh
