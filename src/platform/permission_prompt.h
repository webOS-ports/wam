// Copyright 2022 LG Electronics, Inc.
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

#ifndef PLATFORM_PERMISSION_PROMPT_H_
#define PLATFORM_PERMISSION_PROMPT_H_

#include <string>

#include "neva/app_runtime/public/permission_prompt.h"

class ApplicationDescription;

using PermissionRequest = neva_app_runtime::PermissionRequest;

class PermissionPrompt : public neva_app_runtime::PermissionPrompt {
 public:
  explicit PermissionPrompt(
      neva_app_runtime::PermissionPrompt::Delegate* delegate);
  ~PermissionPrompt() override;

  // Resolves the prompt shown as |prompt_id| with the user's answer. Returns
  // false when no such prompt is waiting, e.g. the page went away first.
  static bool Answer(const std::string& prompt_id, bool allow);

  // Whether |app_desc|'s appinfo.json grants |type| without asking. For
  // location only an app installed with the system may do that: a store app
  // could otherwise grant itself the user's position.
  static bool GrantedByAppInfo(const ApplicationDescription& app_desc,
                               PermissionRequest::RequestType type);

  // Settings > Location "Location for Applications" (systemservice preference
  // autoLocate). While off, every location request that reaches the prompt is
  // refused without asking. Main thread only, like the rest of this class.
  static void SetLocationEnabled(bool enabled);

 private:
  void Show(PermissionRequest::RequestType type);
  void SetDecisions();
  bool GetPermissionStatusFromAppDesc(PermissionRequest::RequestType type);
  neva_app_runtime::PermissionPrompt::Delegate* delegate_;
  // Set while the question is on screen.
  std::string prompt_id_;
};

#endif  // PLATFORM_PERMISSION_PROMPT_H_
