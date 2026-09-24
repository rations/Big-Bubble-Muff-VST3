// BigBubbleMuff — bigbubblemuff.so, the LV2 DSP half.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// A HOST, not a second plug-in: it instantiates the same bbm::Processor a DAW does
// and drives it through IAudioProcessor, after the owner's rations-amp
// products/rations/lv2/rations_lv2.cpp. See bbmlv2.h.
//
// WHAT RUNS WHERE:
//   instantiate()     the host's non-RT context. Every VST3 process structure is
//                     built here, and the parameter queues are sized AND pre-touched,
//                     so run() allocates nothing.
//   run()             the audio thread. Control ports that moved become parameter
//                     points; then one process() call; then the latency port.
//   save()/restore()  the host's non-RT context, where getState/setState belong.
//
// Draws nothing and links neither X11 nor cairo, so a headless host can run it.
#include "lv2/bbmlv2.h"

#include "dsp/Checked.h"
#include "plugin/processor.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/options/options.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

using namespace Steinberg;

namespace bbm::lv2 {
namespace {

constexpr int32 kDefaultMaxBlock = 4096;

class Plugin {
public:
  Plugin() = default;
  ~Plugin();
  Plugin(const Plugin &) = delete;
  Plugin &operator=(const Plugin &) = delete;
  Plugin(Plugin &&) = delete;
  Plugin &operator=(Plugin &&) = delete;

  bool instantiate(double rate, const LV2_Feature *const *features);
  void connectPort(std::uint32_t port, void *data);
  void activate();
  void deactivate();
  void run(std::uint32_t frames);

  LV2_State_Status save(LV2_State_Store_Function store, LV2_State_Handle handle);
  LV2_State_Status restore(LV2_State_Retrieve_Function retrieve, LV2_State_Handle handle);

private:
  void pushPoint(Vst::ParamID id, double norm);

  LV2_URID mStateBlob = 0;
  LV2_URID mAtomChunk = 0;

  IPtr<Processor> mProcessor;
  bool mActive = false;

  const float *mIn = nullptr;
  std::array<float *, 2> mOut{};
  std::array<const float *, kParamCount> mControl{};
  const float *mEnabled = nullptr;
  float *mLatency = nullptr;

  // The last value seen on each input port, and whether one has been seen yet: the
  // first block pushes every port, which is how a host's restored values arrive.
  std::array<std::uint32_t, kParamCount + 1> mLast{};
  std::array<bool, kParamCount + 1> mSeen{};

  // VST3 process plumbing, built once.
  Vst::ProcessData mData{};
  Vst::AudioBusBuffers mInBus{};
  Vst::AudioBusBuffers mOutBus{};
  std::array<float *, 1> mInPtrs{};
  std::array<float *, 2> mOutPtrs{};
  Vst::ParameterChanges mChanges{kParamCount + 1};
};

Plugin::~Plugin() {
  if (!mProcessor)
    return;
  if (mActive) {
    mProcessor->setProcessing(false);
    mProcessor->setActive(false);
  }
  mProcessor->terminate();
}

bool Plugin::instantiate(double rate, const LV2_Feature *const *features) {
  if (!(rate > 0.0) || !std::isfinite(rate))
    return false;

  const LV2_URID_Map *map = nullptr;
  const LV2_Options_Option *options = nullptr;
  for (int i = 0; features != nullptr && features[i] != nullptr; ++i) {
    const std::string_view uri =
        features[i]->URI != nullptr ? features[i]->URI : std::string_view();
    if (uri == LV2_URID__map)
      map = static_cast<const LV2_URID_Map *>(features[i]->data);
    else if (uri == LV2_OPTIONS__options)
      options = static_cast<const LV2_Options_Option *>(features[i]->data);
  }
  if (map == nullptr || map->map == nullptr)
    return false; // urid:map is a required feature (the state keys need it)
  mStateBlob = map->map(map->handle, kStateBlobUri);
  mAtomChunk = map->map(map->handle, "http://lv2plug.in/ns/ext/atom#Chunk");

  // The largest block the host will ask for, when it says. The processor walks
  // anything larger in chunks of this size, so the figure only decides how much is
  // allocated once.
  int32 maxBlock = kDefaultMaxBlock;
  if (options != nullptr) {
    const LV2_URID wanted = map->map(map->handle, LV2_BUF_SIZE__maxBlockLength);
    const LV2_URID atomInt = map->map(map->handle, "http://lv2plug.in/ns/ext/atom#Int");
    for (const LV2_Options_Option *o = options; o->key != 0; ++o)
      if (o->key == wanted && o->type == atomInt && o->value != nullptr &&
          o->size == sizeof(std::int32_t))
        std::memcpy(&maxBlock, o->value, sizeof maxBlock);
  }
  maxBlock = std::clamp<int32>(maxBlock, 16, 1 << 16);

  // The SDK's reference count owns it from here (owned() adopts the first ref).
  mProcessor =
      owned(new (std::nothrow) Processor()); // NOLINT(cppcoreguidelines-owning-memory)
  if (!mProcessor || mProcessor->initialize(nullptr) != kResultOk)
    return false;
  Vst::SpeakerArrangement in = Vst::SpeakerArr::kMono;
  Vst::SpeakerArrangement out = Vst::SpeakerArr::kStereo;
  if (mProcessor->setBusArrangements(&in, 1, &out, 1) != kResultOk)
    return false;
  mProcessor->activateBus(Vst::kAudio, Vst::kInput, 0, true);
  mProcessor->activateBus(Vst::kAudio, Vst::kOutput, 0, true);

  Vst::ProcessSetup setup{};
  setup.processMode = Vst::kRealtime;
  setup.symbolicSampleSize = Vst::kSample32;
  setup.maxSamplesPerBlock = maxBlock;
  setup.sampleRate = rate;
  if (mProcessor->setupProcessing(setup) != kResultOk)
    return false;

  mInBus.numChannels = 1;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  mInBus.channelBuffers32 = mInPtrs.data();
  mOutBus.numChannels = 2;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  mOutBus.channelBuffers32 = mOutPtrs.data();
  mData.numInputs = 1;
  mData.numOutputs = 1;
  mData.inputs = &mInBus;
  mData.outputs = &mOutBus;
  mData.symbolicSampleSize = Vst::kSample32;
  mData.processMode = Vst::kRealtime;
  mData.inputParameterChanges = &mChanges;

  // Pre-touch every queue run() can write, so none is created on the audio thread.
  for (int i = 0; i < kParamCount; ++i)
    pushPoint(static_cast<Vst::ParamID>(i), 0.0);
  pushPoint(kBypassId, 0.0);
  mChanges.clearQueue();
  return true;
}

void Plugin::connectPort(std::uint32_t port, void *data) {
  if (port == kPortAudioIn)
    mIn = static_cast<const float *>(data);
  else if (port == kPortAudioOutL)
    mOut[0] = static_cast<float *>(data);
  else if (port == kPortAudioOutR)
    mOut[1] = static_cast<float *>(data);
  else if (isControlPort(port))
    mControl.at(controlParam(port)) = static_cast<const float *>(data);
  else if (port == kPortEnabled)
    mEnabled = static_cast<const float *>(data);
  else if (port == kPortLatency)
    mLatency = static_cast<float *>(data);
}

void Plugin::activate() {
  if (!mProcessor || mActive)
    return;
  mProcessor->setActive(true);
  mProcessor->setProcessing(true);
  mActive = true;
}

void Plugin::deactivate() {
  if (!mProcessor || !mActive)
    return;
  mProcessor->setProcessing(false);
  mProcessor->setActive(false);
  mActive = false;
}

// One parameter point at offset 0. addPoint REPLACES a point at the same offset
// (parameterchanges.cpp), and the processor reads only the last point anyway.
void Plugin::pushPoint(Vst::ParamID id, double norm) {
  int32 index = 0;
  if (Vst::IParamValueQueue *queue = mChanges.addParameterData(id, index))
    queue->addPoint(0, norm, index);
}

void Plugin::run(std::uint32_t frames) {
  if (!mProcessor || mIn == nullptr || mOut[0] == nullptr || frames == 0)
    return;

  mChanges.clearQueue();
  // A port counts as moved when its BITS change, so a NaN parked on a port is
  // pushed once rather than seen as "moved" on every block.
  const auto moved = [this](std::size_t i, float v) {
    const auto bits = std::bit_cast<std::uint32_t>(v);
    if (at(mSeen, i) && at(mLast, i) == bits)
      return false;
    at(mLast, i) = bits;
    at(mSeen, i) = true;
    return true;
  };
  for (std::size_t i = 0; i < mControl.size(); ++i) {
    const float *port = at(mControl, i);
    if (port != nullptr && moved(i, *port))
      pushPoint(static_cast<Vst::ParamID>(i),
                toNorm(at(kParams, i), static_cast<double>(*port)));
  }
  if (mEnabled != nullptr) {
    const float v = *mEnabled;
    if (moved(kParamCount, v)) {
      pushPoint(kBypassId, bypassFromEnabled(v));
    }
  }

  // The input buffer may alias an output (LV2 permits it unless the plug-in says
  // lv2:inPlaceBroken); the processor copies its dry signal first, so that is safe.
  mInPtrs[0] = const_cast<float *>(mIn); // NOLINT(cppcoreguidelines-pro-type-const-cast)
  mOutPtrs[0] = mOut[0];
  mOutPtrs[1] = mOut[1];
  mOutBus.numChannels = mOut[1] != nullptr ? 2 : 1;
  mData.numSamples = static_cast<int32>(std::min<std::uint32_t>(frames, 1U << 30));
  mProcessor->process(mData);

  if (mLatency != nullptr)
    *mLatency = static_cast<float>(mProcessor->getLatencySamples());
}

LV2_State_Status Plugin::save(LV2_State_Store_Function store, LV2_State_Handle handle) {
  if (!mProcessor || store == nullptr)
    return LV2_STATE_ERR_UNKNOWN;
  MemoryStream stream;
  if (mProcessor->getState(&stream) != kResultOk)
    return LV2_STATE_ERR_UNKNOWN;
  return store(handle, mStateBlob, stream.getData(),
               static_cast<size_t>(stream.getSize()), mAtomChunk,
               LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
}

LV2_State_Status Plugin::restore(LV2_State_Retrieve_Function retrieve,
                                 LV2_State_Handle handle) {
  if (!mProcessor || retrieve == nullptr)
    return LV2_STATE_ERR_UNKNOWN;
  size_t size = 0;
  std::uint32_t type = 0;
  std::uint32_t flags = 0;
  const void *blob = retrieve(handle, mStateBlob, &size, &type, &flags);
  if (blob == nullptr)
    return LV2_STATE_ERR_NO_PROPERTY;
  if (type != mAtomChunk || size == 0 || size > (1U << 20))
    return LV2_STATE_ERR_BAD_TYPE;
  // Untrusted project data, through the processor's own validating reader. The
  // stream only reads from the buffer; its interface is not const.
  MemoryStream stream(
      const_cast<void *>(blob), // NOLINT(cppcoreguidelines-pro-type-const-cast)
      static_cast<TSize>(size));
  return mProcessor->setState(&stream) == kResultOk ? LV2_STATE_SUCCESS
                                                    : LV2_STATE_ERR_UNKNOWN;
}

//------------------------------------------------------------------------
// The LV2 entry points.
Plugin *self(LV2_Handle h) {
  return static_cast<Plugin *>(h);
}

LV2_Handle lv2Instantiate(const LV2_Descriptor * /*d*/, double rate,
                          const char * /*bundlePath*/,
                          const LV2_Feature *const *features) {
  auto plugin = std::unique_ptr<Plugin>(new (std::nothrow) Plugin());
  if (!plugin || !plugin->instantiate(rate, features))
    return nullptr;
  return plugin.release(); // owned by the host until lv2Cleanup
}

void lv2ConnectPort(LV2_Handle h, std::uint32_t port, void *data) {
  if (Plugin *p = self(h))
    p->connectPort(port, data);
}

void lv2Activate(LV2_Handle h) {
  if (Plugin *p = self(h))
    p->activate();
}

void lv2Run(LV2_Handle h, std::uint32_t frames) {
  if (Plugin *p = self(h))
    p->run(frames);
}

void lv2Deactivate(LV2_Handle h) {
  if (Plugin *p = self(h))
    p->deactivate();
}

void lv2Cleanup(LV2_Handle h) {
  const std::unique_ptr<Plugin> owner(self(h));
}

LV2_State_Status lv2Save(LV2_Handle h, LV2_State_Store_Function store,
                         LV2_State_Handle handle, std::uint32_t /*flags*/,
                         const LV2_Feature *const * /*features*/) {
  Plugin *p = self(h);
  return p != nullptr ? p->save(store, handle) : LV2_STATE_ERR_UNKNOWN;
}

LV2_State_Status lv2Restore(LV2_Handle h, LV2_State_Retrieve_Function retrieve,
                            LV2_State_Handle handle, std::uint32_t /*flags*/,
                            const LV2_Feature *const * /*features*/) {
  Plugin *p = self(h);
  return p != nullptr ? p->restore(retrieve, handle) : LV2_STATE_ERR_UNKNOWN;
}

const LV2_State_Interface kStateInterface = {lv2Save, lv2Restore};

const void *lv2ExtensionData(const char *uri) {
  if (uri != nullptr && std::string_view(uri) == LV2_STATE__interface)
    return &kStateInterface;
  return nullptr;
}

const LV2_Descriptor kDescriptor = {
    kPluginUri, lv2Instantiate, lv2ConnectPort, lv2Activate,
    lv2Run,     lv2Deactivate,  lv2Cleanup,     lv2ExtensionData,
};

} // namespace
} // namespace bbm::lv2

extern "C" {
LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(std::uint32_t index) {
  return index == 0 ? &bbm::lv2::kDescriptor : nullptr;
}
}
