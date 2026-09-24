// BigBubbleMuff — X11PlugView implementation. See x11plugview.h for the run-loop
// contract.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Adapted from the owner's rations-pedals src/platform/x11plugview.cpp (MIT).
#include "platform/x11plugview.h"

#include "pluginterfaces/base/keycodes.h"

#include <cairo/cairo-xlib.h>

#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

using namespace Steinberg;

namespace bbm {

namespace {
// ~30 Hz repaint tick.
constexpr Linux::TimerInterval kTimerMs = 33;

// X wheel events arrive as presses of buttons 4 (up) and 5 (down).
constexpr unsigned int kWheelUp = 4;
constexpr unsigned int kWheelDown = 5;

// XEmbed (https://standards.freedesktop.org/xembed-spec/): _XEMBED_INFO is two
// CARD32s, {version, flags}.
constexpr unsigned long kXEmbedVersion = 0;

// How many timer ticks to wait for an embedder to map us before doing it ourselves
// (see ensureMapped).
constexpr int kMapFallbackTicks = 6; // ~200 ms at 33 ms

// One X keysym to one VirtualKeyCodes value (pluginterfaces/base/keycodes.h), or 0
// for a key that is carried by its character instead. Only the keys a text field
// needs are mapped.
int16 virtualKeyFromKeySym(KeySym sym) {
  switch (sym) {
  case XK_BackSpace:
    return KEY_BACK;
  case XK_Tab:
    return KEY_TAB;
  case XK_Return:
    return KEY_RETURN;
  case XK_KP_Enter:
    return KEY_ENTER;
  case XK_Escape:
    return KEY_ESCAPE;
  case XK_Delete:
  case XK_KP_Delete:
    return KEY_DELETE;
  case XK_Left:
  case XK_KP_Left:
    return KEY_LEFT;
  case XK_Right:
  case XK_KP_Right:
    return KEY_RIGHT;
  case XK_Up:
  case XK_KP_Up:
    return KEY_UP;
  case XK_Down:
  case XK_KP_Down:
    return KEY_DOWN;
  case XK_Home:
  case XK_KP_Home:
    return KEY_HOME;
  case XK_End:
  case XK_KP_End:
    return KEY_END;
  case XK_Page_Up:
  case XK_KP_Page_Up:
    return KEY_PAGEUP;
  case XK_Page_Down:
  case XK_KP_Page_Down:
    return KEY_PAGEDOWN;
  default:
    return 0;
  }
}

// X modifier state to a KeyModifier mask. keycodes.h documents kCommandKey as
// "Windows: ctrl key" and kControlKey as "Windows: win key", so ControlMask is
// kCommandKey and Mod4 (Super) is kControlKey.
int16 modifiersFromState(unsigned int state) {
  int mods = 0;
  if ((state & ShiftMask) != 0)
    mods |= kShiftKey;
  if ((state & ControlMask) != 0)
    mods |= kCommandKey;
  if ((state & Mod1Mask) != 0)
    mods |= kAlternateKey;
  if ((state & Mod4Mask) != 0)
    mods |= kControlKey;
  return static_cast<int16>(mods);
}

//------------------------------------------------------------------------
// Non-fatal X error handling.
//
// Xlib's default error handler calls exit(), so one BadWindow — a host that
// destroys its container before calling removed(), a stale id after a re-embed —
// would take the whole host down. This handler counts and returns instead.
//
// gOurDisplays, gPreviousErrorHandler and gHandlerInstalled are process-wide and
// guarded by gErrorMutex; the handler can be entered from any thread that makes an
// X call, which is why it takes the lock too. Errors on a display that is not ours
// go to whatever handler the host had installed.
//
// The handler is uninstalled again when the last of our displays goes: Xlib keeps
// ONE process-global handler pointer, and this code lives in a bundle the host may
// dlclose. A handler left installed across that unload is a pointer into unmapped
// memory.
std::mutex gErrorMutex;
std::vector<::Display *> gOurDisplays;
XErrorHandler gPreviousErrorHandler = nullptr;
bool gHandlerInstalled = false;

// Counts errors on our own connections. X requests are asynchronous, so a rejected
// CreateWindow does not fail in place: sample this, round-trip, sample again.
std::atomic<unsigned long> gErrorCount{0};

int xErrorHandler(::Display *display, XErrorEvent *event) {
  bool ours = false;
  XErrorHandler previous = nullptr;
  {
    const std::lock_guard<std::mutex> lock(gErrorMutex);
    ours = std::find(gOurDisplays.begin(), gOurDisplays.end(), display) !=
           gOurDisplays.end();
    previous = gPreviousErrorHandler;
  }
  if (!ours && previous != nullptr)
    return previous(display, event);
  gErrorCount.fetch_add(1);
  return 0;
}

void registerDisplay(::Display *display) {
  // XSetErrorHandler only swaps Xlib's global pointer and cannot re-enter the
  // handler, so holding the lock across it is safe.
  const std::lock_guard<std::mutex> lock(gErrorMutex);
  if (!gHandlerInstalled) {
    gPreviousErrorHandler = XSetErrorHandler(xErrorHandler);
    gHandlerInstalled = true;
  }
  gOurDisplays.push_back(display);
}

void unregisterDisplay(::Display *display) {
  const std::lock_guard<std::mutex> lock(gErrorMutex);
  const auto it = std::find(gOurDisplays.begin(), gOurDisplays.end(), display);
  if (it != gOurDisplays.end())
    gOurDisplays.erase(it);
  if (!gOurDisplays.empty() || !gHandlerInstalled)
    return;

  // Only if we are still the one installed: another library may have installed its
  // own handler after ours and be chaining to us. Xlib has no call that reads the
  // handler without setting it, so the set IS the read.
  const XErrorHandler current = XSetErrorHandler(gPreviousErrorHandler);
  if (current != &xErrorHandler) {
    XSetErrorHandler(current);
    return;
  }

  // And not if what we would put back has itself been unloaded (a sibling bundle
  // removed first): dladdr fails for an address in no loaded object. The fallback
  // is Xlib's default handler, the only choice that cannot jump into freed code.
  if (gPreviousErrorHandler != nullptr) {
    Dl_info info{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (dladdr(reinterpret_cast<void *>(gPreviousErrorHandler), &info) == 0)
      XSetErrorHandler(nullptr);
  }
  gHandlerInstalled = false;
  gPreviousErrorHandler = nullptr;
}

::Window parentWindow(void *systemWindow) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return static_cast<::Window>(reinterpret_cast<std::uintptr_t>(systemWindow));
}
} // namespace

//------------------------------------------------------------------------
X11PlugView::X11PlugView(Vst::EditController *editController, ViewRect *size)
    : Vst::EditorView(editController, size) {}

X11PlugView::~X11PlugView() {
  // removedFromParent() is the normal teardown path; this only covers a view
  // destroyed while still attached by a non-conforming host.
  closeWindow();
}

//------------------------------------------------------------------------
tresult PLUGIN_API X11PlugView::isPlatformTypeSupported(FIDString type) {
  if (type != nullptr && std::strcmp(type, kPlatformTypeX11EmbedWindowID) == 0)
    return kResultTrue;
  return kResultFalse;
}

// CPluginView::attached() always reports success. Report what actually happened, so
// a host told the attach failed can fall back to its generic parameter panel.
tresult PLUGIN_API X11PlugView::attached(void *parent, FIDString type) {
  const tresult result = CPluginView::attached(parent, type);
  if (result != kResultOk)
    return result;
  return isWindowOpen() ? kResultOk : kResultFalse;
}

//------------------------------------------------------------------------
bool X11PlugView::openWindow(::Window parent) {
  // The host does not share its Display connection, so open our own (Xlib finds
  // the display the host is on). Its file descriptor is registered with the run
  // loop in attachedToParent().
  mDisplay = XOpenDisplay(nullptr);
  if (mDisplay == nullptr)
    return false;
  registerDisplay(mDisplay);

  const int screen = DefaultScreen(mDisplay);
  const auto width = static_cast<unsigned>(std::max(1, rect.getWidth()));
  const auto height = static_cast<unsigned>(std::max(1, rect.getHeight()));

  // Inherit the parent's visual, depth and colormap rather than the screen
  // defaults. The X protocol, of CreateWindow's colormap: "If CopyFromParent is
  // specified ... the window must have the same visual type as the parent (or a
  // Match error results)". A host container with a 32-bit ARGB visual would
  // otherwise reject the window outright.
  ::Window colormapRoot = RootWindow(mDisplay, screen);
  Visual *visual = DefaultVisual(mDisplay, screen);
  int depth = DefaultDepth(mDisplay, screen);
  Colormap colormap = DefaultColormap(mDisplay, screen);

  XWindowAttributes parentAttrs{};
  if (XGetWindowAttributes(mDisplay, parent, &parentAttrs) != 0 &&
      parentAttrs.visual != nullptr) {
    visual = parentAttrs.visual;
    depth = parentAttrs.depth;
    colormap = parentAttrs.colormap;
    if (parentAttrs.root != 0)
      colormapRoot = parentAttrs.root;
  }

  // A parent with no colormap of its own cannot lend us one.
  if (colormap == None) {
    colormap = XCreateColormap(mDisplay, colormapRoot, visual, AllocNone);
    mOwnedColormap = colormap;
  }

  XSetWindowAttributes attrs{};
  attrs.background_pixmap = None; // we paint every pixel ourselves
  attrs.border_pixel = 0;         // required whenever our depth differs from the parent's
  attrs.colormap = colormap;
  // KeyPressMask is a deliberate departure from iplugview.h ("must not handle
  // keyboard events by the means of platform callbacks"): no host tested by the
  // owner ever calls IPlugView::onKeyDown. X delivers keys here only while this
  // window holds the input focus, which is taken only around an open text field
  // (setKeyboardFocus), so the host's own key commands are untouched.
  attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | LeaveWindowMask | StructureNotifyMask |
                     PropertyChangeMask | KeyPressMask | FocusChangeMask;

  const unsigned long errorsBefore = gErrorCount.load();
  mWindow =
      XCreateWindow(mDisplay, parent, 0, 0, width, height, 0, depth, InputOutput, visual,
                    CWBackPixmap | CWBorderPixel | CWColormap | CWEventMask, &attrs);

  // A rejected request surfaces later as a protocol error, never as a null id, so
  // round-trip once and check.
  XSync(mDisplay, False);
  if (mWindow == 0 || gErrorCount.load() != errorsBefore) {
    mWindow = 0; // never created, so there is nothing to destroy
    closeWindow();
    return false;
  }

  // Announce XEmbed support before the parent's connection can see the window. The
  // SDK's reference host reads it at CreateNotify and treats a missing property as
  // fatal. flags = 0, not XEMBED_MAPPED: under XEmbed the embedder owns mapping, and
  // ensureMapped() covers embedders that never get round to it.
  mXEmbedInfoAtom = XInternAtom(mDisplay, "_XEMBED_INFO", False);
  if (mXEmbedInfoAtom != None) {
    const std::array<unsigned long, 2> info{kXEmbedVersion, 0};
    // XChangeProperty takes format-32 data as bytes (Xlib's documented contract).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto *bytes = reinterpret_cast<const unsigned char *>(info.data());
    XChangeProperty(mDisplay, mWindow, mXEmbedInfoAtom, mXEmbedInfoAtom, 32,
                    PropModeReplace, bytes, 2);
  }
  XFlush(mDisplay);

  mTarget.reset(cairo_xlib_surface_create(
      mDisplay, mWindow, visual, static_cast<int>(width), static_cast<int>(height)));
  mBuffer.reset(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(width),
                                           static_cast<int>(height)));
  if (cairo_surface_status(mTarget.get()) != CAIRO_STATUS_SUCCESS ||
      cairo_surface_status(mBuffer.get()) != CAIRO_STATUS_SUCCESS) {
    closeWindow();
    return false;
  }
  return true;
}

//------------------------------------------------------------------------
// Take the X input focus for this window, or give it straight back. Focus is taken
// only while a text field is open and handed back to whoever held it the moment the
// field closes; outside that the host gets every key exactly as before.
void X11PlugView::setKeyboardFocus(bool wanted) {
  if (mDisplay == nullptr || mWindow == 0 || wanted == mKeyFocus)
    return;

  if (wanted) {
    // XSetInputFocus on a window that is not viewable is a BadMatch.
    XWindowAttributes attrs{};
    if (XGetWindowAttributes(mDisplay, mWindow, &attrs) == 0 ||
        attrs.map_state != IsViewable)
      return;
    ::Window focus = None;
    int revert = RevertToParent;
    XGetInputFocus(mDisplay, &focus, &revert);
    mPrevFocus = focus;
    mPrevRevert = revert;
    XSetInputFocus(mDisplay, mWindow, RevertToParent, CurrentTime);
    XFlush(mDisplay);
    mKeyFocus = true;
    return;
  }

  mKeyFocus = false;
  const ::Window prev = mPrevFocus;
  mPrevFocus = None;
  // PointerRoot and None are legal focus values and are handed back as they are; a
  // real window may have been destroyed meanwhile, so it is probed first.
  if (prev == PointerRoot || prev == None) {
    XSetInputFocus(mDisplay, prev, mPrevRevert, CurrentTime);
  } else if (prev != mWindow) {
    XWindowAttributes attrs{};
    if (XGetWindowAttributes(mDisplay, prev, &attrs) != 0)
      XSetInputFocus(mDisplay, prev, mPrevRevert, CurrentTime);
  }
  XFlush(mDisplay);
}

//------------------------------------------------------------------------
void X11PlugView::closeWindow() {
  // Never leave the focus pointed at a window that is about to stop existing.
  setKeyboardFocus(false);

  mBuffer.reset();
  mTarget.reset();
  if (mDisplay != nullptr) {
    if (mWindow != 0) {
      XDestroyWindow(mDisplay, mWindow);
      mWindow = 0;
    }
    if (mOwnedColormap != None) {
      XFreeColormap(mDisplay, mOwnedColormap);
      mOwnedColormap = None;
    }
    // Close first, then unregister: XCloseDisplay flushes and may report a teardown
    // error, which our handler must still be there to absorb. Afterwards the pointer
    // is only compared, never dereferenced.
    XCloseDisplay(mDisplay);
    unregisterDisplay(mDisplay);
    mDisplay = nullptr;
  }
  mWindow = 0;
  mMapped = false;
  mTicksUnmapped = 0;
}

//------------------------------------------------------------------------
// Grow/shrink the X window and both drawing surfaces to w x h. The xlib surface is
// told its new size in place; the offscreen buffer is rebuilt.
bool X11PlugView::resizeSurfaces(int w, int h) {
  if (mDisplay == nullptr || mWindow == 0 || w <= 0 || h <= 0)
    return false;

  XResizeWindow(mDisplay, mWindow, static_cast<unsigned>(w), static_cast<unsigned>(h));
  XFlush(mDisplay);
  if (mTarget)
    cairo_xlib_surface_set_size(mTarget.get(), w, h);

  SurfacePtr buffer(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h));
  if (cairo_surface_status(buffer.get()) != CAIRO_STATUS_SUCCESS)
    return false; // keep the old buffer: wrong size beats not painting at all
  mBuffer = std::move(buffer);
  return true;
}

//------------------------------------------------------------------------
void X11PlugView::attachedToParent() {
  const ::Window parent = parentWindow(systemWindow);
  if (parent == 0)
    return;
  if (mWindow == 0 && !openWindow(parent))
    return;

  // The run loop belongs to the host and reaches us through IPlugFrame.
  if (plugFrame != nullptr) {
    Linux::IRunLoop *runLoop = nullptr;
    // queryInterface's out-parameter is void** by the SDK's COM-style contract.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto **out = reinterpret_cast<void **>(&runLoop);
    if (plugFrame->queryInterface(Linux::IRunLoop::iid, out) == kResultTrue &&
        runLoop != nullptr)
      mRunLoop = owned(runLoop);
  }
  if (mRunLoop) {
    mEventHandlerRegistered =
        mRunLoop->registerEventHandler(this, ConnectionNumber(mDisplay)) == kResultTrue;
    mTimerRegistered = mRunLoop->registerTimer(this, kTimerMs) == kResultTrue;
  }

  onAttached();
  // The subclass caches art at device resolution, so it needs the current size
  // before the first paint.
  onResized(rect.getWidth(), rect.getHeight());
  mDirty = true;

  // Notify the controller (EditorView::attachedToParent -> editorAttached) only once
  // the window exists, so it may immediately push values in.
  Vst::EditorView::attachedToParent();
  redraw();
}

//------------------------------------------------------------------------
void X11PlugView::removedFromParent() {
  // Controller first, so it stops touching this view before anything is torn down.
  Vst::EditorView::removedFromParent();

  // Then the run loop: unregistering before the display goes is what stops the host
  // calling onFDIsSet() on a closed connection.
  if (mRunLoop) {
    if (mEventHandlerRegistered)
      mRunLoop->unregisterEventHandler(this);
    if (mTimerRegistered)
      mRunLoop->unregisterTimer(this);
  }
  mEventHandlerRegistered = false;
  mTimerRegistered = false;
  mRunLoop = nullptr;

  onRemoved();
  closeWindow();
}

//------------------------------------------------------------------------
tresult PLUGIN_API X11PlugView::canResize() {
  return isResizable() ? kResultTrue : kResultFalse;
}

// The host asking whether a size is acceptable. The adjusted rect is written back
// and kResultTrue returned whether or not it changed, as VSTGUI's
// VST3Editor::checkSizeConstraint does — the behaviour every host is tested with.
tresult PLUGIN_API X11PlugView::checkSizeConstraint(ViewRect *proposed) {
  if (proposed == nullptr)
    return kInvalidArgument;
  if (!isResizable())
    return kResultFalse;
  int w = proposed->getWidth();
  int h = proposed->getHeight();
  constrainSize(w, h);
  proposed->right = proposed->left + w;
  proposed->bottom = proposed->top + h;
  return kResultTrue;
}

// The host telling us it has resized the parent: the ONLY place the X window is
// resized ("please only resize the platform representation of the view when
// onSize() is called").
tresult PLUGIN_API X11PlugView::onSize(ViewRect *newSize) {
  if (newSize == nullptr)
    return kInvalidArgument;
  int w = newSize->getWidth();
  int h = newSize->getHeight();
  if (isResizable())
    constrainSize(w, h);

  const bool changed = (w != rect.getWidth() || h != rect.getHeight());
  rect = *newSize;
  rect.right = rect.left + w;
  rect.bottom = rect.top + h;

  if (changed && isWindowOpen()) {
    resizeSurfaces(w, h);
    onResized(w, h);
    mDirty = true;
    // Repaint now: during a drag the host shows our window at its new size at once.
    redraw();
  }
  return kResultTrue;
}

//------------------------------------------------------------------------
void X11PlugView::onFDIsSet(Linux::FileDescriptor /*fd*/) {
  drainEvents();
}

void X11PlugView::drainEvents() {
  if (mDisplay == nullptr)
    return;

  while (XPending(mDisplay) != 0) {
    XEvent event{};
    XNextEvent(mDisplay, &event);
    // XEvent is Xlib's tagged union; `type` selects the active member.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    if (event.xany.window != mWindow)
      continue;

    switch (event.type) {
    case Expose:
      // Coalesce: only the last expose in a burst needs a repaint, on the next tick.
      if (event.xexpose.count == 0)
        mDirty = true;
      break;

    case ButtonPress:
      mPointerMods = modifiersFromState(event.xbutton.state);
      if (event.xbutton.button == kWheelUp || event.xbutton.button == kWheelDown)
        onMouseWheel(event.xbutton.x, event.xbutton.y,
                     event.xbutton.button == kWheelUp ? 1 : -1);
      else
        onMouseDown(event.xbutton.x, event.xbutton.y,
                    static_cast<int>(event.xbutton.button));
      break;

    case ButtonRelease:
      mPointerMods = modifiersFromState(event.xbutton.state);
      if (event.xbutton.button != kWheelUp && event.xbutton.button != kWheelDown)
        onMouseUp(event.xbutton.x, event.xbutton.y,
                  static_cast<int>(event.xbutton.button));
      break;

    case MotionNotify: {
      // Compress motion: only the most recent position matters for a knob drag.
      XEvent latest = event;
      while (XPending(mDisplay) != 0) {
        XEvent peek{};
        XPeekEvent(mDisplay, &peek);
        if (peek.type != MotionNotify || peek.xany.window != mWindow)
          break;
        XNextEvent(mDisplay, &latest);
      }
      mPointerMods = modifiersFromState(latest.xmotion.state);
      onMouseMove(latest.xmotion.x, latest.xmotion.y);
      break;
    }

    case LeaveNotify:
      onMouseLeave();
      break;

    case KeyPress: {
      // XLookupString applies shift and lock for us and yields the Latin-1 byte.
      // The field being fed is ASCII only.
      XKeyEvent ke = event.xkey;
      std::array<char, 8> text{};
      KeySym sym = NoSymbol;
      const int n = XLookupString(&ke, text.data(), static_cast<int>(text.size() - 1),
                                  &sym, nullptr);
      const auto byte = static_cast<unsigned char>(n >= 1 ? text[0] : 0);
      const auto ch = static_cast<char16>((byte >= 0x20 && byte < 0x7F) ? byte : 0);
      if (onKeyDownNative(ch, virtualKeyFromKeySym(sym), modifiersFromState(ke.state)))
        mDirty = true;
      break;
    }

    case FocusOut:
      // Focus can be taken away by the host or window manager at any moment. Follow
      // reality, or a later release would steal it from whoever has it now.
      mKeyFocus = false;
      mPrevFocus = None;
      break;

    case ConfigureNotify: {
      // A host that resizes the parent without routing through onSize() still
      // reaches us here; follow it so the surfaces never disagree with the window.
      const int w = event.xconfigure.width;
      const int h = event.xconfigure.height;
      if (w > 0 && h > 0 && (w != rect.getWidth() || h != rect.getHeight())) {
        rect.right = rect.left + w;
        rect.bottom = rect.top + h;
        if (mTarget)
          cairo_xlib_surface_set_size(mTarget.get(), w, h);
        SurfacePtr buffer(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h));
        if (cairo_surface_status(buffer.get()) == CAIRO_STATUS_SUCCESS)
          mBuffer = std::move(buffer);
        onResized(w, h);
        mDirty = true;
      }
      break;
    }

    case MapNotify:
      mMapped = true;
      mDirty = true;
      break;

    case UnmapNotify:
      mMapped = false;
      break;

    case PropertyNotify:
      // Reaper sets _XEMBED_INFO on the plug-in's window rather than mapping it and
      // expects the plug-in to map itself (VSTGUI carries the same workaround,
      // flagged "needed for Reaper").
      if (mXEmbedInfoAtom != None && event.xproperty.atom == mXEmbedInfoAtom)
        mapWindow();
      break;

    default:
      break;
    }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
  }
}

//------------------------------------------------------------------------
void X11PlugView::onTimer() {
  ensureMapped();
  onTick();
  if (mDirty)
    redraw();
}

void X11PlugView::mapWindow() {
  if (mDisplay == nullptr || mWindow == 0 || mMapped)
    return;
  XMapWindow(mDisplay, mWindow);
  XFlush(mDisplay);
  mMapped = true;
  mDirty = true;
}

// Hosts differ on who maps the plug-in's window: a strict XEmbed embedder maps it,
// Reaper signals via _XEMBED_INFO, and some do neither. Waiting a few ticks and then
// mapping ourselves makes all three work.
void X11PlugView::ensureMapped() {
  if (mMapped || mDisplay == nullptr || mWindow == 0)
    return;
  if (++mTicksUnmapped < kMapFallbackTicks)
    return;
  mapWindow();
}

void X11PlugView::redraw() {
  if (!mBuffer || !mTarget || mDisplay == nullptr)
    return;
  mDirty = false;

  // Compose into the offscreen buffer...
  {
    const ContextPtr cr(cairo_create(mBuffer.get()));
    if (cairo_status(cr.get()) == CAIRO_STATUS_SUCCESS)
      onDraw(cr.get());
  }
  // ...then blit it to the window in one operation, so no partial frame shows.
  {
    const ContextPtr out(cairo_create(mTarget.get()));
    if (cairo_status(out.get()) == CAIRO_STATUS_SUCCESS) {
      cairo_set_operator(out.get(), CAIRO_OPERATOR_SOURCE);
      cairo_set_source_surface(out.get(), mBuffer.get(), 0.0, 0.0);
      cairo_paint(out.get());
    }
  }
  cairo_surface_flush(mTarget.get());
  XFlush(mDisplay);
}

} // namespace bbm
