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

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <vector>

#include <glib.h>
#include <json/value.h>
#include <lunaprefs.h>

#include "log_manager.h"
#include "utils.h"

namespace {

// Deciding whether the device has a physical keyboard, so that
// PalmSystem.deviceInfo can say so. Legacy applications read it: the Enyo and
// Mojo dialpads branch on keyboardType for dial-by-name, and anything that wants
// to put the caret in a field on launch rather than wait for a tap needs to know
// a keyboard is there.
//
// Read from /proc/bus/input/devices, which is world readable and needs no
// service to be up - PalmSystem.deviceInfo is read by applications as they start,
// so an answer that arrives later over the bus would be too late for them.
//
// The rules are the same ones MImKeyboard::keyboardKindOf() applies in
// maliit-framework-webos, which decides the same question for the input method,
// and they have to stay the same: a device where the keyboard takes the on-screen
// keyboard away but applications are told there is no keyboard is worse off than
// before either change. They look at one file for that reason.

// Not all 26: a driver is free to leave a key out or report it as something else,
// and these keyboards' drivers do. Still far above the handful a device that is
// not a keyboard advertises.
constexpr int kMinimumLetterKeys = 20;

// SW_TABLET_MODE, from linux/input-event-codes.h. Spelled out rather than
// included: it is one number, and this file is otherwise free of kernel headers.
constexpr unsigned kSwTabletMode = 0x01;

// The 26 Latin letters in the order evdev numbers them, which is the three rows
// of a US QWERTY: KEY_Q..KEY_P, KEY_A..KEY_L, KEY_Z..KEY_M.
constexpr unsigned kLetterKeys[] = {16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                                    30, 31, 32, 33, 34, 35, 36, 37, 38,
                                    44, 45, 46, 47, 48, 49, 50};

// One capability bitmap, as the kernel prints it after "B: KEY=" or "B: SW=":
// hex words, most significant first. Whether it pads them is not something to
// rely on - an MP01 fills every word, a Mindset pads nothing - so the width comes
// from the longest word, falling back to this process's own long, which belongs
// to the same kernel, when every value is small enough to be ambiguous.
class CapabilityBitmap {
 public:
  explicit CapabilityBitmap(const std::string& words) {
    std::vector<std::string> parsed;
    std::size_t widest = 0;

    for (std::size_t at = 0; at < words.size();) {
      const std::size_t start = words.find_first_not_of(" \t", at);
      if (start == std::string::npos)
        break;
      std::size_t end = words.find_first_of(" \t", start);
      if (end == std::string::npos)
        end = words.size();

      parsed.push_back(words.substr(start, end - start));
      widest = std::max(widest, parsed.back().size());
      at = end;
    }

    if (parsed.empty())
      return;

    word_bits_ = widest > 8 ? 64 : static_cast<int>(sizeof(unsigned long) * 8);

    // Least significant word last in the text, first in the vector, so a code
    // indexes straight into it.
    for (std::size_t i = parsed.size(); i-- > 0;)
      words_.push_back(std::strtoull(parsed[i].c_str(), nullptr, 16));
  }

  bool Advertises(unsigned code) const {
    if (word_bits_ <= 0)
      return false;

    const std::size_t word = code / static_cast<unsigned>(word_bits_);
    const unsigned bit = code % static_cast<unsigned>(word_bits_);

    if (word >= words_.size())
      return false;

    return (words_[word] >> bit) & 1u;
  }

  int LetterCount() const {
    int found = 0;
    for (unsigned code : kLetterKeys) {
      if (Advertises(code))
        ++found;
    }
    return found;
  }

 private:
  std::vector<std::uint64_t> words_;
  int word_bits_ = 0;
};

std::string ValueAfter(const std::string& line, const char* prefix) {
  const std::size_t length = std::strlen(prefix);
  if (line.compare(0, length, prefix) != 0)
    return std::string();
  return line.substr(length);
}

struct KeyboardFacts {
  bool available = false;
  bool slider = false;
};

// A device registered with no parent - directly under /devices/virtual/input/ -
// was made by a program rather than plugged in. A remote-control tool such as
// RustDesk registers a uinput keyboard advertising all 26 letters, and counting
// it would tell every application there is a keyboard when there is not. Only
// that one path: a Bluetooth keyboard arrives through uhid, lands under
// /devices/virtual/misc/uhid/... and is real.
bool IsSoftwareDevice(const std::string& sysfs) {
  return sysfs.compare(0, 23, "/devices/virtual/input/") == 0;
}

KeyboardFacts ReadKeyboardFacts() {
  KeyboardFacts facts;

  std::ifstream devices("/proc/bus/input/devices");
  if (!devices.is_open()) {
    LOG_WARNING(MSGID_WAM_DEBUG, 0,
                "cannot read /proc/bus/input/devices; reporting no keyboard");
    return facts;
  }

  std::string sysfs;
  std::string line;

  while (std::getline(devices, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    // "N: Name=" opens a device; everything up to the next one belongs to it.
    if (line.compare(0, 8, "N: Name=") == 0) {
      sysfs.clear();
      continue;
    }

    const std::string sysfs_value = ValueAfter(line, "S: Sysfs=");
    if (!sysfs_value.empty()) {
      sysfs = sysfs_value;
      continue;
    }

    const std::string keys = ValueAfter(line, "B: KEY=");
    if (!keys.empty()) {
      if (!IsSoftwareDevice(sysfs) &&
          CapabilityBitmap(keys).LetterCount() >= kMinimumLetterKeys) {
        facts.available = true;
      }
      continue;
    }

    const std::string switches = ValueAfter(line, "B: SW=");
    if (!switches.empty() &&
        CapabilityBitmap(switches).Advertises(kSwTabletMode)) {
      // The keyboard folds or slides away, so whether it can be reached is not
      // fixed. Reported as legacy reported it, from DeviceInfo::keyboardSlider().
      facts.slider = true;
    }
  }

  return facts;
}

}  // namespace

DeviceInfoImpl::DeviceInfoImpl() = default;

void DeviceInfoImpl::Initialize() {
  GatherInfo();

  // Seeded from procfs rather than waited for over the bus: applications read
  // PalmSystem.deviceInfo as they launch, and com.webos.service.ime is started
  // on demand, so a subscription alone would leave the first applications told
  // there is no keyboard.
  const KeyboardFacts keyboard = ReadKeyboardFacts();
  keyboard_present_ = keyboard.available;
  keyboard_slider_ = keyboard.slider;

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

  std::string const language(locale_info["locales"]["UI"].asString());
  std::string const localcountry(locale_json["country"].asString());
  std::string const smartservicecountry(
      locale_json["smartServiceCountryCode3"].asString());

  SetSystemLanguage(language.c_str());
  SetDeviceInfo("LocalCountry", localcountry.c_str());
  SetDeviceInfo("SmartServiceCountry", smartservicecountry.c_str());
  }

}

void DeviceInfoImpl::UpdateTvDeviceInfo() {
  Json::Value device_info_json(Json::objectValue);
  device_info_json["modelName"] = model_name_;
  device_info_json["platformVersion"] = platform_version_;
  {
    int major = 0, minor = 0, dot = 0;
    const int fields_count =
        std::sscanf(platform_version_.c_str(), "%d.%d.%d", &major, &minor, &dot);
    if (fields_count != 3) {
      major = minor = dot = -1;
    }

    device_info_json["platformVersionMajor"] = major;
    device_info_json["platformVersionMinor"] = minor;
    device_info_json["platformVersionDot"] = dot;
  }
  device_info_json["screenWidth"] = screen_width_;
  device_info_json["screenHeight"] = screen_height_;

  // LunaSysMgr reported the card area next to the screen size and legacy
  // frameworks lay themselves out from it. A full-screen card is the display;
  // the shell takes its own system UI out of that.
  device_info_json["maximumCardWidth"] = screen_width_;
  device_info_json["maximumCardHeight"] = screen_height_;

  // Also reported by LunaSysMgr, and read by legacy applications. Both were
  // hardcoded false, so every device with a physical keyboard told its
  // applications it had none.
  //
  // Re-read here rather than cached at construction so that republishing device
  // info picks up a change. That still misses a keyboard plugged in while
  // WebAppMgr runs and nothing else changes; the subscription for that is
  // com.webos.service.ime/getKeyboardStatus, and wiring it up is the remaining
  // half of this.
  //
  // keyboardType is deliberately still absent. It is a layout - QWERTY, AZERTY,
  // QWERTZ - which none of this knows: for the phones with a profile it is in
  // webos-keyboard's data, and for a USB or Bluetooth keyboard it is in the
  // compositor's xkb keymap. Absent reads as falsy, which is what applications
  // already handle.
  device_info_json["keyboardAvailable"] = keyboard_present_;
  device_info_json["keyboardSlider"] = keyboard_slider_;
  // device_info_json["panelType"] = "";

  SetDeviceInfo("TvDeviceInfo", util::JsonToString(device_info_json));
}

// The display size is not known when Initialize() runs. HardwareScreenWidth /
// HardwareScreenHeight are never set by anything, and the DisplayWidth
// fallback is only filled in by WebAppManager::SetUiSize() once a window
// exists. Republish whenever it lands, or PalmSystem.deviceInfo keeps the
// zeroes it was built with and every application reads screenWidth: 0.
void DeviceInfoImpl::SetHardwareKeyboard(bool present, bool slider) {
  if (keyboard_present_ == present && keyboard_slider_ == slider) {
    return;
  }

  LOG_INFO(MSGID_WAM_DEBUG, 0,
           "hardware keyboard: present=%d slider=%d (was present=%d)",
           present ? 1 : 0, slider ? 1 : 0, keyboard_present_ ? 1 : 0);

  keyboard_present_ = present;
  keyboard_slider_ = slider;

  // Applications that already read deviceInfo keep what they were given; this
  // is for the ones launched from here on.
  UpdateTvDeviceInfo();
}

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

    std::ifstream buildinfo_file("/etc/buildinfo");
    std::string line;
    while (std::getline(buildinfo_file, line)) {
      size_t const startpos = line.find_first_of(" =");
      if( std::string::npos == startpos ) continue; // skip useless lines
      std::string const key = line.substr(0, startpos);
      std::string const value = line.substr(line.find_last_of(" =")+1);

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
