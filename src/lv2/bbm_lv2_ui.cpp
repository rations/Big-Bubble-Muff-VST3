// BigBubbleMuff — bigbubblemuff_ui.so, the LV2 ui:X11UI half.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Like its DSP sibling, a HOST rather than a second editor: it instantiates the same
// bbm::Controller and BbmView a VST3 host gets, and supplies the host objects the
// pair expects: an IComponentHandler, and an IPlugFrame carrying Linux::IRunLoop.
// After the owner's rations-amp products/rations/lv2/rations_lv2_ui.cpp.
//
// THE RUN LOOP IS THE WHOLE TRICK. X11PlugView already does everything an LV2 X11 UI
// needs (its own Display, the parent's visual, XEmbed mapping, a non-fatal X error
// handler, and it follows a parent resized behind its back via ConfigureNotify).
// What it lacks is a clock: on Linux it borrows the host's IRunLoop for its X fd and
// its 33 ms tick. LV2 supplies that clock as ui:idleInterface, which the host calls
// at least 30 times a second, so this file implements IRunLoop over idle() and the
// same window class serves both formats.
//
// Every parameter is a control port, so the host is the whole transport: an edit
// becomes a port write, and every port change (automation, the DSP's restored
// state, another UI) arrives as port_event and goes into the controller.
#include "lv2/bbmlv2.h"

#include "plugin/controller.h"
#include "ui/bbmview.h"

#include "pluginterfaces/gui/iplugview.h"

#include <lv2/core/lv2.h>
#include <lv2/ui/ui.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

using namespace Steinberg;

namespace bbm::lv2 {
namespace {

class Ui;

// Both host objects below have NON-DELETING reference counts: they are members of
// the Ui and outlive every holder (the controller keeps its handler in an IPtr, the
// view its frame), so a release must never `delete this` on a member.
//
// The SDK interfaces are COM-style and deliberately have no virtual destructor;
// nothing here is ever deleted through one (see x11plugview.h).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

// The host end of the edit loop: a user edit goes out as a control-port write, which
// is what makes the host's automation see it. beginEdit/endEdit have an LV2 analogue
// in ui:touch, which is optional and has no behaviour riding on it here.
class ComponentHandler final : public Vst::IComponentHandler {
public:
  explicit ComponentHandler(Ui &owner) : mOwner(owner) {}
  ~ComponentHandler() = default;
  ComponentHandler(const ComponentHandler &) = delete;
  ComponentHandler &operator=(const ComponentHandler &) = delete;
  ComponentHandler(ComponentHandler &&) = delete;
  ComponentHandler &operator=(ComponentHandler &&) = delete;

  tresult PLUGIN_API beginEdit(Vst::ParamID /*id*/) override { return kResultOk; }
  tresult PLUGIN_API performEdit(Vst::ParamID id, Vst::ParamValue value) override;
  tresult PLUGIN_API endEdit(Vst::ParamID /*id*/) override { return kResultOk; }
  tresult PLUGIN_API restartComponent(int32 /*flags*/) override { return kResultOk; }

  tresult PLUGIN_API queryInterface(const TUID queried, void **obj) override {
    if (obj == nullptr)
      return kInvalidArgument;
    if (FUnknownPrivate::iidEqual(queried, Vst::IComponentHandler::iid) ||
        FUnknownPrivate::iidEqual(queried, FUnknown::iid)) {
      *obj = static_cast<Vst::IComponentHandler *>(this);
      return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return 1000; }
  uint32 PLUGIN_API release() override { return 1000; }

private:
  Ui &mOwner;
};

// IPlugFrame, and the Linux::IRunLoop the view asks it for. The view registers one
// event handler on its X connection and one timer; idle() services both. Offering
// the fd on every idle is safe because onFDIsSet only drains what XPending reports.
class Frame final : public IPlugFrame, public Linux::IRunLoop {
public:
  explicit Frame(Ui &owner) : mOwner(owner) {}
  ~Frame() = default;
  Frame(const Frame &) = delete;
  Frame &operator=(const Frame &) = delete;
  Frame(Frame &&) = delete;
  Frame &operator=(Frame &&) = delete;

  tresult PLUGIN_API resizeView(IPlugView *view, ViewRect *newSize) override;

  tresult PLUGIN_API registerEventHandler(Linux::IEventHandler *handler,
                                          Linux::FileDescriptor fd) override {
    if (handler == nullptr)
      return kInvalidArgument;
    mEventHandler = handler;
    mFd = fd;
    return kResultTrue;
  }
  tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler *handler) override {
    if (handler == mEventHandler)
      mEventHandler = nullptr;
    return kResultTrue;
  }
  tresult PLUGIN_API registerTimer(Linux::ITimerHandler *handler,
                                   Linux::TimerInterval milliseconds) override {
    if (handler == nullptr)
      return kInvalidArgument;
    mTimerHandler = handler;
    mInterval = std::chrono::milliseconds(static_cast<long long>(milliseconds));
    mNextTick = std::chrono::steady_clock::now();
    return kResultTrue;
  }
  tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler *handler) override {
    if (handler == mTimerHandler)
      mTimerHandler = nullptr;
    return kResultTrue;
  }

  // One turn of the loop, from the host's idle().
  void pump() {
    if (mEventHandler != nullptr)
      mEventHandler->onFDIsSet(mFd);
    if (mTimerHandler == nullptr)
      return;
    const auto now = std::chrono::steady_clock::now();
    if (now < mNextTick)
      return;
    // From NOW, not from the last deadline: a host that stalled for a second must
    // not be owed thirty ticks.
    mNextTick = now + mInterval;
    mTimerHandler->onTimer();
  }

  tresult PLUGIN_API queryInterface(const TUID queried, void **obj) override {
    if (obj == nullptr)
      return kInvalidArgument;
    if (FUnknownPrivate::iidEqual(queried, IPlugFrame::iid) ||
        FUnknownPrivate::iidEqual(queried, FUnknown::iid)) {
      *obj = static_cast<IPlugFrame *>(this);
      return kResultOk;
    }
    if (FUnknownPrivate::iidEqual(queried, Linux::IRunLoop::iid)) {
      *obj = static_cast<Linux::IRunLoop *>(this);
      return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return 1000; }
  uint32 PLUGIN_API release() override { return 1000; }

private:
  Ui &mOwner;
  Linux::IEventHandler *mEventHandler = nullptr;
  Linux::FileDescriptor mFd = -1;
  Linux::ITimerHandler *mTimerHandler = nullptr;
  std::chrono::milliseconds mInterval{33};
  std::chrono::steady_clock::time_point mNextTick;
};

#pragma GCC diagnostic pop

struct DisplayCloser {
  void operator()(::Display *d) const noexcept { XCloseDisplay(d); }
};

class Ui {
public:
  Ui(LV2UI_Write_Function write, LV2UI_Controller controller)
      : mHandler(*this), mFrame(*this), mWrite(write), mWriteController(controller) {}
  ~Ui() { close(); }
  Ui(const Ui &) = delete;
  Ui &operator=(const Ui &) = delete;
  Ui(Ui &&) = delete;
  Ui &operator=(Ui &&) = delete;

  bool open(::Window parent, const LV2UI_Resize *resize);
  void close();
  int idle() {
    mFrame.pump();
    // Always 0: a non-zero answer means "the UI was closed" and the host stops
    // calling idle(), which would freeze an embedded editor.
    return 0;
  }
  ::Window widget() const { return mView ? mView->nativeWindow() : 0; }

  void portEvent(std::uint32_t port, std::uint32_t size, std::uint32_t format,
                 const void *buffer);
  void writeParameter(Vst::ParamID id, double norm);
  bool resizeTo(int width, int height);

private:
  void applySizeHints();

  ComponentHandler mHandler;
  Frame mFrame;
  LV2UI_Write_Function mWrite = nullptr;
  LV2UI_Controller mWriteController = nullptr;
  const LV2UI_Resize *mResize = nullptr;

  IPtr<Controller> mController;
  IPtr<BbmView> mView;
  // A connection of our own, used only to set size hints on the editor's window
  // (window ids are server-global; the view does not share its Display).
  std::unique_ptr<::Display, DisplayCloser> mHintDisplay;
};

tresult PLUGIN_API ComponentHandler::performEdit(Vst::ParamID id, Vst::ParamValue value) {
  mOwner.writeParameter(id, value);
  return kResultOk;
}

tresult PLUGIN_API Frame::resizeView(IPlugView *view, ViewRect *newSize) {
  if (view == nullptr || newSize == nullptr)
    return kInvalidArgument;
  if (!mOwner.resizeTo(newSize->getWidth(), newSize->getHeight()))
    return kResultFalse;
  // The SDK's sequence: the host resizes, then calls onSize in this same call.
  view->onSize(newSize);
  return kResultTrue;
}

bool Ui::open(::Window parent, const LV2UI_Resize *resize) {
  mResize = resize;
  // The SDK's reference counts own both objects from here (owned() adopts).
  mController =
      owned(new (std::nothrow) Controller()); // NOLINT(cppcoreguidelines-owning-memory)
  if (!mController || mController->initialize(nullptr) != kResultOk)
    return false;
  mController->setComponentHandler(&mHandler);

  // Made directly rather than through createView(), because the LV2UI_Widget handed
  // back is the view's own X window id, which IPlugView* cannot say. It is the same
  // object createView makes, and it registers with the controller on attach.
  // NOLINTBEGIN(cppcoreguidelines-owning-memory)
  mView = owned(new (std::nothrow) BbmView(mController));
  // NOLINTEND(cppcoreguidelines-owning-memory)
  if (!mView)
    return false;
  mView->setFrame(&mFrame);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
  void *parentPtr = reinterpret_cast<void *>(static_cast<std::uintptr_t>(parent));
  if (mView->attached(parentPtr, kPlatformTypeX11EmbedWindowID) != kResultOk)
    return false;

  // Not fatal without it: the host just does not learn the editor's limits.
  mHintDisplay.reset(XOpenDisplay(nullptr));
  applySizeHints();

  ViewRect size;
  mView->getSize(&size);
  if (mResize != nullptr && mResize->ui_resize != nullptr)
    mResize->ui_resize(mResize->handle, size.getWidth(), size.getHeight());
  return true;
}

void Ui::close() {
  if (mView) {
    if (mView->nativeWindow() != 0)
      mView->removed();
    mView->setFrame(nullptr);
    mView = nullptr;
  }
  if (mController) {
    mController->setComponentHandler(nullptr);
    mController->terminate();
    mController = nullptr;
  }
  mHintDisplay.reset();
}

// State the editor's limits where an LV2 host looks for them: suil reads the
// child's XSizeHints (x11_in_gtk3.c, forward_size_request) and clamps its
// allocation to them. The limits are probed from the editor's own
// checkSizeConstraint, the same rule the VST3 host asks.
void Ui::applySizeHints() {
  if (!mHintDisplay || !mView || mView->nativeWindow() == 0)
    return;
  ViewRect small(0, 0, 1, 1);
  ViewRect large(0, 0, 100000, 100000);
  if (mView->checkSizeConstraint(&small) != kResultTrue ||
      mView->checkSizeConstraint(&large) != kResultTrue)
    return;
  XSizeHints hints{};
  hints.flags = PMinSize | PMaxSize | PAspect;
  hints.min_width = small.getWidth();
  hints.min_height = small.getHeight();
  hints.max_width = large.getWidth();
  hints.max_height = large.getHeight();
  hints.min_aspect.x = hints.max_aspect.x = large.getWidth();
  hints.min_aspect.y = hints.max_aspect.y = large.getHeight();
  XSetWMNormalHints(mHintDisplay.get(), mView->nativeWindow(), &hints);
  XFlush(mHintDisplay.get());
}

bool Ui::resizeTo(int width, int height) {
  if (width <= 0 || height <= 0)
    return false;
  applySizeHints();
  if (mResize != nullptr && mResize->ui_resize != nullptr)
    mResize->ui_resize(mResize->handle, width, height);
  return true;
}

void Ui::writeParameter(Vst::ParamID id, double norm) {
  if (mWrite == nullptr)
    return;
  if (id == kBypassId) {
    const float enabled = enabledFromBypass(norm);
    mWrite(mWriteController, kPortEnabled, sizeof enabled, 0, &enabled);
    return;
  }
  if (id >= static_cast<Vst::ParamID>(kParamCount))
    return;
  const auto plain = static_cast<float>(toPlain(kParams.at(id), norm));
  mWrite(mWriteController, kPortControlFirst + id, sizeof plain, 0, &plain);
}

void Ui::portEvent(std::uint32_t port, std::uint32_t size, std::uint32_t format,
                   const void *buffer) {
  // format 0 is ui:floatProtocol: one float. Host data is untrusted, so the size is
  // checked before the payload is read.
  if (!mController || buffer == nullptr || format != 0 || size != sizeof(float))
    return;
  float v = 0.0f;
  std::memcpy(&v, buffer, sizeof v);
  if (isControlPort(port)) {
    const ParamId id = controlParam(port);
    mController->setParamNormalized(id, toNorm(kParams.at(id), static_cast<double>(v)));
  } else if (port == kPortEnabled) {
    mController->setParamNormalized(kBypassId, bypassFromEnabled(v));
  }
}

//------------------------------------------------------------------------
// The LV2 UI entry points.
Ui *self(LV2UI_Handle h) {
  return static_cast<Ui *>(h);
}

LV2UI_Handle uiInstantiate(const LV2UI_Descriptor * /*d*/, const char *pluginUri,
                           const char * /*bundlePath*/, LV2UI_Write_Function write,
                           LV2UI_Controller controller, LV2UI_Widget *widget,
                           const LV2_Feature *const *features) {
  if (widget == nullptr || pluginUri == nullptr ||
      std::string_view(pluginUri) != kPluginUri)
    return nullptr;
  *widget = nullptr;

  ::Window parent = 0;
  const LV2UI_Resize *resize = nullptr;
  for (int i = 0; features != nullptr && features[i] != nullptr; ++i) {
    const std::string_view uri =
        features[i]->URI != nullptr ? features[i]->URI : std::string_view();
    if (uri == LV2_UI__parent)
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      parent = static_cast<::Window>(reinterpret_cast<std::uintptr_t>(features[i]->data));
    else if (uri == LV2_UI__resize)
      resize = static_cast<const LV2UI_Resize *>(features[i]->data);
  }
  if (parent == 0)
    return nullptr;

  // Any failure returns NULL, so the host falls back to its own generic controls.
  auto ui = std::unique_ptr<Ui>(new (std::nothrow) Ui(write, controller));
  if (!ui || !ui->open(parent, resize) || ui->widget() == 0)
    return nullptr;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
  *widget = reinterpret_cast<LV2UI_Widget>(static_cast<std::uintptr_t>(ui->widget()));
  return ui.release(); // owned by the host until uiCleanup
}

void uiCleanup(LV2UI_Handle h) {
  const std::unique_ptr<Ui> owner(self(h));
}

void uiPortEvent(LV2UI_Handle h, std::uint32_t port, std::uint32_t size,
                 std::uint32_t format, const void *buffer) {
  if (Ui *ui = self(h))
    ui->portEvent(port, size, format, buffer);
}

int uiIdle(LV2UI_Handle h) {
  Ui *ui = self(h);
  return ui != nullptr ? ui->idle() : 1;
}

const LV2UI_Idle_Interface kIdleInterface = {uiIdle};

const void *uiExtensionData(const char *uri) {
  if (uri != nullptr && std::string_view(uri) == LV2_UI__idleInterface)
    return &kIdleInterface;
  return nullptr;
}

const LV2UI_Descriptor kUiDescriptor = {
    kUiUri, uiInstantiate, uiCleanup, uiPortEvent, uiExtensionData,
};

} // namespace
} // namespace bbm::lv2

extern "C" {
LV2_SYMBOL_EXPORT const LV2UI_Descriptor *lv2ui_descriptor(std::uint32_t index) {
  return index == 0 ? &bbm::lv2::kUiDescriptor : nullptr;
}
}
