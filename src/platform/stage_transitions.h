// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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

#ifndef PLATFORM_STAGE_TRANSITIONS_H_
#define PLATFORM_STAGE_TRANSITIONS_H_

#include <functional>

#include "webos/common/webos_constants.h"

// Runs a web app's stage transitions one at a time.
//
// A transition - activating or deactivating the stage - calls into the web
// runtime, and the runtime can report a new window state before the call
// returns: it turns the message loop, or, when Show() rebuilds the native
// window of a hidden keepAlive app, the new toplevel reports its state while
// the widget is still being initialised. A second transition started from
// there runs against the half-applied first one and the process goes down
// inside the runtime.
//
// So a transition asked for while one is running is not started. Its state is
// remembered, and applied once the running one has finished: it is the newer
// truth about the window, and dropping it would leave WAM believing something
// the shell no longer does. Only the newest such state is kept. They are
// applied in a loop rather than recursively, so a chain of them cannot rebuild
// the stack that crashed; and not once the app is closing.
class StageTransitions {
 public:
  using ApplyState = std::function<void(webos::NativeWindowState)>;
  using IsClosing = std::function<bool()>;

  StageTransitions(ApplyState apply_state, IsClosing is_closing);

  // Runs |transition| now, or, if a transition is running, applies |state|
  // after it instead. Returns false when it was deferred.
  bool Run(webos::NativeWindowState state,
           const std::function<void()>& transition);

  bool InTransition() const { return in_transition_; }

 private:
  ApplyState apply_state_;
  IsClosing is_closing_;
  bool in_transition_ = false;
  bool has_pending_state_ = false;
  webos::NativeWindowState pending_state_ = webos::NATIVE_WINDOW_DEFAULT;
};

#endif  // PLATFORM_STAGE_TRANSITIONS_H_
