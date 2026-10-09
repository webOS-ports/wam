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

#include "platform/permission_prompt.h"

#include <map>
#include <string>

#include "core/application_description.h"
#include "core/notification_service.h"
#include "core/web_app_base.h"
#include "core/web_app_manager.h"
#include "util/log_manager.h"

namespace {
const char* PermissionRequestTypeToString(PermissionRequest::RequestType type) {
  switch (type) {
    case PermissionRequest::RequestType::kNotifications:
      return "notifications";
    case PermissionRequest::RequestType::kGeolocation:
      return "geolocation";
    default:
      return nullptr;
  }
}

// Prompts waiting for the user, by prompt id.
std::map<std::string, PermissionPrompt*>& PendingPrompts() {
  static auto* prompts = new std::map<std::string, PermissionPrompt*>();
  return *prompts;
}

ApplicationDescription const* FindAppDescription(const std::string& app_id) {
  WebAppBase const* app = WebAppManager::Instance()->FindAppById(app_id);
  return app ? app->GetAppDescription() : nullptr;
}
}  // namespace

PermissionPrompt::PermissionPrompt(
    neva_app_runtime::PermissionPrompt::Delegate* delegate)
    : delegate_(delegate) {
  SetDecisions();
}

PermissionPrompt::~PermissionPrompt() {
  // The request was dropped (page closed or navigated) before the user
  // answered, so take the question off the screen.
  if (!prompt_id_.empty()) {
    PendingPrompts().erase(prompt_id_);
    NotificationService::Instance()->ClosePermissionPrompt(prompt_id_);
  }
}

// static
bool PermissionPrompt::Answer(const std::string& prompt_id, bool allow) {
  auto it = PendingPrompts().find(prompt_id);
  if (it == PendingPrompts().end()) {
    return false;
  }

  PermissionPrompt* prompt = it->second;
  PendingPrompts().erase(it);
  prompt->prompt_id_.clear();
  // The alert is gone already; this only forgets its id.
  NotificationService::Instance()->ClosePermissionPrompt(prompt_id);

  LOG_INFO(MSGID_SET_PERMISSION, 2,
           PMLOGKS("APP_ID", prompt->delegate_->GetAppId().c_str()),
           PMLOGKS("PERMISSION_STATUS", (allow ? "granted" : "denied")),
           "answered by the user");
  // Either call may delete |prompt|.
  neva_app_runtime::PermissionPrompt::Delegate* delegate = prompt->delegate_;
  if (allow) {
    delegate->Accept();
  } else {
    delegate->Deny();
  }
  return true;
}

void PermissionPrompt::Show(PermissionRequest::RequestType type) {
  static unsigned next_prompt = 0;
  const std::string app_id = delegate_->GetAppId();
  prompt_id_ = app_id + "-" + std::to_string(++next_prompt);

  ApplicationDescription const* app_desc = FindAppDescription(app_id);
  const std::string app_title =
      app_desc && !app_desc->Title().empty() ? app_desc->Title() : app_id;

  LOG_INFO(MSGID_SET_PERMISSION, 2, PMLOGKS("APP_ID", app_id.c_str()),
           PMLOGKS("PERMISSION_STATUS", "asking"), "");
  PendingPrompts()[prompt_id_] = this;
  // Worded as the QtWebEngine era's permission dialog was.
  const bool location = type == PermissionRequest::RequestType::kGeolocation;
  if (!NotificationService::Instance()->ShowPermissionPrompt(
          prompt_id_, location ? "Location Services" : "Notifications",
          app_title + (location ? " wants to access your location."
                                : " wants to show notifications."))) {
    // Nobody can be asked, so leave the permission undecided.
    PendingPrompts().erase(prompt_id_);
    prompt_id_.clear();
    delegate_->Closing();
  }
}

void PermissionPrompt::SetDecisions() {
  LOG_DEBUG("PermissionPrompt::SetDecisions");
  // The delegate takes one decision for all of its requests, and may delete
  // this prompt while doing so; work out the answer before giving it.
  bool known = false;
  bool allow = true;
  bool ask = false;
  PermissionRequest::RequestType ask_type =
      PermissionRequest::RequestType::kInvalid;
  bool mixed_ask = false;
  for (const PermissionRequest* request : delegate_->Requests()) {
    PermissionRequest::RequestType const type = request->GetRequestType();
    switch (type) {
      case PermissionRequest::RequestType::kCameraStream:
      case PermissionRequest::RequestType::kMicStream:
        // Capture stays an appinfo.json decision: an app that does not
        // declare it may not use it.
        known = true;
        allow = allow && GetPermissionStatusFromAppDesc(type);
        break;
      case PermissionRequest::RequestType::kNotifications:
      case PermissionRequest::RequestType::kGeolocation:
        // appinfo.json can grant these up front; otherwise the user is asked.
        known = true;
        if (!GetPermissionStatusFromAppDesc(type)) {
          // One question covers one kind of access: the answer is applied to
          // every request, so it must not grant something it did not name.
          if (ask && ask_type != type)
            mixed_ask = true;
          ask = true;
          ask_type = type;
        }
        break;
      default:
        // Refused rather than ignored: left alone, a request nobody answers
        // keeps the page waiting for good, and in a group with an allowed one
        // it would be granted along with it.
        LOG_ERROR(MSGID_ERROR_ERROR, 0,
                  "There is no matching permission type.");
        known = true;
        allow = false;
    }
  }

  if (!known) {
    return;
  }
  if (mixed_ask) {
    LOG_ERROR(MSGID_ERROR_ERROR, 0,
              "One prompt cannot ask for different kinds of access.");
    delegate_->Closing();
    return;
  }
  if (!allow) {
    delegate_->Deny();
  } else if (ask) {
    Show(ask_type);
  } else {
    delegate_->Accept();
  }
}

bool PermissionPrompt::GetPermissionStatusFromAppDesc(
    PermissionRequest::RequestType type) {
  const std::string app_id = delegate_->GetAppId();
  ApplicationDescription const* app_desc = FindAppDescription(app_id);
  if (!app_desc) {
    return false;
  }

  bool status = false;
  switch (type) {
    case PermissionRequest::RequestType::kCameraStream: {
      status = app_desc->AllowVideoCapture();
    } break;
    case PermissionRequest::RequestType::kMicStream: {
      status = app_desc->AllowAudioCapture();
    } break;
    default: {
      auto& permissions = app_desc->WebAppPermissions();
      const char* str_type = PermissionRequestTypeToString(type);
      if (str_type != nullptr) {
        status = permissions.contains(str_type);
      }
    } break;
  }
  LOG_INFO(MSGID_SET_PERMISSION, 2, PMLOGKS("APP_ID", app_id.c_str()),
           PMLOGKS("PERMISSION_STATUS", (status ? "granted" : "denied")),
           "by appinfo.json");
  return status;
}
