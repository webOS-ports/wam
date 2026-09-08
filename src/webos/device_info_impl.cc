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

#include "device_info_impl.h"

#include <string>
#include <fstream>

#include <glib.h>
#include <json/value.h>
#include <lunaprefs.h>

#include "log_manager.h"
#include "utils.h"

DeviceInfoImpl::DeviceInfoImpl() = default;

void DeviceInfoImpl::Initialize() {
  GatherInfo();

  // Published here rather than at the end of this function: the locale block
  // below returns early when localeInfo cannot be parsed, which would leave
  // PalmSystem.deviceInfo unset altogether.
  UpdateTvDeviceInfo();

  const std::string& json_string =
      util::ReadFile("/var/luna/preferences/localeInfo");
  if (!json_string.empty()) {

  Json::Value locale_json = util::StringToJson(json_string);
  if (!locale_json.isObject() || locale_json.empty() ||
      !locale_json["localeInfo"].isObject() ||
      !locale_json["localeInfo"]["locales"].isObject() ||
      !locale_json["localeInfo"]["locales"]["UI"].isString() ||
      !locale_json["country"].isString() ||
      !locale_json["smartServiceCountryCode3"].isString()) {
    LOG_ERROR(MSGID_LOCALEINFO_READ_FAIL, 1,
              PMLOGKS("CONTENT", json_string.c_str()), "");
    return;
  }

  const Json::Value& locale_info = locale_json["localeInfo"];

  std::string language(locale_info["locales"]["UI"].asString());
  std::string localcountry(locale_json["country"].asString());
  std::string smartservicecountry(
      locale_json["smartServiceCountryCode3"].asString());

  SetSystemLanguage(language.c_str());
  SetDeviceInfo("LocalCountry", localcountry.c_str());
  SetDeviceInfo("SmartServiceCountry", smartservicecountry.c_str());
  }

}

void DeviceInfoImpl::UpdateTvDeviceInfo() {
  Json::Value deviceInfo_json(Json::objectValue);
  deviceInfo_json["modelName"] = model_name_;
  deviceInfo_json["platformVersion"] = platform_version_;
  {
    int major = 0, minor = 0, dot = 0;
    const int fields_count =
        std::sscanf(platform_version_.c_str(), "%d.%d.%d", &major, &minor, &dot);
    if (fields_count != 3) {
      major = minor = dot = -1;
    }

    deviceInfo_json["platformVersionMajor"] = major;
    deviceInfo_json["platformVersionMinor"] = minor;
    deviceInfo_json["platformVersionDot"] = dot;
  }
  deviceInfo_json["screenWidth"] = screen_width_;
  deviceInfo_json["screenHeight"] = screen_height_;

  // LunaSysMgr reported the card area next to the screen size and legacy
  // frameworks lay themselves out from it. A full-screen card is the display;
  // the shell takes its own system UI out of that.
  deviceInfo_json["maximumCardWidth"] = screen_width_;
  deviceInfo_json["maximumCardHeight"] = screen_height_;

  // Also reported by LunaSysMgr, and read by legacy applications.
  deviceInfo_json["keyboardAvailable"] = false;
  deviceInfo_json["keyboardSlider"] = false;
  // deviceInfo_json["panelType"] = "";

  SetDeviceInfo("TvDeviceInfo", util::JsonToString(deviceInfo_json));
}

// The display size is not known when Initialize() runs. HardwareScreenWidth /
// HardwareScreenHeight are never set by anything, and the DisplayWidth
// fallback is only filled in by WebAppManager::SetUiSize() once a window
// exists. Republish whenever it lands, or PalmSystem.deviceInfo keeps the
// zeroes it was built with and every application reads screenWidth: 0.
void DeviceInfoImpl::SetDisplayWidth(int value) {
  DeviceInfo::SetDisplayWidth(value);
  screen_width_ = static_cast<int>(value / screen_density_);
  UpdateTvDeviceInfo();
}

void DeviceInfoImpl::SetDisplayHeight(int value) {
  DeviceInfo::SetDisplayHeight(value);
  screen_height_ = static_cast<int>(value / screen_density_);
  UpdateTvDeviceInfo();
}

bool DeviceInfoImpl::GetInfoFromLunaPrefs(const char* key,
                                          std::string& value) const {
  char* str = nullptr;
  if (LP_ERR_NONE == LPSystemCopyStringValue(key, &str) && str) {
    value = str;
    g_free(static_cast<gchar*>(str));
    return true;
  }

  g_free(static_cast<gchar*>(str));
  value = "Unknown";
  return false;
}

void DeviceInfoImpl::InitDisplayInfo() {
  // Display information
  // --------------------------------------------------------
  int hardware_screen_width = 0;
  int hardware_screen_height = 0;

  std::string hardware_screen_width_str;
  std::string hardware_screen_height_str;
  if (GetDeviceInfo("HardwareScreenWidth", hardware_screen_width_str) &&
      GetDeviceInfo("HardwareScreenHeight", hardware_screen_height_str)) {
    hardware_screen_width =
        util::StrToIntWithDefault(hardware_screen_width_str, 0);
    hardware_screen_height =
        util::StrToIntWithDefault(hardware_screen_height_str, 0);
  } else {
    GetDisplayWidth(hardware_screen_width);
    GetDisplayHeight(hardware_screen_height);
  }

  screen_width_ = static_cast<int>(hardware_screen_width / screen_density_);
  screen_height_ = static_cast<int>(hardware_screen_height / screen_density_);
}

void DeviceInfoImpl::InitPlatformInfo() {
  // normally like this info
  /*
     "modelName": "WEBOS1",
     "platformVersion": "00.00.00",
     "platformVersionDot": 00,
     "platformVersionMajor_pos": 00,
     "platformVersionMinor": 00,
  */

  // The webOS level this image claims to provide, read from /etc/buildinfo
  // below and applied after the distribution's own version.
  std::string webos_compat_version;

  {
    // setup fallback recognizable value, in case buildinfo can't be parsed
    model_name_ = "LuneOS-dev";
    platform_version_ = "0.9.9";

    std::ifstream buildinfoFile("/etc/buildinfo");
    std::string line;
    while (std::getline(buildinfoFile, line))
    {
      size_t startpos = line.find_first_of(" =");
      if( std::string::npos == startpos ) continue; // skip useless lines
      std::string key = line.substr(0, startpos);
      std::string value = line.substr(line.find_last_of(" =")+1);

      if (key == "DISTRO_VERSION") {
        SetDeviceInfo("FirmwareVersion", value + ".0" /*add dot version*/);
      }
      else if (key == "WEBOS_COMPAT_VERSION") {
        webos_compat_version = value;
      }
      else if (key == "MACHINE") {
        SetDeviceInfo("ModelName", value);
      }
    }
  }

  std::string value;
  if (GetDeviceInfo("ModelName", value)) {
    model_name_ = value;
  }
  if (GetDeviceInfo("FirmwareVersion", value)) {
    platform_version_ = std::move(value);
  }

  // Legacy applications branch on platformVersion / platformVersionMajor to
  // choose which of their bundled builds to load - typically "below webOS 3
  // means load the Mojo build". The distribution's own version is 1.0, which
  // reads as webOS 1.x and sends them to a build wanting frameworks that are
  // not here. WEBOS_COMPAT_VERSION states the webOS level actually provided,
  // so report that when the image sets one.
  if (!webos_compat_version.empty()) {
    platform_version_ = webos_compat_version;
  }

  size_t major_pos = 0, minor_pos = 0;
  major_pos = platform_version_.find_first_of('.');
  if (major_pos != std::string::npos &&
      major_pos <= platform_version_.size() - 1) {
    minor_pos = platform_version_.find_first_of('.', major_pos + 1);
  }
  if (major_pos == std::string::npos || minor_pos == std::string::npos) {
    version_major_ = version_minor_ = version_dot_ = -1;
  } else {
    version_major_ =
        util::StrToIntWithDefault(platform_version_.substr(0, major_pos), 0);
    version_minor_ = util::StrToIntWithDefault(
        platform_version_.substr(major_pos + 1, minor_pos), 0);
    version_dot_ =
        util::StrToIntWithDefault(platform_version_.substr(minor_pos + 1), 0);
  }
}

void DeviceInfoImpl::GatherInfo() {
  InitDisplayInfo();
  InitPlatformInfo();
}
