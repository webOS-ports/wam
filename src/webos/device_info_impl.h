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

#ifndef WEBOS_DEVICE_INFO_IMPL_H_
#define WEBOS_DEVICE_INFO_IMPL_H_

#include "device_info.h"

class DeviceInfoImpl : public DeviceInfo {
 public:
  DeviceInfoImpl();

  void Initialize() override;

  void SetDisplayWidth(int value) override;
  void SetDisplayHeight(int value) override;
  void SetHardwareKeyboard(bool present, bool slider) override;

 private:
  void UpdateTvDeviceInfo();

  int screen_width_ = 0;
  int screen_height_ = 0;

  float screen_density_ = 1.0f;

  std::string model_name_ = "webOS.Open";
  std::string platform_version_ = "00.00.00";

  // platform versions are <major>.<minor>.<dot>
  int version_major_ = 0;
  int version_minor_ = 0;
  int version_dot_ = 0;

  // What procfs said at startup, then whatever the input method reports. Kept
  // rather than re-read on every publish so that a live answer is not thrown
  // away by an unrelated republish.
  bool keyboard_present_ = false;
  bool keyboard_slider_ = false;

  std::string ota_id_;
  std::string hardware_version_ = "0x00000001";
  std::string firmware_version_ = "00.00.01";

  bool GetInfoFromLunaPrefs(const char* key, std::string& value) const;
  void InitDisplayInfo();
  void InitPlatformInfo();
  void GatherInfo();
};

#endif  // WEBOS_DEVICE_INFO_IMPL_H_
