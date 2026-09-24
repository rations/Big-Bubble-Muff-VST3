// BigBubbleMuff — the LV2 bundle's gate: what a host sees, and whether it runs.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// After the owner's rations-amp tools/rations_lv2check.cpp. The DSP and the editor
// are proved by the unit tests and the VST3 validator; what needs proving here is
// everything between an LV2 host and that code, all of which fails silently:
//
//   1. discovery   lilv (the reference loader real hosts use) finds the plug-in in
//                  THIS bundle, with the right name, class, version, ports, ranges,
//                  latency port, required features and X11 UI;
//   2. running     instantiate at 48 kHz, run audio in varied block sizes (larger
//                  than the stated maximum too): finite, non-silent, L == R, the
//                  latency port reads what the VST3 reports, and run() makes no heap
//                  allocation at all;
//   3. bypass      enabled = 0 and footswitch = 0 each give the input exactly,
//                  delayed by the latency;
//   4. state       a lilv state round trip into a second instance reproduces the
//                  sound, and a hostile or mistyped blob is refused cleanly;
//   5. the UI      refuses (NULL, no crash) without a ui:parent or for another
//                  plug-in's URI;
//   6. embedding   (--x11 only; needs a display) the UI embeds into an UNMAPPED
//                  parent window, so nothing ever appears on screen: it creates its
//                  child there at 500x790, tells the host that size through
//                  ui:resize, runs its idle loop, follows a resize and a port
//                  event, and tears down cleanly.
//
//   bbm_lv2check <spec dir> <bundle dir> [--x11]
//
// The executable exports its operator new (ENABLE_EXPORTS), so the plug-in's own
// allocations resolve to the counter below.
#include "lv2/bbmlv2.h"
#include "ui/layout.h"

#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/options/options.h>
#include <lv2/state/state.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>

#include <X11/Xlib.h>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// ---- allocation counter ------------------------------------------------------
namespace {
std::atomic<long> gAllocations{0};
}

void *operator new(std::size_t n) {
  gAllocations.fetch_add(1, std::memory_order_relaxed);
  if (void *p = std::malloc(n == 0 ? 1 : n))
    return p;
  throw std::bad_alloc();
}
void *operator new(std::size_t n, const std::nothrow_t & /*tag*/) noexcept {
  gAllocations.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(n == 0 ? 1 : n);
}
void *operator new[](std::size_t n) {
  return operator new(n);
}
void *operator new[](std::size_t n, const std::nothrow_t &tag) noexcept {
  return operator new(n, tag);
}
void operator delete(void *p) noexcept {
  std::free(p);
}
void operator delete(void *p, std::size_t /*n*/) noexcept {
  std::free(p);
}
void operator delete[](void *p) noexcept {
  std::free(p);
}
void operator delete[](void *p, std::size_t /*n*/) noexcept {
  std::free(p);
}
void operator delete(void *p, const std::nothrow_t & /*tag*/) noexcept {
  std::free(p);
}
void operator delete[](void *p, const std::nothrow_t & /*tag*/) noexcept {
  std::free(p);
}

namespace {

using namespace bbm;
using namespace bbm::lv2;

int gFailures = 0;

void check(bool ok, const std::string &what) {
  std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok)
    ++gFailures;
}

// ---- urid:map / unmap ---------------------------------------------------------
struct Urids {
  std::vector<std::string> uris{""}; // URID 0 is reserved
  LV2_URID_Map map{this, &Urids::mapUri};
  LV2_URID_Unmap unmap{this, &Urids::unmapUri};

  static LV2_URID mapUri(LV2_URID_Map_Handle h, const char *uri) {
    auto *self = static_cast<Urids *>(h);
    for (std::size_t i = 1; i < self->uris.size(); ++i)
      if (self->uris[i] == uri)
        return static_cast<LV2_URID>(i);
    self->uris.emplace_back(uri);
    return static_cast<LV2_URID>(self->uris.size() - 1);
  }
  static const char *unmapUri(LV2_URID_Unmap_Handle h, LV2_URID id) {
    auto *self = static_cast<Urids *>(h);
    return id > 0 && id < self->uris.size() ? self->uris[id].c_str() : nullptr;
  }
};

struct NodeFree {
  void operator()(LilvNode *n) const noexcept { lilv_node_free(n); }
};
using Node = std::unique_ptr<LilvNode, NodeFree>;
struct InstanceFree {
  void operator()(LilvInstance *i) const noexcept { lilv_instance_free(i); }
};
using Instance = std::unique_ptr<LilvInstance, InstanceFree>;
struct StateFree {
  void operator()(LilvState *s) const noexcept { lilv_state_free(s); }
};
using State = std::unique_ptr<LilvState, StateFree>;
struct WorldFree {
  void operator()(LilvWorld *w) const noexcept { lilv_world_free(w); }
};

constexpr double kRate = 48000.0;
constexpr std::int32_t kMaxBlock = 512;
constexpr std::uint32_t kLatency = 4; // lround(Oversampler kLatencySamples)
constexpr int layout_w = layout::kWidth;
constexpr int layout_h = layout::kHeight;

// One running instance with its own buffers.
struct Rig {
  Instance inst;
  std::array<float, kParamCount> controls{};
  float enabled = 1.0f;
  float latency = -1.0f;
  std::vector<float> in, outL, outR;

  Rig(const LilvPlugin *plugin, const LV2_Feature *const *features)
      : in(1U << 15), outL(in.size()), outR(in.size()) {
    inst.reset(lilv_plugin_instantiate(plugin, kRate, features));
    if (!inst)
      return;
    for (int i = 0; i < kParamCount; ++i)
      controls.at(static_cast<std::size_t>(i)) = static_cast<float>(kParams.at(i).def);
    for (int i = 0; i < kParamCount; ++i)
      lilv_instance_connect_port(inst.get(), kPortControlFirst + static_cast<unsigned>(i),
                                 &controls.at(static_cast<std::size_t>(i)));
    lilv_instance_connect_port(inst.get(), kPortEnabled, &enabled);
    lilv_instance_connect_port(inst.get(), kPortLatency, &latency);
    lilv_instance_activate(inst.get());
  }
  ~Rig() {
    if (inst)
      lilv_instance_deactivate(inst.get());
  }
  Rig(const Rig &) = delete;
  Rig &operator=(const Rig &) = delete;
  Rig(Rig &&) = delete;
  Rig &operator=(Rig &&) = delete;

  // Run `n` frames starting at `offset` in the buffers.
  void run(std::size_t offset, std::uint32_t n) {
    lilv_instance_connect_port(inst.get(), kPortAudioIn, &in.at(offset));
    lilv_instance_connect_port(inst.get(), kPortAudioOutL, &outL.at(offset));
    lilv_instance_connect_port(inst.get(), kPortAudioOutR, &outR.at(offset));
    lilv_instance_run(inst.get(), n);
  }
  // The whole buffer, in the given block-size pattern.
  void runAll(std::span<const std::uint32_t> blocks) {
    std::size_t pos = 0;
    for (std::size_t b = 0; pos < in.size(); ++b) {
      const auto n = static_cast<std::uint32_t>(
          std::min<std::size_t>(blocks[b % blocks.size()], in.size() - pos));
      run(pos, n);
      pos += n;
    }
  }
};

void sine(std::vector<float> &buf, double hz, double amp, std::size_t phase0 = 0) {
  for (std::size_t n = 0; n < buf.size(); ++n)
    buf[n] = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * hz *
                                               static_cast<double>(n + phase0) / kRate));
}

// Exact equality, bit for bit (and -Wfloat-equal clean).
bool same(float a, float b) {
  return std::memcmp(&a, &b, sizeof a) == 0;
}

double rms(std::span<const float> x) {
  double s = 0.0;
  for (const float v : x)
    s += static_cast<double>(v) * v;
  return x.empty() ? 0.0 : std::sqrt(s / static_cast<double>(x.size()));
}

bool allFinite(std::span<const float> x) {
  return std::all_of(x.begin(), x.end(), [](float v) { return std::isfinite(v); });
}

// Exact delayed passthrough from `from` on.
bool isDelayedDry(const Rig &r, std::size_t from) {
  for (std::size_t n = from; n < r.in.size(); ++n)
    if (std::memcmp(&r.outL[n], &r.in[n - kLatency], sizeof(float)) != 0 ||
        std::memcmp(&r.outR[n], &r.in[n - kLatency], sizeof(float)) != 0)
      return false;
  return true;
}

// ---- state callbacks -----------------------------------------------------------
struct PortValues {
  Rig *rig;
  LV2_URID atomFloat;
};

const void *getValue(const char *symbol, void *user, std::uint32_t *size,
                     std::uint32_t *type) {
  auto *pv = static_cast<PortValues *>(user);
  *size = sizeof(float);
  *type = pv->atomFloat;
  const std::string_view s(symbol);
  if (s == "enabled")
    return &pv->rig->enabled;
  for (int i = 0; i < kParamCount; ++i) {
    std::string sym = kParams.at(i).title;
    std::transform(sym.begin(), sym.end(), sym.begin(),
                   [](char c) { return static_cast<char>(std::tolower(c)); });
    if (s == sym)
      return &pv->rig->controls.at(static_cast<std::size_t>(i));
  }
  *size = 0;
  return nullptr;
}

void setValue(const char *symbol, void *user, const void *value, std::uint32_t size,
              std::uint32_t type) {
  auto *pv = static_cast<PortValues *>(user);
  std::uint32_t ignored = 0;
  std::uint32_t t = 0;
  if (size != sizeof(float) || type != pv->atomFloat)
    return;
  if (const void *dst = getValue(symbol, user, &ignored, &t))
    std::memcpy(const_cast<void *>(dst), value, sizeof(float));
}

struct Blob {
  std::vector<unsigned char> bytes;
  LV2_URID key, type;
};
const void *retrieveBlob(LV2_State_Handle h, std::uint32_t key, size_t *size,
                         std::uint32_t *type, std::uint32_t *flags) {
  const auto *b = static_cast<const Blob *>(h);
  if (key != b->key)
    return nullptr;
  *size = b->bytes.size();
  *type = b->type;
  *flags = LV2_STATE_IS_POD;
  return b->bytes.data();
}

struct ResizeLog {
  int w = 0, h = 0, calls = 0;
};
int onUiResize(LV2UI_Feature_Handle h, int w, int hgt) {
  auto *log = static_cast<ResizeLog *>(h);
  log->w = w;
  log->h = hgt;
  ++log->calls;
  return 0;
}

struct WriteLog {
  int writes = 0;
};
void onUiWrite(LV2UI_Controller c, std::uint32_t /*port*/, std::uint32_t /*size*/,
               std::uint32_t /*format*/, const void * /*buffer*/) {
  ++static_cast<WriteLog *>(c)->writes;
}

int gXErrors = 0;
int countXError(Display * /*d*/, XErrorEvent * /*e*/) {
  ++gXErrors;
  return 0;
}

void embedUi(const LV2UI_Descriptor *d, const std::filesystem::path &bundle,
             const LV2_Feature *mapF) {
  Display *dpy = XOpenDisplay(nullptr);
  check(dpy != nullptr, "open the X display");
  if (dpy == nullptr)
    return;
  const XErrorHandler previous = XSetErrorHandler(countXError);
  // Never mapped, so neither it nor anything embedded in it is ever visible.
  const Window parent =
      XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1200, 1800, 0, 0, 0);
  XSync(dpy, False);

  ResizeLog resizeLog;
  LV2UI_Resize resize{&resizeLog, onUiResize};
  LV2_Feature parentF{LV2_UI__parent,
                      reinterpret_cast<void *>(static_cast<std::uintptr_t>(parent))};
  LV2_Feature resizeF{LV2_UI__resize, &resize};
  LV2_Feature idleF{LV2_UI__idleInterface, nullptr};
  const std::array<const LV2_Feature *, 5> uiFeatures{mapF, &parentF, &resizeF, &idleF,
                                                      nullptr};
  WriteLog writes;
  LV2UI_Widget widget = nullptr;
  LV2UI_Handle ui = d->instantiate(d, kPluginUri, bundle.c_str(), onUiWrite, &writes,
                                   &widget, uiFeatures.data());
  check(ui != nullptr && widget != nullptr, "the UI embeds into a parent window");
  const auto *idle =
      static_cast<const LV2UI_Idle_Interface *>(d->extension_data(LV2_UI__idleInterface));
  if (ui != nullptr && idle != nullptr) {
    const auto child = static_cast<Window>(reinterpret_cast<std::uintptr_t>(widget));
    int idleNonZero = 0;
    const auto pump = [&](int ms) {
      for (int t = 0; t < ms / 10; ++t) {
        idleNonZero += idle->idle(ui) != 0 ? 1 : 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    };
    pump(300);
    Window root = 0, up = 0, *kids = nullptr;
    unsigned n = 0;
    XQueryTree(dpy, child, &root, &up, &kids, &n);
    if (kids != nullptr)
      XFree(kids);
    check(up == parent, "the UI's window is a child of ui:parent");
    XWindowAttributes a{};
    XGetWindowAttributes(dpy, child, &a);
    check(a.width == layout_w && a.height == layout_h,
          "it opens at 500x790 (" + std::to_string(a.width) + "x" +
              std::to_string(a.height) + ")");
    check(resizeLog.calls >= 1 && resizeLog.w == layout_w && resizeLog.h == layout_h,
          "and says so through ui:resize");

    // The host resizes the child behind the editor's back (suil's way); the editor
    // must follow it without an X error or a crash.
    XResizeWindow(dpy, child, 1000, 1580);
    XSync(dpy, False);
    pump(200);
    float v = 0.1f;
    d->port_event(ui, kPortControlFirst + static_cast<std::uint32_t>(kSustainId),
                  sizeof v, 0, &v);
    v = 0.0f;
    d->port_event(ui, kPortEnabled, sizeof v, 0, &v);
    d->port_event(ui, kPortEnabled, 3, 0, &v); // wrong size: ignored
    d->port_event(ui, 999, sizeof v, 0, &v);   // no such port: ignored
    pump(200);
    check(writes.writes == 0, "port events from the host are not echoed back");
    check(idleNonZero == 0, "idle() always answers 0 (the UI stays alive)");
    d->cleanup(ui);
    XSync(dpy, False);
    check(gXErrors == 0, "no X errors (" + std::to_string(gXErrors) + ")");
  }
  XDestroyWindow(dpy, parent);
  XSetErrorHandler(previous);
  XCloseDisplay(dpy);
}

} // namespace

int main(int argc, char **argv) {
  const std::span<char *> args(argv, static_cast<std::size_t>(argc));
  const bool embed = args.size() == 4 && std::string_view(args[3]) == "--x11";
  if (args.size() != 3 && !embed) {
    std::fputs("usage: bbm_lv2check <lv2 spec dir> <bundle dir> [--x11]\n", stderr);
    return 2;
  }
  const std::filesystem::path bundle = std::filesystem::absolute(args[2]);
  Urids urids;
  const std::unique_ptr<LilvWorld, WorldFree> world(lilv_world_new());
  LilvWorld *w = world.get();

  // ---- 1. discovery ----------------------------------------------------------
  std::puts("== discovery ==");
  {
    // Only the system spec bundles and this bundle: never ~/.lv2, where an older
    // installed copy would answer for the same URI.
    const std::string path = std::string(args[1]) + ":" + bundle.parent_path().string();
    const Node lv2Path(lilv_new_string(w, path.c_str()));
    lilv_world_set_option(w, LILV_OPTION_LV2_PATH, lv2Path.get());
  }
  lilv_world_load_all(w);
  const Node pluginUri(lilv_new_uri(w, kPluginUri));
  const LilvPlugin *plugin =
      lilv_plugins_get_by_uri(lilv_world_get_all_plugins(w), pluginUri.get());
  check(plugin != nullptr, std::string("lilv finds <") + kPluginUri + ">");
  if (plugin == nullptr)
    return 1;

  {
    char *bpath = lilv_file_uri_parse(
        lilv_node_as_uri(lilv_plugin_get_bundle_uri(plugin)), nullptr);
    const std::string found = bpath != nullptr ? bpath : "";
    lilv_free(bpath);
    std::error_code ec;
    check(std::filesystem::equivalent(found, bundle, ec), "found in " + bundle.string());
  }
  {
    const Node name(lilv_plugin_get_name(plugin));
    check(name && std::string_view(lilv_node_as_string(name.get())) == stringPluginName,
          std::string("doap:name \"") + stringPluginName + "\"");
    const LilvNode *cls = lilv_plugin_class_get_uri(lilv_plugin_get_class(plugin));
    check(std::string_view(lilv_node_as_uri(cls)) == kPluginClassUri,
          "class lv2:DistortionPlugin");

    const Node minorP(lilv_new_uri(w, LV2_CORE__minorVersion));
    const Node microP(lilv_new_uri(w, LV2_CORE__microVersion));
    const Node minor(lilv_world_get(w, pluginUri.get(), minorP.get(), nullptr));
    const Node micro(lilv_world_get(w, pluginUri.get(), microP.get(), nullptr));
    check(minor && micro && lilv_node_as_int(minor.get()) == kMinorVersion &&
              lilv_node_as_int(micro.get()) == kMicroVersion,
          "lv2:minorVersion " + std::to_string(kMinorVersion) + ", microVersion " +
              std::to_string(kMicroVersion));
  }

  check(lilv_plugin_get_num_ports(plugin) == kPortCount,
        std::to_string(kPortCount) + " ports");
  {
    std::vector<float> mins(kPortCount), maxs(kPortCount), defs(kPortCount);
    lilv_plugin_get_port_ranges_float(plugin, mins.data(), maxs.data(), defs.data());
    bool ranges = true;
    for (int i = 0; i < kParamCount; ++i) {
      const std::size_t p = kPortControlFirst + static_cast<std::size_t>(i);
      ranges = ranges && same(mins[p], static_cast<float>(kParams.at(i).min)) &&
               same(maxs[p], static_cast<float>(kParams.at(i).max)) &&
               same(defs[p], static_cast<float>(kParams.at(i).def));
    }
    check(ranges, "every control port's range and default is kParams'");
    check(same(defs[kPortEnabled], 1.0f), "enabled defaults to 1");
  }
  {
    const Node enabledD(lilv_new_uri(w, LV2_CORE__enabled));
    const Node inputPort(lilv_new_uri(w, LV2_CORE__InputPort));
    const LilvPort *port =
        lilv_plugin_get_port_by_designation(plugin, inputPort.get(), enabledD.get());
    check(port != nullptr && lilv_port_get_index(plugin, port) == kPortEnabled,
          "lv2:enabled designates the bypass port");
  }
  check(lilv_plugin_has_latency(plugin) &&
            lilv_plugin_get_latency_port_index(plugin) == kPortLatency,
        "lv2:reportsLatency on port " + std::to_string(kPortLatency));
  {
    LilvNodes *req = lilv_plugin_get_required_features(plugin);
    bool onlyMap = lilv_nodes_size(req) == 1;
    LILV_FOREACH(nodes, i, req)
    onlyMap = onlyMap &&
              std::string_view(lilv_node_as_uri(lilv_nodes_get(req, i))) == LV2_URID__map;
    lilv_nodes_free(req);
    check(onlyMap, "the only required feature is urid:map");
  }
  {
    LilvUIs *uis = lilv_plugin_get_uis(plugin);
    const Node x11(lilv_new_uri(w, LV2_UI__X11UI));
    bool found = false;
    LILV_FOREACH(uis, i, uis) {
      const LilvUI *ui = lilv_uis_get(uis, i);
      if (std::string_view(lilv_node_as_uri(lilv_ui_get_uri(ui))) == kUiUri)
        found = lilv_ui_is_a(ui, x11.get());
    }
    lilv_uis_free(uis);
    check(found, std::string("<") + kUiUri + "> is a ui:X11UI");
  }

  // ---- 2. running -------------------------------------------------------------
  std::puts("== running ==");
  const LV2_URID atomInt = urids.map.map(&urids, LV2_ATOM__Int);
  const LV2_URID atomFloat = urids.map.map(&urids, LV2_ATOM__Float);
  const LV2_URID atomChunk = urids.map.map(&urids, LV2_ATOM__Chunk);
  const std::int32_t maxBlock = kMaxBlock;
  const std::array<LV2_Options_Option, 2> options{{
      {LV2_OPTIONS_INSTANCE, 0, urids.map.map(&urids, LV2_BUF_SIZE__maxBlockLength),
       sizeof maxBlock, atomInt, &maxBlock},
      {LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr},
  }};
  LV2_Feature mapF{LV2_URID__map, &urids.map};
  LV2_Feature unmapF{LV2_URID__unmap, &urids.unmap};
  LV2_Feature optF{LV2_OPTIONS__options,
                   const_cast<LV2_Options_Option *>(options.data())};
  LV2_Feature boundedF{LV2_BUF_SIZE__boundedBlockLength, nullptr};
  const std::array<const LV2_Feature *, 5> features{&mapF, &unmapF, &optF, &boundedF,
                                                    nullptr};
  {
    const std::array<const LV2_Feature *, 1> none{nullptr};
    check(lilv_plugin_instantiate(plugin, kRate, none.data()) == nullptr,
          "instantiate without urid:map is refused");
  }

  const long beforeInst = gAllocations.load();
  Rig a(plugin, features.data());
  check(a.inst != nullptr, "instantiate at 48 kHz, maxBlockLength 512");
  if (!a.inst)
    return 1;
  check(gAllocations.load() > beforeInst,
        "the plug-in's allocations reach this counter (interposition works)");

  sine(a.in, 196.0, 0.1);
  // Warm up once (first-block port pushes), then count.
  a.run(0, 64);
  const long before = gAllocations.load();
  const std::array<std::uint32_t, 6> blocks{256, 1, 511, 512, 64, 2000};
  a.runAll(blocks);
  // Read before building the message: the message itself allocates.
  const long during = gAllocations.load() - before;
  check(during == 0, "run() allocates nothing (" + std::to_string(during) +
                         " allocations over " + std::to_string(a.in.size()) + " frames)");
  check(allFinite(a.outL) && allFinite(a.outR), "every output sample is finite");
  const double level = rms(std::span<const float>(a.outL).subspan(a.in.size() / 2));
  check(level > 1e-3,
        "a 196 Hz sine at 0.1 comes out non-silent (RMS " + std::to_string(level) + ")");
  check(std::memcmp(a.outL.data(), a.outR.data(), a.outL.size() * sizeof(float)) == 0,
        "mono in: left and right are identical");
  check(same(a.latency, static_cast<float>(kLatency)),
        "the latency port reads " + std::to_string(kLatency) + " (the VST3's figure)");

  // ---- 3. bypass ---------------------------------------------------------------
  std::puts("== bypass ==");
  {
    a.enabled = 0.0f;
    a.runAll(blocks); // ramp out
    a.runAll(blocks);
    check(isDelayedDry(a, kLatency), "enabled = 0: exactly the input, 4 samples late");
    a.enabled = 1.0f;
    a.controls.at(kSwitchId) = 0.0f;
    a.runAll(blocks);
    a.runAll(blocks);
    check(isDelayedDry(a, kLatency), "footswitch = 0: exactly the input, 4 samples late");
    a.controls.at(kSwitchId) = 1.0f;
    a.controls.at(kSustainId) = std::numeric_limits<float>::quiet_NaN();
    a.controls.at(kOutputId) = 1e30f;
    a.runAll(blocks);
    check(allFinite(a.outL), "NaN and absurd control values stay finite");
    a.controls.at(kSustainId) = 0.3f;
    a.controls.at(kOutputId) = -6.0f;
  }

  // ---- 4. state ------------------------------------------------------------------
  std::puts("== state ==");
  {
    a.runAll(blocks);
    PortValues pvA{&a, atomFloat};
    const State st(lilv_state_new_from_instance(
        plugin, a.inst.get(), &urids.map, nullptr, nullptr, nullptr, nullptr, getValue,
        &pvA, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, features.data()));
    check(st != nullptr, "lilv saves a state from the instance");
    if (st) {
      char *text = lilv_state_to_string(w, &urids.map, &urids.unmap, st.get(),
                                        "urn:bbm:check", nullptr);
      const std::string ttl = text != nullptr ? text : "";
      lilv_free(text);
      check(ttl.find(kStateBlobUri) != std::string::npos,
            "the state carries the VST3 blob under #state");

      Rig b(plugin, features.data());
      PortValues pvB{&b, atomFloat};
      lilv_state_restore(st.get(), b.inst.get(), setValue, &pvB, 0, features.data());
      check(same(b.controls.at(kSustainId), 0.3f) &&
                same(b.controls.at(kOutputId), -6.0f),
            "restore sets the port values");
      // Same input to both; after the smoothers settle the two must agree.
      sine(a.in, 440.0, 0.2, 7);
      b.in = a.in;
      for (int pass = 0; pass < 3; ++pass) {
        a.runAll(blocks);
        b.runAll(blocks);
      }
      double err = 0.0;
      for (std::size_t n = b.in.size() / 2; n < b.in.size(); ++n)
        err = std::max(err, std::fabs(static_cast<double>(a.outL[n]) - b.outL[n]));
      check(err < 1e-4,
            "a restored instance sounds the same (max diff " + std::to_string(err) + ")");
    }

    const auto *iface = static_cast<const LV2_State_Interface *>(
        lilv_instance_get_extension_data(a.inst.get(), LV2_STATE__interface));
    check(iface != nullptr && iface->restore != nullptr, "state:interface is provided");
    if (iface != nullptr) {
      LV2_Handle h = lilv_instance_get_handle(a.inst.get());
      const LV2_URID key = urids.map.map(&urids, kStateBlobUri);
      Blob junk{std::vector<unsigned char>(64, 0xFF), key, atomChunk};
      check(iface->restore(h, retrieveBlob, &junk, 0, nullptr) != LV2_STATE_SUCCESS,
            "a garbage blob is refused");
      Blob wrongType{std::vector<unsigned char>(64, 0), key, atomInt};
      check(iface->restore(h, retrieveBlob, &wrongType, 0, nullptr) ==
                LV2_STATE_ERR_BAD_TYPE,
            "a mistyped blob is refused as BAD_TYPE");
      Blob missing{{}, key + 1000, atomChunk};
      check(iface->restore(h, retrieveBlob, &missing, 0, nullptr) ==
                LV2_STATE_ERR_NO_PROPERTY,
            "a missing blob is NO_PROPERTY");
      a.runAll(blocks);
      check(allFinite(a.outL) && rms(a.outL) > 1e-3, "still running after hostile state");
    }
  }

  // ---- 5. the UI -------------------------------------------------------------------
  std::puts("== ui ==");
  {
    const std::string so = (bundle / kUiBinary).string();
    void *lib = dlopen(so.c_str(), RTLD_NOW | RTLD_LOCAL);
    check(lib != nullptr, std::string("dlopen ") + kUiBinary);
    if (lib != nullptr) {
      using Fn = const LV2UI_Descriptor *(*)(std::uint32_t);
      auto fn = reinterpret_cast<Fn>(dlsym(lib, "lv2ui_descriptor"));
      const LV2UI_Descriptor *d = fn != nullptr ? fn(0) : nullptr;
      check(d != nullptr && std::string_view(d->URI) == kUiUri && fn(1) == nullptr,
            "lv2ui_descriptor(0) is the X11 UI, and there is only one");
      if (d != nullptr) {
        LV2UI_Widget widget = nullptr;
        const std::array<const LV2_Feature *, 2> noParent{&mapF, nullptr};
        check(d->instantiate(d, kPluginUri, bundle.c_str(), nullptr, nullptr, &widget,
                             noParent.data()) == nullptr,
              "the UI refuses to open without ui:parent");
        check(d->instantiate(d, "urn:someone:else", bundle.c_str(), nullptr, nullptr,
                             &widget, noParent.data()) == nullptr,
              "the UI refuses another plug-in's URI");
        check(d->extension_data(LV2_UI__idleInterface) != nullptr,
              "ui:idleInterface is provided");
        if (embed) {
          std::puts("== embedding ==");
          embedUi(d, bundle, &mapF);
        }
      }
      dlclose(lib);
    }
  }

  std::printf("\nbbm_lv2check: %s (%d failure%s)\n", gFailures == 0 ? "PASSED" : "FAILED",
              gFailures, gFailures == 1 ? "" : "s");
  return gFailures == 0 ? 0 : 1;
}
