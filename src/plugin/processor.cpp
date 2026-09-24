// BigBubbleMuff — audio processor implementation (see processor.h).
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#include "plugin/processor.h"
#include "dsp/Checked.h"

#include "plugin/denormal.h"
#include "plugin/state.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Steinberg;

namespace bbm {

namespace {
constexpr double kSwitchRampMs = 8.0; // footswitch crossfade
constexpr double kBypassRampMs = 5.0; // host bypass crossfade

inline float finiteOrZero(float x) {
  return std::isfinite(x) ? x : 0.0f;
}

inline double stepToward(double v, double target, double step) {
  if (v < target)
    return std::min(target, v + step);
  if (v > target)
    return std::max(target, v - step);
  return v;
}
} // namespace

Processor::Processor() {
  setControllerClass(FUID::fromTUID(kControllerUID));
  const StateValues d = defaultState();
  for (int i = 0; i < kParamCount; ++i)
    at(mNorm, i).store(at(d.norm, i), std::memory_order_relaxed);
}

Processor::~Processor() = default;

uint32 Processor::latency() {
  return static_cast<uint32>(std::lround(BigMuffPi::latencySamples()));
}

tresult PLUGIN_API Processor::initialize(FUnknown *context) {
  const tresult result = AudioEffect::initialize(context);
  if (result != kResultOk)
    return result;
  addAudioInput(STR16("Input"), Vst::SpeakerArr::kStereo);
  addAudioOutput(STR16("Output"), Vst::SpeakerArr::kStereo);
  return kResultOk;
}

// Accepted: mono->mono, mono->stereo, stereo->stereo. Stereo->mono is refused: a
// downmix nobody asked for is a worse answer than "this does not fit here".
tresult PLUGIN_API Processor::setBusArrangements(Vst::SpeakerArrangement *inputs,
                                                 int32 numIns,
                                                 Vst::SpeakerArrangement *outputs,
                                                 int32 numOuts) {
  if (numIns != 1 || numOuts != 1 || inputs == nullptr || outputs == nullptr)
    return kResultFalse;
  const bool inMono = inputs[0] == Vst::SpeakerArr::kMono;
  const bool inStereo = inputs[0] == Vst::SpeakerArr::kStereo;
  const bool outMono = outputs[0] == Vst::SpeakerArr::kMono;
  const bool outStereo = outputs[0] == Vst::SpeakerArr::kStereo;
  if (!(inMono || inStereo) || !(outMono || outStereo) || (inStereo && outMono))
    return kResultFalse;
  return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize) {
  return symbolicSampleSize == Vst::kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::setupProcessing(Vst::ProcessSetup &setup) {
  const tresult result = AudioEffect::setupProcessing(setup);
  if (result != kResultOk)
    return result;

  mSampleRate = setup.sampleRate > 0.0 ? setup.sampleRate : 48000.0;
  mMaxBlock = std::max<int32>(1, setup.maxSamplesPerBlock);
  const auto n = static_cast<std::size_t>(mMaxBlock);

  // Every allocation the audio path ever uses happens here (message thread).
  mMono.assign(n, 0.0f);
  for (auto &d : mDry)
    d.assign(n, 0.0f);
  for (auto &d : mDryDelay)
    d.assign(std::max<std::size_t>(1, latency()), 0.0f);
  mDryPos = 0;
  mSwitchStep = 1.0 / std::max(1.0, kSwitchRampMs * 0.001 * mSampleRate);
  mBypassStep = 1.0 / std::max(1.0, kBypassRampMs * 0.001 * mSampleRate);

  // Allocates and settles the circuit (expensive; never on the audio thread).
  mEngine.prepare(mSampleRate, mMaxBlock);
  pushControls();
  return kResultOk;
}

tresult PLUGIN_API Processor::setActive(TBool state) {
  if (state) {
    // Come up host-bypassed and ramp in, so activation never opens mid-signal.
    // The engine itself is NOT reset here: setupProcessing has just settled it,
    // and resetting would reintroduce the turn-on transient that settle removed.
    mBypassMix = 1.0;
    mSwitchMix = mNorm[kSwitchId].load(std::memory_order_relaxed) >= 0.5 ? 1.0 : 0.0;
    for (auto &d : mDryDelay)
      std::fill(d.begin(), d.end(), 0.0f);
    mDryPos = 0;
  }
  return AudioEffect::setActive(state);
}

uint32 PLUGIN_API Processor::getLatencySamples() {
  return latency();
}

uint32 PLUGIN_API Processor::getTailSamples() {
  // A fuzz has no reverb tail worth keeping alive; the coupling-network decay is
  // well under 100 ms.
  return static_cast<uint32>(std::lround(0.1 * mSampleRate));
}

void Processor::handleParameterChanges(Vst::IParameterChanges *changes) {
  if (changes == nullptr)
    return;
  const int32 count = changes->getParameterCount();
  for (int32 i = 0; i < count; ++i) {
    Vst::IParamValueQueue *queue = changes->getParameterData(i);
    if (queue == nullptr)
      continue;
    const int32 points = queue->getPointCount();
    int32 offset = 0;
    Vst::ParamValue value = 0.0;
    // The last point of the block is the value the block runs at: the engine
    // smooths every control itself, so sample-accurate automation buys nothing.
    if (points < 1 || queue->getPoint(points - 1, offset, value) != kResultTrue)
      continue;
    const Vst::ParamID id = queue->getParameterId();
    if (id == kBypassId)
      mHostBypass.store(clampNorm(value), std::memory_order_relaxed);
    else if (id < static_cast<Vst::ParamID>(kParamCount))
      at(mNorm, id).store(clampNorm(value), std::memory_order_relaxed);
  }
}

void Processor::pushControls() {
  const auto plain = [this](ParamId id) {
    return static_cast<float>(
        toPlain(at(kParams, id), at(mNorm, id).load(std::memory_order_relaxed)));
  };
  Controls c;
  c.sustain = plain(kSustainId);
  c.tone = plain(kToneId);
  c.volume = plain(kVolumeId);
  c.outputTrimDb = plain(kOutputId);
  c.gate = plain(kGateId);
  mEngine.setControls(c);
}

tresult PLUGIN_API Processor::process(Vst::ProcessData &data) {
  setDenormalMode();
  handleParameterChanges(data.inputParameterChanges);

  if (data.numSamples <= 0 || data.numInputs < 1 || data.numOutputs < 1)
    return kResultOk;
  // AudioBusBuffers is an SDK union of 32- and 64-bit pointers; canProcessSampleSize
  // admits only kSample32, so the 32-bit member is the active one.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  float *const *in = data.inputs[0].channelBuffers32;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  float *const *out = data.outputs[0].channelBuffers32;
  const int32 inCh = data.inputs[0].numChannels;
  const int32 outCh = data.outputs[0].numChannels;
  if (in == nullptr || out == nullptr || inCh < 1 || outCh < 1 || mMaxBlock < 1)
    return kResultOk;
  for (int32 c = 0; c < std::min(inCh, kMaxChannels); ++c)
    if (in[c] == nullptr)
      return kResultOk;
  for (int32 c = 0; c < outCh; ++c)
    if (out[c] == nullptr)
      return kResultOk;

  pushControls();

  // A host must respect maxSamplesPerBlock; a stray oversized block is walked in
  // prepared-size chunks rather than overrunning the scratch buffers.
  for (int32 done = 0; done < data.numSamples;) {
    const int32 n = std::min(data.numSamples - done, mMaxBlock);
    processChunk(in, out, inCh, outCh, done, n);
    done += n;
  }

  // Channels beyond the two this plug-in negotiates are silenced, not left stale.
  for (int32 c = kMaxChannels; c < outCh; ++c)
    std::memset(out[c], 0, static_cast<std::size_t>(data.numSamples) * sizeof(float));
  data.outputs[0].silenceFlags = 0;
  return kResultOk;
}

void Processor::processChunk(float *const *in, float *const *out, int32 inCh, int32 outCh,
                             int32 offset, int32 n) noexcept {
  const auto count = static_cast<std::size_t>(n);
  const auto off = static_cast<std::size_t>(offset);
  const int32 usedIn = std::min(inCh, kMaxChannels);
  const int32 usedOut = std::min(outCh, kMaxChannels);

  // Dry copies first: the host may hand the same buffer in and out. A mono input
  // feeds both dry channels. The mono average is what the circuit sees; averaging
  // (not summing) keeps a duplicated-mono stereo feed identical to a true mono one.
  const float invCh = 1.0f / static_cast<float>(usedIn);
  for (std::size_t k = 0; k < count; ++k) {
    float acc = 0.0f;
    for (int32 c = 0; c < kMaxChannels; ++c) {
      const float x = finiteOrZero(in[std::min(c, usedIn - 1)][off + k]);
      at(mDry, c)[k] = x;
      if (c < usedIn)
        acc += x;
    }
    mMono[k] = acc * invCh;
  }

  const double switchTarget =
      mNorm[kSwitchId].load(std::memory_order_relaxed) >= 0.5 ? 1.0 : 0.0;
  const double bypassTarget =
      mHostBypass.load(std::memory_order_relaxed) >= 0.5 ? 1.0 : 0.0;

  // Skip the circuit entirely once the footswitch has faded all the way out.
  const bool engineIdle = switchTarget <= 0.0 && mSwitchMix <= 0.0;
  if (!engineIdle)
    mEngine.process(mMono.data(), mMono.data(), count);

  const std::size_t delayLen = mDryDelay[0].size();
  const bool delayDry = latency() > 0;
  for (std::size_t k = 0; k < count; ++k) {
    mSwitchMix = stepToward(mSwitchMix, switchTarget, mSwitchStep);
    mBypassMix = stepToward(mBypassMix, bypassTarget, mBypassStep);
    const auto wetGain = static_cast<float>(mSwitchMix * (1.0 - mBypassMix));
    const float wet = engineIdle ? 0.0f : mMono[k];
    for (int32 c = 0; c < usedOut; ++c) {
      const auto ch = static_cast<std::size_t>(c);
      float dry = at(mDry, ch)[k];
      if (delayDry) {
        std::swap(dry, at(mDryDelay, ch)[mDryPos]);
      }
      out[c][off + k] = wetGain * wet + (1.0f - wetGain) * dry;
    }
    if (delayDry && usedOut < kMaxChannels)
      mDryDelay[1][mDryPos] = mDry[1][k]; // keep the unused line in step
    if (delayDry)
      mDryPos = (mDryPos + 1 == delayLen) ? 0 : mDryPos + 1;
  }
}

tresult PLUGIN_API Processor::setState(IBStream *state) {
  StateValues v;
  if (!readState(state, v))
    return kResultFalse;
  for (int i = 0; i < kParamCount; ++i)
    at(mNorm, i).store(at(v.norm, i), std::memory_order_relaxed);
  mHostBypass.store(v.hostBypass, std::memory_order_relaxed);
  return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream *state) {
  StateValues v;
  for (int i = 0; i < kParamCount; ++i)
    at(v.norm, i) = at(mNorm, i).load(std::memory_order_relaxed);
  v.hostBypass = mHostBypass.load(std::memory_order_relaxed);
  return writeState(state, v) ? kResultOk : kResultFalse;
}

} // namespace bbm
