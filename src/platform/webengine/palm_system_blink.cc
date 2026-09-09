// Copyright (c) 2014-2021 LG Electronics, Inc.
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

#include "json/json.h"

#include "application_description.h"
#include "log_manager.h"
#include "palm_system_blink.h"
#include "utils.h"
#include "web_app_base.h"
#include "web_app_wayland.h"
#include "web_page_blink.h"
#include "web_app_manager_service_luna.h"

namespace {

// LSCalloutContext cancels its call from its destructor, so a context created
// on the stack cancels the moment the calling function returns and its reply
// is never delivered. Keep banner contexts alive here, keyed by banner id -
// the same ownership pattern notification_service_luna.cc uses for its own
// createToast calls.
std::map<int, LSCalloutContext> banner_contexts;

const char* toStr(const bool value) {
  return value ? "true" : "false";
}

}  // namespace

PalmSystemBlink::PalmSystemBlink(WebAppBase* app) : PalmSystemWebOS(app) {}

std::string PalmSystemBlink::HandleBrowserControlMessage(
    const std::string& command,
    const std::vector<std::string>& arguments) {
  if (command == "initialize") {
    return util::JsonToString(Initialize());
  } else if (command == "country") {
    return Country();
  } else if (command == "locale") {
    return Locale();
  } else if (command == "localeRegion") {
    return LocaleRegion();
  } else if (command == "isMinimal") {
    return toStr(IsMinimal());
  } else if (command == "identifier") {
    return Identifier();
  } else if (command == "screenOrientation") {
    return ScreenOrientation();
  } else if (command == "currentCountryGroup") {
    return GetDeviceInfo("CountryGroup");
  } else if (command == "stageReady") {
    StageReady();
  } else if (command == "activate") {
    LOG_INFO(MSGID_PALMSYSTEM, 3, PMLOGKS("APP_ID", app_->AppId().c_str()),
             PMLOGKS("INSTANCE_ID", app_->InstanceId().c_str()),
             PMLOGKFV("PID", "%d", app_->Page()->GetWebProcessPID()),
             "webOSSystem.activate()");
    Activate();
  } else if (command == "deactivate") {
    LOG_INFO(MSGID_PALMSYSTEM, 3, PMLOGKS("APP_ID", app_->AppId().c_str()),
             PMLOGKS("INSTANCE_ID", app_->InstanceId().c_str()),
             PMLOGKFV("PID", "%d", app_->Page()->GetWebProcessPID()),
             "webOSSystem.deactivate()");
    Deactivate();
  } else if (command == "isActivated") {
    return toStr(IsActivated());
  } else if (command == "isKeyboardVisible") {
    return toStr(IsKeyboardVisible());
  } else if (command == "getIdentifier") {
    return Identifier();
  } else if (command == "launchParams") {
    LOG_INFO(MSGID_PALMSYSTEM, 3, PMLOGKS("APP_ID", app_->AppId().c_str()),
             PMLOGKS("INSTANCE_ID", app_->InstanceId().c_str()),
             PMLOGKFV("PID", "%d", app_->Page()->GetWebProcessPID()),
             "webOSSystem.launchParams Updated by app; %s",
             arguments[0].c_str());
    UpdateLaunchParams(arguments[0]);
  } else if (command == "keepAlive") {
    if (arguments.size() > 0) {
      SetKeepAlive(arguments[0] == "true");
    }
  } else if (command == "PmLogInfoWithClock") {
    if (arguments.size() == 3) {
      LogMsgWithClock(arguments[0], arguments[1], arguments[2]);
    }
  } else if (command == "PmLogString") {
    if (arguments.size() > 3) {
      int32_t v1 = 0;
      if (util::StrToInt(arguments[0], v1)) {
        LogMsgString(v1, arguments[1], arguments[2], arguments[3]);
      }
    }
  } else if (command == "setWindowProperty") {
    if (arguments.size() > 1) {
      LOG_INFO(MSGID_PALMSYSTEM, 3, PMLOGKS("APP_ID", app_->AppId().c_str()),
               PMLOGKS("INSTANCE_ID", app_->InstanceId().c_str()),
               PMLOGKFV("PID", "%d", app_->Page()->GetWebProcessPID()),
               "webOSSystem.window.setProperty('%s', '%s')",
               arguments[0].c_str(), arguments[1].c_str());
      app_->SetWindowProperty(arguments[0], arguments[1]);
    }
  } else if (command == "platformBack") {
    LOG_INFO(MSGID_PALMSYSTEM, 3, PMLOGKS("APP_ID", app_->AppId().c_str()),
             PMLOGKS("INSTANCE_ID", app_->InstanceId().c_str()),
             PMLOGKFV("PID", "%d", app_->Page()->GetWebProcessPID()),
             "webOSSystem.platformBack()");
    app_->PlatformBack();
  } else if (command == "setCursor") {
    if (arguments.size() == 3) {
      const std::string& v1 = arguments[0];
      int32_t v2 = 0, v3 = 0;
      const bool v2_conversion = util::StrToInt(arguments[1], v2);
      const bool v3_conversion = util::StrToInt(arguments[2], v3);
      if (v2_conversion && v3_conversion) {
        app_->SetCursor(v1, v2, v3);
      }
    }
  } else if (command == "setInputRegion") {
    std::string data;
    for (const auto& argument : arguments) {
      data.append(argument);
    }
    SetInputRegion(data);
  } else if (command == "setKeyMask") {
    std::string data;
    for (const auto& argument : arguments) {
      data.append(argument);
    }
    SetGroupClientEnvironment(kKeyMask, data);
  } else if (command == "focusOwner") {
    SetGroupClientEnvironment(kFocusOwner, std::string());
  } else if (command == "focusLayer") {
    SetGroupClientEnvironment(kFocusLayer, std::string());
  } else if (command == "hide") {
    Hide();
  } else if (command == "setLoadErrorPolicy") {
    if (arguments.size() > 0) {
      LOG_INFO(MSGID_PALMSYSTEM, 3, PMLOGKS("APP_ID", app_->AppId().c_str()),
               PMLOGKS("INSTANCE_ID", app_->InstanceId().c_str()),
               PMLOGKFV("PID", "%d", app_->Page()->GetWebProcessPID()),
               "webOSSystem.setLoadErrorPolicy(%s)", arguments[0].c_str());
      SetLoadErrorPolicy(arguments[0]);
    }
  } else if (command == "onCloseNotify") {
    if (arguments.size() > 0) {
      LOG_INFO(MSGID_PALMSYSTEM, 3, PMLOGKS("APP_ID", app_->AppId().c_str()),
               PMLOGKS("INSTANCE_ID", app_->InstanceId().c_str()),
               PMLOGKFV("PID", "%d", app_->Page()->GetWebProcessPID()),
               "webOSSystem.onCloseNotify(%s)", arguments[0].c_str());
      OnCloseNotify(arguments[0]);
    }
  } else if (command == "cursorVisibility") {
    return toStr(CursorVisibility());
  } else if (command == "serviceCall") {
    if (app_->Page()->IsClosing()) {
      LOG_INFO(MSGID_PALMSYSTEM, 3, PMLOGKS("APP_ID", app_->AppId().c_str()),
               PMLOGKS("INSTANCE_ID", app_->InstanceId().c_str()),
               PMLOGKFV("PID", "%d", app_->Page()->GetWebProcessPID()),
               "webOSSystem.serviceCall(%s, %s)", arguments[0].c_str(),
               arguments[1].c_str());
      app_->ServiceCall(arguments[0], arguments[1], app_->AppId());
    } else {
      LOG_WARNING(
          MSGID_SERVICE_CALL_FAIL, 3, PMLOGKS("APP_ID", app_->AppId().c_str()),
          PMLOGKS("INSTANCE_ID", app_->InstanceId().c_str()),
          PMLOGKS("URL", arguments[0].c_str()), "Page is NOT in closing");
    }
  } else if (command == "getResource") {
    if (arguments.size() == 1) {
      const std::string& path = arguments[0];
      std::string file_str = util::ReadFile(path);
      return file_str;
    }
  } else if (command == "addBannerMessage") {
    std::string const msg = arguments.size() >= 1 ? arguments[0] : "";
    std::string const params = arguments.size() >= 2 ? arguments[1] : "";
    std::string const icon = arguments.size() >= 3 ? arguments[2] : "";
    std::string const sound_class = arguments.size() >= 4 ? arguments[3] : "";
    std::string const sound_file = arguments.size() >= 5 ? arguments[4] : "";
    std::string const duration = arguments.size() >= 6 ? arguments[5] : "";
    std::string const do_not_suppress =
        arguments.size() >= 7 ? arguments[6] : "false";

    return std::to_string(AddBannerMessage(
        msg, params, icon, sound_class, sound_file, duration, do_not_suppress));
  } else if (command == "removeBannerMessage") {
    if (arguments.size() == 1) {
      RemoveBannerMessage(arguments[0]);
    }
  } else if (command == "clearBannerMessages") {
    ClearBannerMessages();
  }

  return std::string();
}

void PalmSystemBlink::SetCountry() {
  static_cast<WebPageBlink*>(app_->Page())
      ->UpdateExtensionData("country", Country());
}

void PalmSystemBlink::SetLaunchParams(const std::string& params) {
  PalmSystemWebOS::SetLaunchParams(params);
  static_cast<WebPageBlink*>(app_->Page())
      ->UpdateExtensionData("launchParams", LaunchParams());
}

void PalmSystemBlink::SetLocale(const std::string& params) {
  static_cast<WebPageBlink*>(app_->Page())
      ->UpdateExtensionData("locale", params);
}

std::string PalmSystemBlink::Identifier() const {
  if (!app_->Page()) {
    return std::string();
  }

  return static_cast<WebPageBlink*>(app_->Page())->GetIdentifier();
}

void PalmSystemBlink::SetLoadErrorPolicy(const std::string& params) {
  static_cast<WebPageBlink*>(app_->Page())->SetLoadErrorPolicy(params);
}

std::string PalmSystemBlink::TrustLevel() const {
  return static_cast<WebPageBlink*>(app_->Page())->TrustLevel();
}

void PalmSystemBlink::OnCloseNotify(const std::string& params) {
  if (params == "didSetOnCloseCallback") {
    static_cast<WebPageBlink*>(app_->Page())->SetHasOnCloseCallback(true);
  } else if (params == "didClearOnCloseCallback") {
    static_cast<WebPageBlink*>(app_->Page())->SetHasOnCloseCallback(false);
  } else if (params == "didRunOnCloseCallback") {
    static_cast<WebPageBlink*>(app_->Page())->DidRunCloseCallback();
  }
}

double PalmSystemBlink::DevicePixelRatio() {
  return static_cast<WebPageBlink*>(app_->Page())->DevicePixelRatio();
}

// banner management
int PalmSystemBlink::AddBannerMessage(const std::string& msg_title,
                                      const std::string& launch_params,
                                      const std::string& msg_icon_url,
                                      const std::string& /*sound_class*/,
                                      const std::string& /*msg_sound_file*/,
                                      const std::string& /*sound_duration*/,
                                      const std::string& /*do_not_suppress*/) {
  // we define a banner as a toast
  Json::Value create_params;
  create_params["type"] = "standard";
  create_params["message"] = msg_title;
  create_params["launchParams"] = launch_params;
  create_params["iconUrl"] = msg_icon_url;

  // unsupported attributes for now
  //  create_params["soundClass"] = sound_class;
  //  create_params["soundFile"] = msg_sound_file;
  //  create_params["duration"] = sound_duration;
  //  create_params["doNotSuppress"] = do_not_suppress;
  //  create_params["expireTimeout"] = "0";

  // Always increment a static int, to return a unique id.
  static int current_notif_id = 0;

  // Ids start at 1, so 0 can mean "none".
  const int banner_id = ++current_notif_id;

  // Left empty until createToast answers. RemoveBannerMessage() treats an
  // empty entry as "not created yet" rather than closing toastId "".
  banner_ids_[banner_id] = std::string();

  // banner_id is captured by value: current_notif_id is a static local and will
  // have moved on by the time this reply arrives if another banner was posted
  // in the meantime.
  banner_contexts.emplace(
      banner_id,
      LSCalloutContext([this, banner_id](const Json::Value& payload) {
        if (payload.isObject() && payload["toastId"].isString()) {
          this->banner_ids_[banner_id] = payload["toastId"].asString();
        }
        banner_contexts.erase(banner_id);
        return Json::Value();
      }));

  WebAppManagerServiceLuna::Instance()->Call(
      "luna://com.webos.notification/createToast", create_params,
      app_->AppId().c_str(), &banner_contexts.at(banner_id));

  return banner_id;
}

void PalmSystemBlink::RemoveBannerMessage(const std::string& id) {
  const auto it = banner_ids_.find(util::StrToIntWithDefault(id, 0));
  if (it == banner_ids_.end() || it->second.empty()) {
    // Unknown banner, or createToast has not returned its id yet.
    return;
  }

  std::string const remove_params = R"({"toastId":")" + it->second + R"("})";

  app_->ServiceCall("luna://com.webos.notification/closeToast", remove_params, app_->AppId());
  banner_ids_.erase(it);
}

void PalmSystemBlink::ClearBannerMessages() {
  // closeToast's bulk form: drop everything this application posted.
  std::string const clear_params = R"({"sourceId":")" + app_->AppId() + R"("})";

  app_->ServiceCall("luna://com.webos.notification/closeToast", clear_params, app_->AppId());
  banner_ids_.clear();
}

// PalmSystem.deviceInfo, in the coordinate space the application is laid out
// in rather than the panel's.
//
// Enyo documents screenWidth/screenHeight and maximumCardWidth/maximumCardHeight
// as the numbers an application sizes itself from, and Mojo derives its menu row
// count from maximumCardHeight. DeviceInfoImpl fills them from the window size,
// which is the panel: on sargo 1080x2220. That was right when the scale reached
// Blink as --force-device-scale-factor, because the window itself was then
// reported in DIP and came out at 450x925 - the same space the application lays
// out in. Page zoom does not touch the window, so the two parted company and a
// legacy application reading these gets numbers 2.4x larger than the viewport it
// actually has.
//
// Divided per application rather than in DeviceInfoImpl because the scale is per
// application: a modern application at zoom 1 must keep the panel's own numbers.
std::string PalmSystemBlink::ScaledDeviceInfo() const {
  const std::string device_info = GetDeviceInfo("TvDeviceInfo");

  auto* page = static_cast<WebPageBlink*>(app_->Page());
  if (!page)
    return device_info;

  const double scale = page->UiScaleFactor();
  if (scale == 1.0)
    return device_info;

  Json::Value json;
  if (!util::StringToJson(device_info, json) || !json.isObject())
    return device_info;

  // Only the geometry. Everything else in the object describes the device, not
  // a space that the zoom moves.
  static constexpr const char* scaled_keys[] = {
      "screenWidth", "screenHeight", "maximumCardWidth", "maximumCardHeight"};
  for (const char* key : scaled_keys) {
    if (json.isMember(key) && json[key].isNumeric())
      json[key] = static_cast<int>(json[key].asDouble() / scale);
  }

  return util::JsonToString(json);
}

Json::Value PalmSystemBlink::Initialize() {
  initialized_ = true;

  Json::Value data;
  data["launchParams"] = LaunchParams();
  data["country"] = Country();
  data["tvSystemName"] = GetDeviceInfo("TvSystemName");
  data["currentCountryGroup"] = GetDeviceInfo("CountryGroup");
  data["locale"] = Locale();
  data["localeRegion"] = LocaleRegion();
  data["isMinimal"] = IsMinimal();
  data["identifier"] = Identifier();
  data["screenOrientation"] = ScreenOrientation();
  data["deviceInfo"] = ScaledDeviceInfo();
  data["activityId"] = static_cast<double>(ActivityId());
  data["phoneRegion"] = PhoneRegion();
  data["folderPath"] = app_->GetAppDescription()->FolderPath();

  data["devicePixelRatio"] = DevicePixelRatio();
  data["trustLevel"] = TrustLevel();
  return data;
}
