// Copyright (c) 2015-2021 LG Electronics, Inc.
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

#include "web_app_manager_service_luna_impl.h"

#include "json/json.h"

#include "log_manager.h"
#include "utils.h"
#include "web_app_manager.h"

#define LS2_CALL(FUNC, SERVICE, PARAMS)                                    \
  Call<WebAppManagerServiceLunaImpl, &WebAppManagerServiceLunaImpl::FUNC>( \
      SERVICE, PARAMS, this)

WebAppManagerServiceLuna* WebAppManagerServiceLuna::Instance() {
  static WebAppManagerServiceLuna* service = new WebAppManagerServiceLunaImpl();
  return service;
}

void WebAppManagerServiceLunaImpl::SystemServiceConnectCallback(
    const Json::Value& reply) {
  WebAppManagerServiceLuna::SystemServiceConnectCallback(reply);

  if (reply.isObject() && reply.isMember("connected")) {
    Json::Value option_params;
    option_params["subscribe"] = true;
    option_params["category"] = "option";
    Json::Value option_list;
    option_list.append("country");
    option_list.append("smartServiceCountryCode3");
    option_list.append("audioGuidance");
    option_list.append("screenRotation");
    option_params["keys"] = std::move(option_list);
    LS2_CALL(GetSystemOptionCallback,
             "luna://com.webos.settingsservice/getSystemSettings",
             std::move(option_params));
  }
}

void WebAppManagerServiceLunaImpl::DidConnect() {
  WebAppManagerServiceLuna::DidConnect();

  // com.webos.service.ime is started on demand, so it is usually not up yet.
  // Registering for its status means the subscription happens whenever it
  // arrives, and again if maliit-server is restarted.
  Json::Value params;
  params["subscribe"] = true;
  params["serviceName"] = std::string("com.webos.service.ime");

  if (!LS2_CALL(ImeServiceConnectCallback,
                "luna://com.palm.lunabus/signal/registerServerStatus",
                std::move(params))) {
    LOG_WARNING(MSGID_SERVICE_CONNECT_FAIL, 0,
                "Failed to watch for the input method service");
  }
}

//! \brief Follows what the input method says about the physical keyboard.
//!
//! keyboardAvailable is read from procfs when WebAppMgr starts, because
//! applications read PalmSystem.deviceInfo as they launch and could not wait for
//! an answer over the bus. That leaves the case procfs was never consulted again
//! for: a keyboard plugged in or pulled off while the session runs, which on a
//! PineTab2 or with any USB or Bluetooth keyboard is ordinary.
//!
//! com.webos.service.ime already knows - it watches udev to decide whether to
//! show the on-screen keyboard at all - so this subscribes rather than polling
//! procfs on a timer.
void WebAppManagerServiceLunaImpl::ImeServiceConnectCallback(
    const Json::Value& reply) {
  if (!reply.isObject() || !reply.isMember("connected") ||
      !reply["connected"].asBool()) {
    return;
  }

  Json::Value params;
  params["subscribe"] = true;

  LS2_CALL(GetKeyboardStatusCallback,
           "luna://com.webos.service.ime/getKeyboardStatus", std::move(params));
}

void WebAppManagerServiceLunaImpl::GetKeyboardStatusCallback(
    const Json::Value& reply) {
  if (!reply.isObject() || !reply["returnValue"].asBool() ||
      !reply["hardwareKeyboard"].isObject()) {
    return;
  }

  const Json::Value& keyboard = reply["hardwareKeyboard"];

  // "present" is the physical fact - a keyboard is attached - and not whether
  // the on-screen keyboard is currently suppressed, which is what "usable"
  // carries. Applications want to know a keyboard exists.
  WebAppManager::Instance()->SetHardwareKeyboard(
      keyboard["present"].asBool(), keyboard["slider"].asBool());
}

Json::Value WebAppManagerServiceLunaImpl::setInspectorEnable(
    const Json::Value& /*request*/) {
  return util::StringToJson(R"({"returnValue": true})");
}

void WebAppManagerServiceLunaImpl::GetSystemOptionCallback(
    const Json::Value& reply) {
  const Json::Value& settings = reply["settings"];
  // The settings is empty when service is crashed
  // The right value will be notified again when service is restarted
  if (!reply.isObject() || !reply["settings"].isObject() ||
      reply["settings"].empty()) {
    LOG_WARNING(MSGID_RECEIVED_INVALID_SETTINGS, 1,
                PMLOGKFV("MSG", "%s", util::JsonToString(reply).c_str()), "");
    return;
  }
  LOG_INFO(MSGID_SETTING_SERVICE, 0,
           "Notified from settingsservice/getSystemSettings");

  std::string const country = reply["settings"]["country"].isString()
                            ? settings["country"].asString()
                            : "";
  std::string const smart_service_country =
      reply["settings"]["country"].isString()
          ? settings["smartServiceCountryCode3"].asString()
          : "";
  std::string const audio_guidance = reply["settings"]["country"].isString()
                                   ? settings["audioGuidance"].asString()
                                   : "";
  std::string const screen_rotation = reply["settings"]["country"].isString()
                                    ? settings["screenRotation"].asString()
                                    : "";

  LOG_INFO(
      MSGID_SETTING_SERVICE, 1,
      PMLOGKS("BroadcastCountry", country.empty() ? "Empty" : country.c_str()),
      "");
  LOG_INFO(MSGID_SETTING_SERVICE, 1,
           PMLOGKS("SmartServiceCountry", smart_service_country.empty()
                                              ? "Empty"
                                              : smart_service_country.c_str()),
           "");
  LOG_INFO(MSGID_SETTING_SERVICE, 1,
           PMLOGKS("AudioGuidance",
                   audio_guidance.empty() ? "Empty" : audio_guidance.c_str()),
           "");
  LOG_INFO(MSGID_SETTING_SERVICE, 1,
           PMLOGKS("ScreenRotation",
                   screen_rotation.empty() ? "Empty" : screen_rotation.c_str()),
           "");
  if (!country.empty()) {
    WebAppManagerService::SetDeviceInfo("LocalCountry", country.c_str());
  }
  if (!smart_service_country.empty()) {
    WebAppManagerService::SetDeviceInfo("SmartServiceCountry",
                                        smart_service_country.c_str());
  }
  if (!audio_guidance.empty()) {
    WebAppManagerService::SetAccessibilityEnabled(audio_guidance == "on");
  }
  if (!screen_rotation.empty()) {
    WebAppManagerService::SetDeviceInfo("ScreenRotation",
                                        screen_rotation.c_str());
  }
}
