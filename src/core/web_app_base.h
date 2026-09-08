// Copyright (c) 2008-2021 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_WEB_APP_BASE_H_
#define CORE_WEB_APP_BASE_H_

#include <cstdint>
#include <memory>
#include <string>

#include "web_app_manager.h"
#include "web_page_observer.h"

class ApplicationDescription;
// Whether the shell this runs under keeps a window on screen after it stops
// being the foreground one, from WAM_SHELL_KEEPS_DEACTIVATED_WINDOWS_SHOWN.
//
// It decides what happens by default when a window loses the stage. On a
// display where the foreground application is the only visible one, losing the
// stage means leaving the screen and suspending the page is right, which is
// what WAM has always done and what this returns false for. A card shell keeps
// deactivated windows on screen and has to keep painting them, so it sets the
// variable and the default flips.
//
// A default rather than something the shell states per window because getting
// it wrong in one direction blanks a card the user is looking at, and in the
// other merely leaves an application running that could have been suspended.
// The per-window call exists for a shell that can tell when a window really is
// covered; until it says otherwise this is the answer.
bool ShellKeepsDeactivatedWindowsShown();

class WebAppBasePrivate;
class WebPageBase;

namespace Json {
class Value;
}

class WebAppBase : public WebPageObserver {
 public:
  enum PreloadState : std::uint8_t {
    kNonePreload = 0,
    kFullPreload = 1,
    kSemiFullPreload = 2,
    kPartialPreload = 3,
    kMinimalPreload = 4
  };

  WebAppBase();
  ~WebAppBase() override;

  virtual void Attach(WebPageBase*);
  virtual WebPageBase* Detach();
  virtual void SuspendAppRendering() = 0;
  virtual void ResumeAppRendering() = 0;
  virtual bool IsFocused() const = 0;
  virtual void Resize(int width, int height) = 0;
  virtual bool IsActivated() const = 0;
  virtual bool IsMinimized() = 0;
  virtual bool IsNormal() = 0;
  virtual void OnStageActivated() = 0;
  virtual void OnStageDeactivated() = 0;
  virtual void StartLaunchTimer() {}
  virtual void SetHiddenWindow(bool hidden);
  virtual void ConfigureWindow(const std::string& type) = 0;
  // Window type this app was actually created with. Empty when the
  // platform doesn't track one; WebAppWayland overrides it.
  virtual std::string WindowType() const { return std::string(); }
  virtual void SetKeepAlive(bool keep_alive);
  virtual bool IsWindowed() const;
  virtual void Relaunch(const std::string& args,
                        const std::string& launching_app_id);
  virtual void SetWindowProperty(const std::string& name,
                                 const std::string& value) = 0;
  virtual void PlatformBack() = 0;
  virtual void SetCursor(const std::string& cursor_arg,
                         int hotspot_x,
                         int hotspot_y) = 0;
  virtual void SetInputRegion(const Json::Value& json_doc) = 0;
  virtual void SetKeyMask(const Json::Value& json_doc) = 0;
  virtual void Hide(bool forced_hide = false) = 0;
  virtual void Focus() = 0;
  virtual void Unfocus() = 0;
  virtual void SetOpacity(float opacity) = 0;
  virtual void SetAppDescription(std::unique_ptr<ApplicationDescription>);
  virtual void SetPreferredLanguages(const std::string& language);
  virtual void StagePreparing();
  virtual void StageReady();
  virtual void Raise() = 0;
  virtual void GoBackground() = 0;
  virtual void DoPendingRelaunch();
  virtual void DeleteSurfaceGroup() = 0;
  virtual void KeyboardVisibilityChanged(bool visible, int height);
  virtual void DoClose() = 0;
  virtual void SetUseVirtualKeyboard(const bool enable) = 0;
  virtual bool IsKeyboardVisible() { return false; }
  static void OnCursorVisibilityChanged(const std::string& jsscript);
  virtual bool HideWindow() = 0;

  // Whether this window is still on screen after it stopped being the
  // foreground one - a carded window in a card shell is. See
  // ShellKeepsDeactivatedWindowsShown() for where the initial value comes from;
  // a shell can correct it per window through
  // com.palm.webappmanager/setAppVisibility.
  bool IsShownWhileDeactivated() const { return shown_while_deactivated_; }
  void SetShownWhileDeactivated(bool shown) {
    shown_while_deactivated_ = shown;
  }
  virtual void SetDisplayFirstActivateTimeoutMs(uint32_t /*timeout*/) {}
  bool GetCrashState() const;
  void SetCrashState(bool state);
  bool GetHiddenWindow() const;
  bool KeepAlive() const;
  void SetForceClose();
  bool ForceClose();
  WebPageBase* Page() const;
  void HandleWebAppMessage(WebAppManager::WebAppMessageType type,
                           const std::string& message);
  void SetAppId(const std::string& app_id);
  void SetLaunchingAppId(const std::string& app_id);
  std::string AppId() const;
  std::string LaunchingAppId() const;
  void SetInstanceId(const std::string& instance_id);
  std::string InstanceId() const;
  std::string Url() const;

  ApplicationDescription* GetAppDescription() const;

  void SetAppProperties(const std::string& properties);

  void SetNeedReload(bool status) { need_reload_ = status; }
  bool NeedReload() { return need_reload_; }

  static int CurrentUiWidth();
  static int CurrentUiHeight();

  void ExecuteCloseCallback();
  void DispatchUnload();

  void SetUseAccessibility(bool enabled);
  void ServiceCall(const std::string& url,
                   const std::string& payload,
                   const std::string& app_id);

  void SetPreloadState(const std::string& properties);
  void ClearPreloadState();
  PreloadState GetPreloadState() const { return preload_state_; }

  bool IsClosing() const;
  bool IsCheckLaunchTimeEnabled();
  void SetClosePageRequested(bool requested) {
    close_page_requested_ = requested;
  }
  bool ClosePageRequested() const { return close_page_requested_; }

 protected:
  virtual void DoAttach() = 0;
  virtual void ShowWindow();

  void SetUiSize(int width, int height);
  void SetActiveInstanceId(const std::string& id);
  void ForceCloseAppInternal();
  void CloseAppInternal();
  void CloseWebApp();

  // WebPageObserver
  void CloseCallbackExecuted() override;
  void ClosingAppProcessDidCrashed() override;
  void DidDispatchUnload() override;
  void TimeoutExecuteCloseCallback() override;
  void WebPageClosePageRequested() override;
  void WebPageLoadFinished() override;

  PreloadState preload_state_ = kNonePreload;
  bool added_to_window_mgr_ = false;
  std::string in_progress_relaunch_params_;
  std::string in_progress_relaunch_launching_app_id_;
  float scale_factor_ = 1.0f;

  bool shown_while_deactivated_ = ShellKeepsDeactivatedWindowsShown();
 private:
  std::unique_ptr<WebAppBasePrivate> app_private_;
  bool need_reload_ = false;
  bool crashed_ = false;
  bool hidden_window_ = false;
  bool no_window_ = false;
  bool close_page_requested_ = false;  // window.close() is called once then
                                       // have to drop further requests
};

#endif  // CORE_WEB_APP_BASE_H_
