// Copyright (c) 2023 LG Electronics, Inc.
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

#include "notification_service_luna.h"

namespace {

Json::Value Callback(const std::string& notification_id,
                     std::map<std::string, LSCalloutContext>& contexts,
                     std::map<std::string, std::string>& ids,
                     const std::string& name,
                     const Json::Value& reply) {
  if (reply.isObject() && reply["returnValue"].isBool() &&
      reply["returnValue"].asBool() && reply[name].isString()) {
    ids[notification_id] = reply[name].asString();
  }
  contexts.erase(notification_id);
  return Json::Value();
}

std::map<std::string, LSCalloutContext> toast_contexts;
std::map<std::string, std::string> toast_ids;
std::map<std::string, LSCalloutContext> prompt_contexts;
std::map<std::string, std::string> prompt_ids;

}  // namespace

// static
NotificationService* NotificationService::Instance() {
  return NotificationServiceLuna::Instance();
}

// static
NotificationServiceLuna* NotificationServiceLuna::Instance() {
  // Never destroyed: pages may still close their notifications while the
  // process exits.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  static NotificationServiceLuna* instance = new NotificationServiceLuna();
  return instance;
}

NotificationServiceLuna::NotificationServiceLuna() {
  StartService();
}

void NotificationServiceLuna::DidConnect() {}

LSMethod* NotificationServiceLuna::Methods() const {
  return nullptr;
}

const char* NotificationServiceLuna::ServiceName() const {
  return "com.webos.notification.client";
}

bool NotificationServiceLuna::Display(const NotificationData& notification) {
  // Close the previous notification with the same tag, if possible.
  Close(notification.id);
  // The LuneOS shell shows toasts, in its banner and notification area;
  // alerts are only drawn by the OSE notification app, which it never shows.
  return CreateToast(notification);
}

bool NotificationServiceLuna::CreateToast(
    const NotificationData& notification) {
  Json::Value toast_params;
  // notificationmgr refuses a toast without a message, and a web
  // notification needs only a title.
  toast_params["message"] =
      notification.message.empty() ? notification.title : notification.message;
  if (!notification.message.empty()) {
    toast_params["title"] = notification.title;
  }
  if (!notification.icon.empty()) {
    toast_params["iconUrl"] = notification.icon;
  }
  toast_params["sourceId"] = notification.app_id;
  // Tapping the toast brings the app that made it to the front.
  toast_params["onclick"]["appId"] = notification.app_id;

  toast_contexts.emplace(
      notification.id,
      LSCalloutContext(std::bind(Callback, notification.id,
                                 std::ref(toast_contexts), std::ref(toast_ids),
                                 std::cref("toastId"), std::placeholders::_1)));
  return Call("luna://com.webos.notification/createToast", toast_params,
              notification.app_id.c_str(), &toast_contexts.at(notification.id));
}

bool NotificationServiceLuna::Close(const std::string& notification_id) {
  return Close(notification_id, toast_ids, "toastId",
               "luna://com.webos.notification/closeToast");
}

bool NotificationServiceLuna::ShowPermissionPrompt(
    const std::string& prompt_id,
    const std::string& title,
    const std::string& message) {
  Json::Value params;
  params["promptId"] = prompt_id;

  // ok and cancel are notificationmgr's button types; the shell draws them
  // green and red.
  Json::Value alert_buttons;
  Json::Value allow_button;
  allow_button["label"] = "Allow";
  allow_button["buttonType"] = "ok";
  allow_button["onclick"] =
      "luna://com.webos.service.webappmanager/answerPermissionPrompt";
  allow_button["params"] = params;
  allow_button["params"]["allow"] = true;
  alert_buttons.append(allow_button);

  Json::Value deny_button;
  deny_button["label"] = "Deny";
  deny_button["buttonType"] = "cancel";
  deny_button["onclick"] =
      "luna://com.webos.service.webappmanager/answerPermissionPrompt";
  deny_button["params"] = params;
  deny_button["params"]["allow"] = false;
  alert_buttons.append(deny_button);

  Json::Value alert_params;
  alert_params["buttons"] = alert_buttons;
  alert_params["title"] = title;
  alert_params["message"] = message;
  alert_params["modal"] = true;

  prompt_contexts.emplace(
      prompt_id,
      LSCalloutContext(std::bind(Callback, prompt_id, std::ref(prompt_contexts),
                                 std::ref(prompt_ids), std::cref("alertId"),
                                 std::placeholders::_1)));
  // Sent as WAM, not as the app: notificationmgr checks the button uris
  // against the caller, and only WAM's client may answer a prompt.
  return Call("luna://com.webos.notification/createAlert", alert_params,
              nullptr, &prompt_contexts.at(prompt_id));
}

bool NotificationServiceLuna::ClosePermissionPrompt(
    const std::string& prompt_id) {
  return Close(prompt_id, prompt_ids, "alertId",
               "luna://com.webos.notification/closeAlert");
}

bool NotificationServiceLuna::Close(const std::string& notification_id,
                                    std::map<std::string, std::string>& map,
                                    const std::string& name,
                                    const std::string& uri) {
  if (!map.contains(notification_id)) {
    return false;
  }

  std::string const id = map.at(notification_id);
  map.erase(notification_id);

  Json::Value close_params;
  close_params[name] = id;

  return Call(uri.c_str(), std::move(close_params));
}
