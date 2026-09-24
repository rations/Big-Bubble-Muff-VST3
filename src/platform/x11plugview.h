// BigBubbleMuff — reusable base class for an X11-embedded VST 3 editor.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
// Adapted from the owner's rations-pedals src/platform/x11plugview.h (MIT). The
// runtime trace switch that project reads from the environment is gone: this
// plug-in probes no environment beyond what Xlib itself needs to find the display.
//
// Implements the kPlatformTypeX11EmbedWindowID contract from
// pluginterfaces/gui/iplugview.h: the host passes an X11 Window ID (as a void*) to
// IPlugView::attached(); the plug-in creates one child window inside it and paints
// that window itself. Coordinates are physical pixels.
//
// THE RUN LOOP. On Linux the plug-in owns no thread. The SDK's comment on
// Linux::IRunLoop: "On Linux the host has to provide this interface to the plug-in
// as there's no global event run loop defined as on other platforms." So this class
// asks IPlugFrame for Linux::IRunLoop, registers an IEventHandler on the X
// connection's file descriptor and an ITimerHandler at ~30 Hz, and unregisters both
// in removedFromParent() BEFORE tearing the window down.
//
// Painting is deferred: X events only ever set a dirty flag, and the draw happens on
// the next timer tick (or at once after a resize). Painting from an event handler is
// how an editor ends up re-entering the host's run loop.
//
// Drawing is double-buffered: subclasses paint into an offscreen image surface
// through onDraw(), and the result is blitted to the window in one step.
//
// RESIZING. CPluginView::canResize() returns kResultFalse and its
// checkSizeConstraint() is a stub, so a resizable editor overrides all three of
// canResize / checkSizeConstraint / onSize. A subclass opts in through isResizable()
// and states its rule in constrainSize(), which both checkSizeConstraint() and
// onSize() route through. Per the SDK, the X window is resized only from onSize().
//
// Subclasses do all windowing-dependent setup in onAttached(), never in their
// constructor, so createView() stays harmless in headless hosts (the validator
// creates and destroys views without attaching them).
#pragma once

#include "gfx/cairoptr.h"

#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <cairo/cairo.h>

#include <X11/Xlib.h>

namespace bbm {

// The SDK's interfaces (FUnknown and everything derived from it) deliberately have
// no virtual destructor: they are COM-style, and an object is only ever destroyed
// through release() on its most-derived type. EditorView's own base has the same
// shape; the warning fires here only because this class names two interfaces
// directly.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
class X11PlugView : public Steinberg::Vst::EditorView,
                    public Steinberg::Linux::IEventHandler,
                    public Steinberg::Linux::ITimerHandler {
public:
  explicit X11PlugView(Steinberg::Vst::EditController *editController,
                       Steinberg::ViewRect *size = nullptr);
  ~X11PlugView() override;

  X11PlugView(const X11PlugView &) = delete;
  X11PlugView &operator=(const X11PlugView &) = delete;
  X11PlugView(X11PlugView &&) = delete;
  X11PlugView &operator=(X11PlugView &&) = delete;

  //---from CPluginView-------------
  Steinberg::tresult PLUGIN_API
  isPlatformTypeSupported(Steinberg::FIDString type) override;
  Steinberg::tresult PLUGIN_API attached(void *parent,
                                         Steinberg::FIDString type) override;
  void attachedToParent() override;
  void removedFromParent() override;

  //---from IPlugView, resizing-----
  Steinberg::tresult PLUGIN_API canResize() override;
  Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect *rect) override;
  Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect *newSize) override;

  //---from Linux::IEventHandler----
  void PLUGIN_API onFDIsSet(Steinberg::Linux::FileDescriptor fd) override;

  //---from Linux::ITimerHandler----
  void PLUGIN_API onTimer() override;

  //---Interface--------------------
  OBJ_METHODS(X11PlugView, Steinberg::Vst::EditorView)
  DEFINE_INTERFACES
  DEF_INTERFACE(Steinberg::Linux::IEventHandler)
  DEF_INTERFACE(Steinberg::Linux::ITimerHandler)
  END_DEFINE_INTERFACES(Steinberg::Vst::EditorView)
  REFCOUNT_METHODS(Steinberg::Vst::EditorView)

protected:
  // --- subclass hooks, all called on the host's run-loop thread ---

  // The window and its drawing surfaces now exist. Load resources here.
  virtual void onAttached() {}
  // The window is about to go away. Release anything tied to it.
  virtual void onRemoved() {}

  // Paint the whole editor. `cr` targets the offscreen buffer.
  virtual void onDraw(cairo_t *cr) = 0;

  // Pointer input. Buttons are X button numbers (1 = left, 2 = middle, 3 = right).
  // Wheel steps arrive as delta +1 (up) or -1 (down). pointerModifiers() holds the
  // modifier state that came with the event being delivered.
  virtual void onMouseDown(int x, int y, int button) { (void)x, (void)y, (void)button; }
  virtual void onMouseUp(int x, int y, int button) { (void)x, (void)y, (void)button; }
  virtual void onMouseMove(int x, int y) { (void)x, (void)y; }
  virtual void onMouseLeave() {}
  virtual void onMouseWheel(int x, int y, int delta) { (void)x, (void)y, (void)delta; }

  // Keyboard, taken from the PLATFORM window rather than handed in by the host.
  // Return true if the key was consumed. See setKeyboardFocus() in the .cpp for why
  // this exists alongside IPlugView::onKeyDown. `key` is an ASCII character or 0;
  // `keyCode` a VirtualKeyCodes value (pluginterfaces/base/keycodes.h) or 0;
  // `modifiers` a KeyModifier mask — IPlugView::onKeyDown's own three arguments.
  virtual bool onKeyDownNative(Steinberg::char16 key, Steinberg::int16 keyCode,
                               Steinberg::int16 modifiers) {
    (void)key, (void)keyCode, (void)modifiers;
    return false;
  }

  // ~30 Hz, immediately before a repaint is considered.
  virtual void onTick() {}

  // --- resize policy, supplied by the subclass ---

  // Opt in to host resizing. Default is a fixed-size editor.
  virtual bool isResizable() const { return false; }
  // Adjust a proposed size in place to the nearest one the editor can draw. Called
  // for both the host's "may I?" and its "I did", so there is exactly one rule.
  virtual void constrainSize(int &w, int &h) const { (void)w, (void)h; }
  // The window and its surfaces are now `w` x `h` physical pixels.
  virtual void onResized(int w, int h) { (void)w, (void)h; }

  // Take the X input focus, or hand it straight back to whoever had it. Called by
  // the editor around a text field, and never held for longer than one.
  void setKeyboardFocus(bool wanted);

  // Request a repaint on the next tick. Cheap; call it freely.
  void invalidate() { mDirty = true; }

  bool isWindowOpen() const { return mWindow != 0; }

  // KeyModifier mask (kShiftKey, kCommandKey = Ctrl, kAlternateKey, kControlKey =
  // Super) of the pointer event currently being delivered.
  Steinberg::int16 pointerModifiers() const { return mPointerMods; }

private:
  bool openWindow(::Window parent);
  void closeWindow();
  bool resizeSurfaces(int w, int h);
  void drainEvents();
  void redraw();
  void mapWindow();
  void ensureMapped();

  ::Display *mDisplay = nullptr;
  ::Window mWindow = 0;
  // Only set when the parent had no colormap to share and we had to make one; a
  // borrowed parent colormap must not be freed.
  Colormap mOwnedColormap = None;
  Atom mXEmbedInfoAtom = None;
  SurfacePtr mTarget; // xlib surface for mWindow
  SurfacePtr mBuffer; // offscreen image surface

  Steinberg::IPtr<Steinberg::Linux::IRunLoop> mRunLoop;
  bool mEventHandlerRegistered = false;
  bool mTimerRegistered = false;
  bool mDirty = true;
  bool mMapped = false;
  int mTicksUnmapped = 0;
  Steinberg::int16 mPointerMods = 0;

  // Keyboard focus, held only while a text field is open. mPrevFocus is whatever
  // had the focus when we took it, so it can be given back.
  bool mKeyFocus = false;
  ::Window mPrevFocus = None;
  int mPrevRevert = RevertToParent;
};
#pragma GCC diagnostic pop

} // namespace bbm
