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

#include "stage_transitions.h"

#include <utility>

StageTransitions::StageTransitions(ApplyState apply_state, IsClosing is_closing)
    : apply_state_(std::move(apply_state)), is_closing_(std::move(is_closing)) {}

bool StageTransitions::Run(webos::NativeWindowState state,
                           const std::function<void()>& transition) {
  if (in_transition_) {
    pending_state_ = state;
    has_pending_state_ = true;
    return false;
  }

  in_transition_ = true;
  transition();
  in_transition_ = false;

  while (has_pending_state_) {
    const webos::NativeWindowState pending = pending_state_;
    has_pending_state_ = false;
    if (is_closing_()) {
      return true;
    }
    in_transition_ = true;
    apply_state_(pending);
    in_transition_ = false;
  }
  return true;
}
