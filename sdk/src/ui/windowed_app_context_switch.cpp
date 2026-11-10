/**
 * @file        ui/windowed_app_context_switch.cpp
 * @brief       UI loop for Nintendo Switch on top of the libnx applet
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/ui/windowed_app_context_switch.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/window_switch.h>

#include <switch.h>

#include "rex/ui/switch_saltynx.h"

// usf4 0.6.5: on the user's console (2026-10-16), holding Capture to record a clip crashed the game, "too often".
// The game uses nearly all of the application memory (3281 of 3285 MB), and the system records with the running
// application's resources. This turns recording off the way games do for scenes they do not want recorded (the
// capture button then says the software does not allow it). Screenshots are not affected.
REXCVAR_DEFINE_BOOL(switch_video_recording, false, "Switch",
                    "Allow video clips (hold Capture). Off by default: recording crashed the game on the console");

// scudrace-nx (2026-11-10): closing the software from the home menu normally ends the process at once, so whatever a
// game saves only when it quits by itself is lost. With this on, the exit is locked (libnx appletLockExit: the system
// waits up to 15 s); a close request is then reported to the game (RexSwitchExitRequested), which saves and quits
// through its own path. If it has not quit within switch_close_grace_ms, the window is closed as before.
REXCVAR_DEFINE_BOOL(switch_close_saves, true, "Switch",
                    "Closing from the home menu first lets the game save and quit by itself");
REXCVAR_DEFINE_INT32(switch_close_grace_ms, 8000, "Switch",
                     "How long a close from the home menu waits for the game to quit by itself (ms)");

namespace rex::ui {

namespace {

std::atomic<bool> g_exit_requested{false};

std::atomic<bool> g_output_held{false};
// The one context, for RexSwitchReleaseOutput.
std::atomic<SwitchWindowedAppContext*> g_context{nullptr};

// Every source of work signals an event, so this only bounds how long a
// missed signal could go unnoticed.
constexpr u64 kWaitTimeoutNs = 100'000'000;

void AppletHookCallback(AppletHookType hook, void* param) {
  static_cast<SwitchWindowedAppContext*>(param)->OnAppletHook(int(hook));
}

const char* FocusStateName(AppletFocusState state) {
  switch (state) {
    case AppletFocusState_InFocus:
      return "in focus";
    case AppletFocusState_OutOfFocus:
      return "out of focus";
    case AppletFocusState_Background:
      return "in background";
    default:
      return "unknown focus";
  }
}

}  // namespace

struct SwitchWindowedAppContext::Impl {
  UEvent wakeup{};
  Event display_resolution_changed{};
  bool has_display_resolution_event = false;
  AppletHookCookie hook_cookie{};
  bool hooked = false;
  // The last effective mode seen. A Reverse-NX change is pure software: the applet does not
  // change operation mode, so neither the hook nor the resolution event ever fires and the
  // window would not notice. It is polled here, the only place that already loops on its own.
  bool effective_docked = false;
  bool effective_seen_docked = false;
  // switch_close_saves: the exit is locked; and when a close request came (0: none), armGetSystemTick.
  bool exit_locked = false;
  u64 exit_request_tick = 0;
};

SwitchWindowedAppContext::SwitchWindowedAppContext() : impl_(std::make_unique<Impl>()) {
  // Created here rather than in Initialize: other threads may post functions
  // as soon as the context exists.
  ueventCreate(&impl_->wakeup, true);
  g_context.store(this);
}

SwitchWindowedAppContext::~SwitchWindowedAppContext() {
  g_context.store(nullptr);
  // Execute leftover pending functions before the loop machinery goes away,
  // mirroring the shutdown contract documented in WindowedAppContext.
  ExecutePendingFunctionsFromUIThread();
  if (impl_->hooked) {
    appletUnhook(&impl_->hook_cookie);
  }
  if (impl_->has_display_resolution_event) {
    eventClose(&impl_->display_resolution_changed);
  }
  if (impl_->exit_locked) {
    // Before main returns (libnx); a pending close request then ends the process.
    appletUnlockExit();
    impl_->exit_locked = false;
  }
}

bool SwitchWindowedAppContext::Initialize() {
  if (appletGetAppletType() != AppletType_Application) {
    // Applet mode leaves ~400 MB, far from the guest's needs.
    REXLOG_WARN("Switch: not running as an application (applet type {}); launch through title "
                "takeover",
                int(appletGetAppletType()));
  }
  // HOME and sleep still suspend the process, but the focus change is reported
  // first, so painting stops before the display stack stops taking frames.
  Result rc = appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);
  if (R_FAILED(rc)) {
    REXLOG_WARN("Switch: appletSetFocusHandlingMode failed: 0x{:08X}", rc);
  }
  if (!REXCVAR_GET(switch_video_recording)) {
    rc = appletSetGamePlayRecordingState(false);
    if (R_FAILED(rc)) {
      REXLOG_WARN("Switch: could not turn video recording off: 0x{:08X}", rc);
    } else {
      REXLOG_INFO("Switch: video recording off (switch_video_recording = false)");
    }
  }
  if (REXCVAR_GET(switch_close_saves)) {
    rc = appletLockExit();
    impl_->exit_locked = R_SUCCEEDED(rc);
    if (!impl_->exit_locked) {
      REXLOG_WARN("Switch: appletLockExit failed: 0x{:08X}", rc);
    }
  }
  appletHook(&impl_->hook_cookie, AppletHookCallback, this);
  impl_->hooked = true;
  impl_->has_display_resolution_event =
      R_SUCCEEDED(appletGetDefaultDisplayResolutionChangeEvent(&impl_->display_resolution_changed));
  REXLOG_INFO("Switch: {} mode, {}",
              rex::ui::switch_saltynx::BaseMode(appletGetOperationMode() == AppletOperationMode_Console) ? "docked" : "handheld",
              FocusStateName(appletGetFocusState()));
  return true;
}

void SwitchWindowedAppContext::NotifyUILoopOfPendingFunctions() {
  ueventSignal(&impl_->wakeup);
}

void SwitchWindowedAppContext::PlatformQuitFromUIThread() {
  // RunMainMessageLoop re-checks HasQuitFromUIThread after every wakeup.
  NotifyUILoopOfPendingFunctions();
}

void SwitchWindowedAppContext::RegisterWindow(WindowSwitch* window) {
  if (!IsRegistered(window)) {
    windows_.push_back(window);
  }
}

void SwitchWindowedAppContext::UnregisterWindow(WindowSwitch* window) {
  windows_.erase(std::remove(windows_.begin(), windows_.end(), window), windows_.end());
}

bool SwitchWindowedAppContext::IsRegistered(const WindowSwitch* window) const {
  return std::find(windows_.begin(), windows_.end(), window) != windows_.end();
}

void SwitchWindowedAppContext::ReconnectWindowSurfaces() {
  const std::vector<WindowSwitch*> windows = windows_;
  for (WindowSwitch* window : windows) {
    if (IsRegistered(window)) {
      window->ReconnectSurface();
    }
  }
}

void SwitchWindowedAppContext::OnAppletHook(int hook_type) {
  switch (AppletHookType(hook_type)) {
    case AppletHookType_OnOperationMode:
      REXLOG_INFO("Switch: now {}",
                  rex::ui::switch_saltynx::BaseMode(appletGetOperationMode() == AppletOperationMode_Console) ? "docked" : "handheld");
      display_changed_ = true;
      break;
    case AppletHookType_OnFocusState:
      REXLOG_INFO("Switch: {}", FocusStateName(appletGetFocusState()));
      focus_changed_ = true;
      break;
    case AppletHookType_OnResume:
      REXLOG_INFO("Switch: resumed");
      focus_changed_ = true;
      break;
    case AppletHookType_OnExitRequest:
      REXLOG_INFO("Switch: exit requested{}",
                  impl_->exit_locked ? ": the game is asked to save and quit" : "");
      if (impl_->exit_locked && !impl_->exit_request_tick) {
        impl_->exit_request_tick = armGetSystemTick();
        g_exit_requested.store(true, std::memory_order_release);
      }
      break;
    default:
      break;
  }
}

void SwitchWindowedAppContext::ProcessQuitRequest() {
  // Use the normal close-request path rather than terminating the loop
  // directly. Window listeners use OnClosing to stop guest, audio and GPU
  // threads; bypassing it leaves the process hanging during teardown.
  if (quit_requested_) {
    return;
  }
  quit_requested_ = true;
  const std::vector<WindowSwitch*> windows = windows_;
  for (WindowSwitch* window : windows) {
    if (IsRegistered(window)) {
      window->HandleCloseRequest();
    }
  }
  if (windows_.empty() && !HasQuitFromUIThread()) {
    QuitFromUIThread();
  }
}

int SwitchWindowedAppContext::RunMainMessageLoop() {
  while (!HasQuitFromUIThread()) {
    s32 index = -1;
    if (impl_->has_display_resolution_event) {
      waitMulti(&index, kWaitTimeoutNs, waiterForUEvent(&impl_->wakeup),
                waiterForEvent(appletGetMessageEvent()),
                waiterForEvent(&impl_->display_resolution_changed));
    } else {
      waitMulti(&index, kWaitTimeoutNs, waiterForUEvent(&impl_->wakeup),
                waiterForEvent(appletGetMessageEvent()));
    }
    if (impl_->has_display_resolution_event &&
        R_SUCCEEDED(eventWait(&impl_->display_resolution_changed, 0))) {
      // Cleared explicitly: an event that stayed signaled would turn the wait
      // above into a busy loop.
      eventClear(&impl_->display_resolution_changed);
      display_changed_ = true;
    }
    // And the same if what changed is Reverse-NX, which signals no event.
    {
      const bool docked = rex::ui::switch_saltynx::BaseMode(
          appletGetOperationMode() == AppletOperationMode_Console);
      if (!impl_->effective_seen_docked) {
        impl_->effective_seen_docked = true;
        impl_->effective_docked = docked;
      } else if (docked != impl_->effective_docked) {
        impl_->effective_docked = docked;
        REXLOG_INFO("Switch: Reverse-NX now says {}: changing the window size",
                    docked ? "docked" : "handheld");
        display_changed_ = true;
      }
    }

    // Processes every queued applet message and runs the hooks.
    // With the exit locked, a close request first gives the game switch_close_grace_ms to quit by itself.
    if (!appletMainLoop() && !impl_->exit_request_tick) {
      ProcessQuitRequest();
    }
    if (impl_->exit_request_tick && !quit_requested_ &&
        armTicksToNs(armGetSystemTick() - impl_->exit_request_tick) / 1000000 >=
            u64(std::max(0, REXCVAR_GET(switch_close_grace_ms)))) {
      REXLOG_WARN("Switch: the game did not quit within {} ms of the close request: closing the window",
                  REXCVAR_GET(switch_close_grace_ms));
      ProcessQuitRequest();
    }

    ExecutePendingFunctionsFromUIThread();
    if (HasQuitFromUIThread()) {
      break;
    }

    const std::vector<WindowSwitch*> windows = windows_;
    if (display_changed_) {
      display_changed_ = false;
      for (WindowSwitch* window : windows) {
        if (IsRegistered(window)) {
          window->HandleDisplayModeChange();
        }
      }
    }
    if (focus_changed_) {
      focus_changed_ = false;
      const bool has_focus = appletGetFocusState() == AppletFocusState_InFocus;
      for (WindowSwitch* window : windows) {
        if (IsRegistered(window)) {
          window->HandleFocusChange(has_focus);
        }
      }
    }
    for (WindowSwitch* window : windows) {
      if (IsRegistered(window)) {
        window->HandlePendingPaint();
      }
    }
  }
  return EXIT_SUCCESS;
}

}  // namespace rex::ui

// True once the system asked the software to close (home menu) with switch_close_saves on: the game should save and
// quit by itself.
extern "C" bool RexSwitchExitRequested(void) {
  return rex::ui::g_exit_requested.load(std::memory_order_acquire);
}

extern "C" void RexSwitchHoldOutput(void) {
  rex::ui::g_output_held.store(true, std::memory_order_release);
}

extern "C" bool RexSwitchOutputHeld(void) {
  return rex::ui::g_output_held.load(std::memory_order_acquire);
}

namespace {
std::atomic<void (*)(void)> g_output_holder_stop{nullptr};
}  // namespace

extern "C" void RexSwitchSetOutputHolderStop(void (*stop)(void)) {
  g_output_holder_stop.store(stop, std::memory_order_release);
}

extern "C" void RexSwitchStopOutputHolder(void) {
  if (!RexSwitchOutputHeld()) {
    return;
  }
  if (void (*stop)(void) = g_output_holder_stop.load(std::memory_order_acquire)) {
    stop();
  }
}

extern "C" void RexSwitchReleaseOutput(void) {
  if (!rex::ui::g_output_held.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  // Windows opened while held have no surface. One opened after this makes its own; a pending call runs once the UI
  // loop does.
  if (rex::ui::SwitchWindowedAppContext* context = rex::ui::g_context.load()) {
    context->CallInUIThreadDeferred([context] { context->ReconnectWindowSurfaces(); });
  }
}
