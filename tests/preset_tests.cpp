// BigBubbleMuff — user preset tests: the file format, the on-disk store, the bar.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Everything on disk happens under a fresh directory inside the build tree
// (BBM_TEST_SCRATCH), never in the user's real preset folder.
#include "test.h"

#include "dsp/Checked.h"
#include "plugin/controller.h"
#include "plugin/ids.h"
#include "presets/presetstore.h"
#include "ui/bbmview.h"
#include "ui/layout.h"
#include "ui/presetbar.h"

#include "pluginterfaces/base/keycodes.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Steinberg;
namespace fs = std::filesystem;
using bbm::presets::Norms;
using bbm::presets::Store;

namespace {

// A new, empty directory for one test, removed again when it goes out of scope.
struct Scratch {
  fs::path root;
  Scratch() {
    fs::create_directories(BBM_TEST_SCRATCH);
    std::string templ = std::string(BBM_TEST_SCRATCH) + "/presets-XXXXXX";
    if (::mkdtemp(templ.data()) != nullptr)
      root = templ;
  }
  ~Scratch() {
    std::error_code ec;
    if (!root.empty())
      fs::remove_all(root, ec);
  }
  Scratch(const Scratch &) = delete;
  Scratch &operator=(const Scratch &) = delete;
  Scratch(Scratch &&) = delete;
  Scratch &operator=(Scratch &&) = delete;
  std::string dir(const char *sub = "Presets") const { return (root / sub).string(); }
};

Norms defaults() {
  Norms n{};
  for (int i = 0; i < bbm::kParamCount; ++i)
    bbm::at(n, i) = bbm::toNorm(bbm::at(bbm::kParams, i), bbm::at(bbm::kParams, i).def);
  return n;
}

bool same(const Norms &a, const Norms &b) {
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::fabs(a[i] - b[i]) > 1e-12)
      return false;
  return true;
}

void writeFile(const fs::path &p, const std::string &text) {
  std::ofstream(p, std::ios::binary) << text;
}

unsigned mode(const fs::path &p) {
  struct stat st{};
  return ::stat(p.c_str(), &st) == 0 ? static_cast<unsigned>(st.st_mode & 0777U) : 0U;
}

class Probe : public bbm::BbmView {
public:
  using BbmView::BbmView;
  void attach() { onAttached(); }
  void resize(int w, int h) { onResized(w, h); }
  void press(float x, float y) {
    onMouseDown(static_cast<int>(x), static_cast<int>(y), 1);
    onMouseUp(static_cast<int>(x), static_cast<int>(y), 1);
  }
  void type(const char *s) {
    for (; *s != '\0'; ++s)
      onKeyDownNative(static_cast<char16>(*s), 0, 0);
  }
  void keyCode(int16 code) { onKeyDownNative(0, code, 0); }
  void tickN(int n) {
    for (int i = 0; i < n; ++i)
      onTick();
  }
};

struct BarRig {
  IPtr<bbm::Controller> ctrl;
  IPtr<Probe> view;
  explicit BarRig(const std::string &dir) : ctrl(owned(new bbm::Controller())) {
    ctrl->initialize(nullptr);
    view = owned(new Probe(ctrl, dir));
    ctrl->editorAttached(view);
    view->attach();
    view->resize(bbm::layout::kWidth, bbm::layout::kHeight);
  }
  ~BarRig() {
    ctrl->editorRemoved(view);
    view = nullptr;
    ctrl->terminate();
  }
  BarRig(const BarRig &) = delete;
  BarRig &operator=(const BarRig &) = delete;
  BarRig(BarRig &&) = delete;
  BarRig &operator=(BarRig &&) = delete;
  double norm(Vst::ParamID id) const { return ctrl->getParamNormalized(id); }
};

void centre(Probe &v, const bbm::Rect &r) {
  v.press(r.centreX(), r.centreY());
}

} // namespace

TEST_CASE("Preset format", "names: only the safe alphabet, no dot-files, 1..64") {
  using bbm::presets::nameIsSafe;
  CHECK(nameIsSafe("My Fuzz"));
  CHECK(nameIsSafe("a"));
  CHECK(nameIsSafe("v1.2_final-take"));
  CHECK(nameIsSafe(std::string(64, 'x')));
  CHECK(!nameIsSafe(""));
  CHECK(!nameIsSafe(std::string(65, 'x')));
  CHECK(!nameIsSafe(".hidden"));
  CHECK(!nameIsSafe(".."));
  CHECK(!nameIsSafe("../escape"));
  CHECK(!nameIsSafe("a/b"));
  CHECK(!nameIsSafe("trailing "));
  CHECK(!nameIsSafe(std::string("nul\0byte", 8)));
  CHECK(!nameIsSafe("tab\there"));
  CHECK(!nameIsSafe("caf\xC3\xA9"));
}

TEST_CASE("Preset format", "round-trips the knobs exactly and leaves the switch alone") {
  Norms in = defaults();
  in[bbm::kSustainId] = 0.123456789;
  in[bbm::kToneId] = 0.9;
  in[bbm::kVolumeId] = 0.0;
  in[bbm::kOutputId] = bbm::toNorm(bbm::kParams[bbm::kOutputId], -7.25);
  in[bbm::kGateId] = 1.0;
  in[bbm::kSwitchId] = 0.0;
  const std::string text = bbm::presets::serialise(in);
  bbmtest::log(text);
  CHECK(text.rfind("#BBMPRESET 1\n", 0) == 0);
  CHECK(text.find("output=-7.25\n") != std::string::npos);
  CHECK(text.find("switch") == std::string::npos);

  Norms out = defaults();
  out[bbm::kSwitchId] = 1.0;
  CHECK(bbm::presets::parse(text, out));
  Norms expect = in;
  expect[bbm::kSwitchId] = 1.0; // not carried: the caller's value stays
  CHECK(same(out, expect));
}

TEST_CASE("Preset format", "rejects malformed files and applies nothing") {
  const char *bad[] = {
      "",
      "sustain=0.5\n",                   // no header
      "\xEF\xBB\xBF#BBMPRESET 1\n",      // BOM before the header
      "#BBMPRESET\n",                    // no version
      "#BBMPRESET 0\n",                  // version too old
      "#BBMPRESET 2\nsustain=0.5\n",     // from the future
      "#BBMPRESET 1x\n",                 // junk version
      "#BBMPRESET 1\nsustain=abc\n",     // not a number
      "#BBMPRESET 1\nsustain=0.5abc\n",  // trailing junk
      "#BBMPRESET 1\nsustain=nan\n",     // non-finite
      "#BBMPRESET 1\ntone=inf\n",        // non-finite
      "#BBMPRESET 1\nvolume=1e999\n",    // overflows to inf
      "#BBMPRESET 1\ngate=\n",           // empty value
      "#BBMPRESET 1\njust some words\n", // no '='
  };
  for (const char *text : bad) {
    Norms out = defaults();
    out[bbm::kToneId] = 0.321;
    const Norms before = out;
    CHECK_MSG(!bbm::presets::parse(text, out), text);
    CHECK_MSG(same(out, before), text);
  }
  // Over the size cap, however valid its content.
  std::string big = "#BBMPRESET 1\n";
  big.append(bbm::presets::kMaxFileBytes, '#');
  Norms out = defaults();
  CHECK(!bbm::presets::parse(big, out));
}

TEST_CASE("Preset format", "clamps, defaults missing keys, ignores unknown ones") {
  Norms out = defaults();
  out[bbm::kSustainId] = 0.0;
  out[bbm::kSwitchId] = 0.0;
  const char *text = "#BBMPRESET 1\r\n"
                     "# a comment\r\n"
                     "\r\n"
                     "  tone = 5 \r\n"  // clamps to 1
                     "output=-1000\r\n" // clamps to -24 dB
                     "volume=0.25\r\n"
                     "volume=0.75\r\n"   // the last one wins
                     "futureKnob=12\r\n" // ignored
                     "footswitch=1\r\n"; // not a preset key: ignored
  CHECK(bbm::presets::parse(text, out));
  CHECK(std::fabs(out[bbm::kToneId] - 1.0) < 1e-12);
  CHECK(std::fabs(out[bbm::kOutputId] - 0.0) < 1e-12);
  CHECK(std::fabs(out[bbm::kVolumeId] - 0.75) < 1e-12);
  CHECK(std::fabs(out[bbm::kSustainId] - 0.75) < 1e-12); // missing: the default
  CHECK(std::fabs(out[bbm::kGateId] - 0.12) < 1e-12);
  CHECK(std::fabs(out[bbm::kSwitchId] - 0.0) < 1e-12); // untouched
}

TEST_CASE("Preset store", "save, list, load, replace and remove; 0700 dir, 0600 file") {
  const Scratch scratch;
  const Store store(scratch.dir("config/BigBubbleMuff/Presets"));
  CHECK(store.list().empty());

  Norms a = defaults();
  a[bbm::kSustainId] = 0.2;
  CHECK(store.save("beta", a));
  CHECK(store.save("Alpha", a));
  CHECK(store.save("alpha 2", a));
  CHECK(mode(store.dir()) == 0700U);
  CHECK(mode(scratch.dir("config")) == 0700U);
  CHECK(mode(store.dir() + "/beta.bbmpreset") == 0600U);

  const std::vector<std::string> names = store.list();
  CHECK(names == (std::vector<std::string>{"Alpha", "alpha 2", "beta"}));

  Norms got = defaults();
  CHECK(store.load("beta", got));
  CHECK(same(got, a));

  a[bbm::kSustainId] = 0.9;
  CHECK(store.save("beta", a)); // replaces
  CHECK(store.load("beta", got));
  CHECK(std::fabs(got[bbm::kSustainId] - 0.9) < 1e-12);
  CHECK(!fs::exists(store.dir() + "/beta.bbmpreset.tmp"));

  CHECK(store.exists("beta"));
  CHECK(store.remove("beta"));
  CHECK(!store.exists("beta"));
  CHECK(!store.remove("beta"));
  CHECK(!store.load("beta", got));
}

TEST_CASE("Preset store", "refuses unsafe names, symlinks, directories and big files") {
  const Scratch scratch;
  const Store store(scratch.dir());
  Norms n = defaults();
  CHECK(!store.save("../escape", n));
  CHECK(!store.save(".hidden", n));
  CHECK(!store.save("", n));
  CHECK(!fs::exists(scratch.root / "escape.bbmpreset"));

  CHECK(store.save("real", n));
  const fs::path dir(store.dir());

  // A symlink dressed as a preset: not listed, not loaded, not removable.
  const fs::path target = scratch.root / "target.txt";
  writeFile(target, "#BBMPRESET 1\nsustain=0\n");
  fs::create_symlink(target, dir / "link.bbmpreset");
  // A directory dressed as one, a leftover temp file, and a stray old .xml.
  fs::create_directory(dir / "folder.bbmpreset");
  writeFile(dir / "crash.bbmpreset.tmp", "#BBMPRESET 1\n");
  writeFile(dir / "old.xml", "<Parameters/>");
  // A file one byte over the cap.
  writeFile(dir / "huge.bbmpreset",
            "#BBMPRESET 1\n" + std::string(bbm::presets::kMaxFileBytes, '#'));

  CHECK(store.list() == (std::vector<std::string>{"huge", "real"}));
  CHECK(!store.load("link", n));
  CHECK(!store.remove("link"));
  CHECK(fs::is_symlink(dir / "link.bbmpreset"));
  CHECK(!store.load("folder", n));
  CHECK(!store.load("huge", n));

  // A symlink planted at the temp name is not written through.
  fs::create_symlink(target, dir / "real.bbmpreset.tmp");
  n[bbm::kToneId] = 0.0;
  CHECK(store.save("real", n));
  std::string content(static_cast<std::size_t>(fs::file_size(target)), '\0');
  std::ifstream(target, std::ios::binary)
      .read(content.data(), static_cast<std::streamsize>(content.size()));
  CHECK(content == "#BBMPRESET 1\nsustain=0\n");

  // A store with no directory fails everything, cleanly.
  const Store none("");
  CHECK(none.list().empty());
  CHECK(!none.save("x", n));
  CHECK(!none.load("x", n));
  CHECK(!none.remove("x"));
}

TEST_CASE("Preset store", "the default directory is under $HOME/.config") {
  const std::string dir = Store::defaultDir();
  const char *home = std::getenv("HOME");
  if (home != nullptr && home[0] == '/') {
    CHECK(dir.size() > 30);
    CHECK(dir.find("/.config/BigBubbleMuff/Presets") == dir.size() - 30);
  } else {
    CHECK(dir.empty());
  }
}

TEST_CASE("Preset bar", "save by name, reload over changed knobs, delete on confirm") {
  const Scratch scratch;
  BarRig rig(scratch.dir());
  Probe &v = *rig.view;
  const Store store(scratch.dir());

  // Set a sound, then Save -> type -> Enter.
  rig.ctrl->setParamNormalized(bbm::kSustainId, 0.3);
  rig.ctrl->setParamNormalized(bbm::kOutputId, 0.25);
  centre(v, bbm::PresetBar::saveButton());
  v.type("My Fuzz/?"); // '/' and '?' are not name characters and are dropped
  v.keyCode(KEY_RETURN);
  CHECK(store.list() == (std::vector<std::string>{"My Fuzz"}));

  // Escape abandons a name without saving.
  centre(v, bbm::PresetBar::saveButton());
  v.keyCode(KEY_END);
  v.type(" 2");
  v.keyCode(KEY_ESCAPE);
  CHECK(store.list().size() == 1);

  // Move the knobs, the footswitch too, then load the preset from the list.
  rig.ctrl->setParamNormalized(bbm::kSustainId, 0.9);
  rig.ctrl->setParamNormalized(bbm::kOutputId, 0.8);
  rig.ctrl->setParamNormalized(bbm::kSwitchId, 0.0);
  centre(v, bbm::PresetBar::nameBox());
  v.press(bbm::PresetBar::nameBox().centreX(),
          static_cast<float>(bbm::layout::kBarHeight) +
              bbm::PresetBar::kRowHeight * 0.5f);
  CHECK(std::fabs(rig.norm(bbm::kSustainId) - 0.3) < 1e-12);
  CHECK(std::fabs(rig.norm(bbm::kOutputId) - 0.25) < 1e-12);
  CHECK(std::fabs(rig.norm(bbm::kSwitchId) - 0.0) < 1e-12); // not a preset value

  // Delete: the first click only arms it, the arm times out, then two clicks delete.
  centre(v, bbm::PresetBar::deleteButton());
  CHECK(store.list().size() == 1);
  v.tickN(100);
  centre(v, bbm::PresetBar::deleteButton());
  CHECK(store.list().size() == 1);
  centre(v, bbm::PresetBar::deleteButton());
  CHECK(store.list().empty());
}

TEST_CASE("Preset bar", "a click that closes the list never turns a knob") {
  const Scratch scratch;
  BarRig rig(scratch.dir());
  Probe &v = *rig.view;
  centre(v, bbm::PresetBar::nameBox()); // opens the (empty) list
  const auto &sustain = bbm::layout::kKnobs[0];
  const double before = rig.norm(bbm::kSustainId);
  v.press(static_cast<float>(sustain.cx),
          static_cast<float>(sustain.cy + bbm::layout::kBarHeight));
  CHECK(std::fabs(rig.norm(bbm::kSustainId) - before) < 1e-12);
  CHECK(std::fabs(rig.norm(bbm::kSwitchId) - 1.0) < 1e-12);
}
