// BigBubbleMuff — plug-in layer tests: state validation, processor behaviour.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#include "test.h"

#include "plugin/controller.h"
#include "plugin/ids.h"
#include "plugin/processor.h"
#include "plugin/state.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <vector>

using namespace Steinberg;

namespace {

// A fresh MemoryStream holding `bytes`, rewound for reading.
IPtr<MemoryStream> streamOf(const std::vector<std::uint8_t> &bytes) {
  auto s = owned(new MemoryStream());
  int32 written = 0;
  if (!bytes.empty())
    s->write(const_cast<std::uint8_t *>(bytes.data()), static_cast<int32>(bytes.size()),
             &written);
  s->seek(0, IBStream::kIBSeekSet, nullptr);
  return s;
}

std::vector<std::uint8_t> bytesOf(MemoryStream &s) {
  const auto n = static_cast<std::size_t>(s.getSize());
  std::vector<std::uint8_t> out(n);
  std::memcpy(out.data(), s.getData(), n);
  return out;
}

// Builds a blob by hand: version, count, values..., bypass.
std::vector<std::uint8_t> blob(int32 version, int32 count,
                               const std::vector<double> &vals, double bypass,
                               bool includeBypass = true) {
  auto s = owned(new MemoryStream());
  IBStreamer w(s, kLittleEndian);
  w.writeInt32(version);
  w.writeInt32(count);
  for (double v : vals)
    w.writeDouble(v);
  if (includeBypass)
    w.writeDouble(bypass);
  return bytesOf(*s);
}

bool read(const std::vector<std::uint8_t> &bytes, bbm::StateValues &out) {
  auto s = streamOf(bytes);
  return bbm::readState(s, out);
}

// A minimal offline host around one Processor instance.
struct Rig {
  IPtr<bbm::Processor> proc;
  int32 block;
  int32 inCh, outCh;
  std::vector<std::vector<float>> inBufs, outBufs;
  std::vector<float *> inPtrs, outPtrs;
  Vst::AudioBusBuffers inBus{}, outBus{};
  Vst::ParameterChanges changes{8};

  Rig(double fs, int32 blockSize, int32 in, int32 out)
      : proc(owned(new bbm::Processor())), block(blockSize), inCh(in), outCh(out) {
    proc->initialize(nullptr);
    Vst::SpeakerArrangement ia =
        in == 1 ? Vst::SpeakerArr::kMono : Vst::SpeakerArr::kStereo;
    Vst::SpeakerArrangement oa =
        out == 1 ? Vst::SpeakerArr::kMono : Vst::SpeakerArr::kStereo;
    proc->setBusArrangements(&ia, 1, &oa, 1);
    Vst::ProcessSetup setup{Vst::kRealtime, Vst::kSample32, blockSize, fs};
    proc->setupProcessing(setup);
    proc->setActive(true);
    proc->setProcessing(true);
    inBufs.assign(static_cast<std::size_t>(in),
                  std::vector<float>(static_cast<std::size_t>(blockSize), 0.0f));
    outBufs.assign(static_cast<std::size_t>(out),
                   std::vector<float>(static_cast<std::size_t>(blockSize), 0.0f));
    for (auto &b : inBufs)
      inPtrs.push_back(b.data());
    for (auto &b : outBufs)
      outPtrs.push_back(b.data());
    inBus.numChannels = in;
    inBus.channelBuffers32 = inPtrs.data();
    outBus.numChannels = out;
    outBus.channelBuffers32 = outPtrs.data();
  }
  ~Rig() {
    proc->setProcessing(false);
    proc->setActive(false);
    proc->terminate();
  }
  Rig(const Rig &) = delete;
  Rig &operator=(const Rig &) = delete;

  void set(Vst::ParamID id, double norm) {
    int32 index = 0;
    if (auto *q = changes.addParameterData(id, index)) {
      int32 p = 0;
      q->addPoint(0, norm, p);
    }
  }

  void run() {
    Vst::ProcessData d{};
    d.processMode = Vst::kRealtime;
    d.symbolicSampleSize = Vst::kSample32;
    d.numSamples = block;
    d.numInputs = 1;
    d.numOutputs = 1;
    d.inputs = &inBus;
    d.outputs = &outBus;
    d.inputParameterChanges = &changes;
    proc->process(d);
    changes.clearQueue();
  }
};

// Bit-exact float comparison (-Wfloat-equal forbids ==; bypass must be exact).
bool same(float a, float b) {
  return std::memcmp(&a, &b, sizeof(float)) == 0;
}

void fillSine(std::vector<float> &b, double fs, double f, float a, double &phase) {
  const double inc = 2.0 * std::numbers::pi * f / fs;
  for (float &s : b) {
    s = a * static_cast<float>(std::sin(phase));
    phase += inc;
  }
}

} // namespace

// ---- state ------------------------------------------------------------------

TEST_CASE("State blob", "round-trips every value") {
  bbm::StateValues v = bbm::defaultState();
  for (int i = 0; i < bbm::kParamCount; ++i)
    v.norm[static_cast<std::size_t>(i)] = 0.1 * (i + 1);
  v.hostBypass = 1.0;
  auto s = owned(new MemoryStream());
  CHECK(bbm::writeState(s, v));
  bbm::StateValues r;
  CHECK(read(bytesOf(*s), r));
  for (int i = 0; i < bbm::kParamCount; ++i)
    CHECK(std::abs(r.norm[static_cast<std::size_t>(i)] - 0.1 * (i + 1)) < 1e-15);
  CHECK(r.hostBypass > 0.5);
}

TEST_CASE("State blob", "rejects null, empty, truncated and future-version input") {
  bbm::StateValues r = bbm::defaultState();
  CHECK(!bbm::readState(nullptr, r));
  CHECK(!read({}, r));
  const auto good = blob(1, 2, {0.3, 0.4}, 0.0);
  for (std::size_t cut = 0; cut < good.size(); ++cut) {
    const std::vector<std::uint8_t> part(good.begin(),
                                         good.begin() + static_cast<std::ptrdiff_t>(cut));
    CHECK_MSG(!read(part, r), "accepted a blob truncated to " + std::to_string(cut));
  }
  CHECK(!read(blob(0, 2, {0.3, 0.4}, 0.0), r));
  CHECK(!read(blob(bbm::kStateVersion + 1, 2, {0.3, 0.4}, 0.0), r));
  CHECK(!read(blob(1, -1, {}, 0.0), r));
  CHECK(!read(blob(1, bbm::kMaxStateValues + 1, {}, 0.0), r));
}

TEST_CASE("State blob", "a non-finite value fails the whole read") {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  bbm::StateValues r = bbm::defaultState();
  r.norm[0] = 0.123;
  CHECK(!read(blob(1, 2, {0.3, nan}, 0.0), r));
  CHECK(!read(blob(1, 2, {inf, 0.3}, 0.0), r));
  CHECK(!read(blob(1, 2, {0.3, 0.3}, nan), r));
  CHECK_MSG(std::abs(r.norm[0] - 0.123) < 1e-15, "failed read modified the output");
}

TEST_CASE("State blob",
          "clamps out-of-range values; tolerates shorter and longer blobs") {
  bbm::StateValues r;
  CHECK(read(blob(1, 2, {-5.0, 7.0}, 3.0), r));
  CHECK(r.norm[0] <= 0.0 && r.norm[1] >= 1.0 && r.hostBypass >= 1.0);
  // Shorter: the rest keep their defaults.
  const bbm::StateValues d = bbm::defaultState();
  CHECK(std::abs(r.norm[bbm::kGateId] - d.norm[bbm::kGateId]) < 1e-15);
  // Longer (a newer build): known values kept, extras dropped.
  std::vector<double> many(20, 0.25);
  CHECK(read(blob(1, 20, many, 0.0), r));
  CHECK(std::abs(r.norm[bbm::kSwitchId] - 0.25) < 1e-15);
}

TEST_CASE("State blob", "processor and controller accept and reject the same bytes") {
  const auto good = blob(1, bbm::kParamCount, {0.9, 0.1, 0.8, 0.5, 0.0, 1.0}, 0.0);
  const auto bad = blob(1, 2, {0.3, std::numeric_limits<double>::quiet_NaN()}, 0.0);
  auto proc = owned(new bbm::Processor());
  auto ctrl = owned(new bbm::Controller());
  proc->initialize(nullptr);
  ctrl->initialize(nullptr);
  CHECK(proc->setState(streamOf(good)) == kResultOk);
  CHECK(ctrl->setComponentState(streamOf(good)) == kResultOk);
  CHECK(proc->setState(streamOf(bad)) != kResultOk);
  CHECK(ctrl->setComponentState(streamOf(bad)) != kResultOk);
  CHECK(std::abs(ctrl->getParamNormalized(bbm::kSustainId) - 0.9) < 1e-12);
  // What the processor saves is what it loaded.
  auto out = owned(new MemoryStream());
  CHECK(proc->getState(out) == kResultOk);
  CHECK(bytesOf(*out) == good);
  ctrl->terminate();
  proc->terminate();
}

TEST_CASE("Controller", "registers the table plus a host bypass") {
  auto ctrl = owned(new bbm::Controller());
  ctrl->initialize(nullptr);
  CHECK(ctrl->getParameterCount() == bbm::kParamCount + 1);
  Vst::ParameterInfo info{};
  CHECK(ctrl->getParameterInfo(bbm::kBypassId, info) == kResultOk);
  CHECK((info.flags & Vst::ParameterInfo::kIsBypass) != 0);
  for (const bbm::ParamSpec &p : bbm::kParams) {
    CHECK(ctrl->getParameterInfo(static_cast<int32>(p.id), info) == kResultOk);
    CHECK(info.id == p.id);
    CHECK(std::abs(info.defaultNormalizedValue - bbm::toNorm(p, p.def)) < 1e-12);
  }
  ctrl->terminate();
}

// ---- processor ----------------------------------------------------------------

TEST_CASE("Processor", "bus arrangements: mono/stereo accepted, stereo->mono refused") {
  auto proc = owned(new bbm::Processor());
  proc->initialize(nullptr);
  Vst::SpeakerArrangement m = Vst::SpeakerArr::kMono, s = Vst::SpeakerArr::kStereo,
                          q = Vst::SpeakerArr::k51;
  CHECK(proc->setBusArrangements(&m, 1, &m, 1) == kResultTrue);
  CHECK(proc->setBusArrangements(&m, 1, &s, 1) == kResultTrue);
  CHECK(proc->setBusArrangements(&s, 1, &s, 1) == kResultTrue);
  CHECK(proc->setBusArrangements(&s, 1, &m, 1) != kResultTrue);
  CHECK(proc->setBusArrangements(&q, 1, &q, 1) != kResultTrue);
  CHECK(proc->canProcessSampleSize(Vst::kSample64) != kResultTrue);
  proc->terminate();
}

TEST_CASE("Processor", "host bypass passes the dry signal, delayed by the latency") {
  const double fs = 48000.0;
  Rig rig(fs, 256, 2, 2);
  rig.set(bbm::kBypassId, 1.0);
  const auto L = static_cast<std::size_t>(bbm::Processor::latency());
  CHECK(L == static_cast<std::size_t>(rig.proc->getLatencySamples()));
  double phL = 0.0, phR = 1.0;
  std::vector<float> prevL, prevR;
  bool exact = true;
  for (int b = 0; b < 8; ++b) {
    fillSine(rig.inBufs[0], fs, 440.0, 0.5f, phL);
    fillSine(rig.inBufs[1], fs, 660.0, 0.3f, phR); // different: stereo kept dry
    const auto inL = rig.inBufs[0], inR = rig.inBufs[1];
    rig.run();
    if (b >= 2) { // after the 5 ms ramp from the activation state
      for (std::size_t k = L; k < inL.size(); ++k)
        exact = exact && same(rig.outBufs[0][k], inL[k - L]) &&
                same(rig.outBufs[1][k], inR[k - L]);
      for (std::size_t k = 0; k < L; ++k)
        exact = exact && same(rig.outBufs[0][k], prevL[prevL.size() - L + k]);
    }
    prevL = inL;
    prevR = inR;
  }
  CHECK_MSG(exact, "bypassed output is not the dry input delayed by the latency");
}

TEST_CASE("Processor", "footswitch off passes the dry signal; on processes it") {
  const double fs = 48000.0;
  Rig rig(fs, 512, 1, 2);
  rig.set(bbm::kSwitchId, 0.0);
  double ph = 0.0;
  for (int b = 0; b < 4; ++b) {
    fillSine(rig.inBufs[0], fs, 220.0, 0.3f, ph);
    rig.run();
  }
  double err = 0.0;
  fillSine(rig.inBufs[0], fs, 220.0, 0.3f, ph);
  const auto in = rig.inBufs[0];
  rig.run();
  const auto L = static_cast<std::size_t>(bbm::Processor::latency());
  for (std::size_t k = L; k < in.size(); ++k)
    err = std::max(err, static_cast<double>(std::abs(rig.outBufs[0][k] - in[k - L])));
  CHECK_MSG(err < 1e-7, "footswitch-off output is not dry");
  CHECK_MSG(rig.outBufs[0] == rig.outBufs[1], "mono input must reach both outputs");

  rig.set(bbm::kSwitchId, 1.0);
  for (int b = 0; b < 4; ++b) {
    fillSine(rig.inBufs[0], fs, 220.0, 0.3f, ph);
    rig.run();
  }
  double diff = 0.0;
  for (std::size_t k = L; k < in.size(); ++k)
    diff += std::abs(rig.outBufs[0][k] - rig.inBufs[0][k - L]);
  CHECK_MSG(diff > 1.0, "footswitch-on output is still dry");
}

TEST_CASE("Processor", "non-finite input never reaches the output") {
  Rig rig(48000.0, 128, 2, 2);
  rig.inBufs[0][5] = std::numeric_limits<float>::quiet_NaN();
  rig.inBufs[1][9] = std::numeric_limits<float>::infinity();
  for (int b = 0; b < 3; ++b)
    rig.run();
  bool finite = true;
  for (const auto &ch : rig.outBufs)
    for (float x : ch)
      finite = finite && std::isfinite(x);
  CHECK(finite);
}

TEST_CASE("Processor", "process() does not allocate (parameters, switch, bypass)") {
  Rig rig(48000.0, 256, 2, 2);
  double ph = 0.0;
  fillSine(rig.inBufs[0], 48000.0, 220.0, 0.3f, ph);
  rig.run(); // warm-up
  bbmtest::AllocationGuard guard;
  for (int b = 0; b < 32; ++b) {
    rig.set(bbm::kSustainId, (b % 7) / 7.0);
    rig.set(bbm::kToneId, (b % 5) / 5.0);
    rig.set(bbm::kSwitchId, (b / 8) % 2 == 0 ? 1.0 : 0.0);
    rig.set(bbm::kBypassId, (b / 4) % 2 == 0 ? 0.0 : 1.0);
    fillSine(rig.inBufs[0], 48000.0, 220.0, 0.3f, ph);
    rig.inBufs[1] = rig.inBufs[0];
    rig.run();
  }
  CHECK_MSG(guard.allocations() == 0,
            "process() allocated " + std::to_string(guard.allocations()) + " time(s)");
}
