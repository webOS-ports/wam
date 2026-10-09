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

#ifndef CORE_WEB_APP_MANAGER_SERVICE_H_
#define CORE_WEB_APP_MANAGER_SERVICE_H_

#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "web_app_manager.h"
#include "webos/webview_base.h"

namespace Json {
class Value;
}

enum ErrorCode : std::uint16_t {
  kErrCodeLaunchappMissParam = 1000,
  kErrCodeLaunchappUnsupportedType = 1001,
  kErrCodeLaunchappInvalidTrustlevel = 1002,
  kErrCodeNoRunningApp = 2000,
  kErrCodeClearDataBrawsingEmptyArray = 3000,
  kErrCodeClearDataBrawsingInvalidValue = 3001,
  kErrCodeClearDataBrawsingUnknownData = 3002,
  kErrCodeFireNotificationEventMissingParameter = 4000,
  kErrCodeFireNotificationEventUnsupportedType = 4001,
  kErrCodeAnswerPermissionPromptMissingParameter = 4100,
  kErrCodeAnswerPermissionPromptUnknownPrompt = 4101,
  kErrCodeAppPermissionInvalidPermission = 4200,
  kErrCodeAppPermissionInvalidSetting = 4201,
  kErrCodeAppPermissionInvalidAppId = 4202,
  kErrCodeInvalidParam = 5000
};

const std::string kErrInvalidParam =
    "Incoming JSON is invalid or not completed";

const std::string kErrMissParam = "Miss launch parameter(s)";
const std::string kErrUnsupportedType = "Unsupported app type (Check subType)";
const std::string kErrInvalidTrustLevel =
    "Invalid trust level (Check trustLevel)";

const std::string kErrNoRunningApp = "App is not running";

const std::string kErrEmptyArray = "Empty array is not allowed.";
const std::string kErrInvalidValue = "Invalid value";
const std::string kErrUnknownData = "Unknown data";
const std::string kErrOnlyAllowedForString = "Only allowed for string type";

const std::string kErrFireNotificationEventMissingParameter =
    "Missing parameter(s)";
const std::string kErrFireNotificationEventUnsupportedType = "Unsupported type";

const std::string kErrAnswerPermissionPromptMissingParameter =
    "promptId (string) and allow (boolean) are required";
const std::string kErrAnswerPermissionPromptUnknownPrompt =
    "No such prompt is waiting for an answer";

const std::string kErrAppPermissionInvalidPermission =
    "permission must be \"geolocation\" or \"notifications\"";
const std::string kErrAppPermissionInvalidSetting =
    "setting must be \"allow\", \"block\" or \"ask\"";
const std::string kErrAppPermissionInvalidAppId =
    "appId must be an application id (letters, digits, '.', '-', '_')";

class WebAppBase;

class WebAppManagerService {
 public:
  WebAppManagerService();
  virtual ~WebAppManagerService() = default;

  virtual bool StartService() = 0;
  // methods published to the bus
  virtual Json::Value launchApp(const Json::Value& request) = 0;
  virtual Json::Value killApp(const Json::Value& request) = 0;
  virtual Json::Value pauseApp(const Json::Value& request) = 0;
  virtual Json::Value logControl(const Json::Value& request) = 0;
  virtual Json::Value setOrientation(const Json::Value& request) = 0;
  virtual Json::Value setAppVisibility(const Json::Value& request) = 0;
  virtual Json::Value setInspectorEnable(const Json::Value& request) = 0;
  virtual Json::Value closeAllApps(const Json::Value& request) = 0;
  virtual Json::Value listRunningApps(const Json::Value& request,
                                      bool subscribed) = 0;
  virtual Json::Value getWebProcessSize(const Json::Value& request) = 0;
  virtual Json::Value clearBrowsingData(const Json::Value& request) = 0;
  virtual Json::Value webProcessCreated(const Json::Value& request,
                                        bool subscribed) = 0;
  virtual Json::Value fireNotificationEvent(const Json::Value& request) = 0;

 protected:
  std::string OnLaunch(const std::string& app_desc_string,
                       const std::string& params,
                       const std::string& launching_app_id,
                       int& err_code,
                       std::string& err_msg);

  bool OnKillApp(const std::string& app_id,
                 const std::string& instance_id,
                 bool force = false);
  bool OnPauseApp(const std::string& instance_id);
  bool SetAppShownWhileDeactivated(const std::string& instance_id,
                                   const std::string& app_id,
                                   bool shown);
  Json::Value OnLogControl(const std::string& keys, const std::string& value);
  bool OnCloseAllApps(uint32_t pid = 0);
  Json::Value GetWebProcessProfiling();
  int MaskForBrowsingDataType(const char* type);
  void OnClearBrowsingData(const int remove_browsing_data_mask);
  void OnAppInstalled(const std::string& app_id);
  void OnAppRemoved(const std::string& app_id);

  void SetDeviceInfo(const std::string& name, const std::string& value);
  void SetUiSize(int width, int height);
  void SetSystemLanguage(const std::string& language);
  std::string GetSystemLanguage();
  void SetForceCloseApp(const std::string& app_id,
                        const std::string& instance_id);
  void UpdateNetworkStatus(const Json::Value& object);
  void NotifyMemoryPressure(webos::WebViewBase::MemoryPressureLevel level);
  void UpdateNotificationPermission(const std::string& app_id, bool enabled);
  std::map<std::string, std::string> GetAppPermissions(
      const std::string& permission);
  void SetAppPermission(const std::string& app_id,
                        const std::string& permission,
                        const std::string& setting);
  void ResetAppPermissions(const std::string& permission);
  void SetAccessibilityEnabled(bool enable);
  uint32_t GetWebProcessId(const std::string& app_id,
                           const std::string& instance_id);

  std::list<const WebAppBase*> RunningApps();
  std::list<const WebAppBase*> RunningApps(uint32_t pid);
  std::vector<ApplicationInfo> List(bool include_system_apps = false);

  bool IsEnyoApp(const std::string& app_id);
  bool SetOrientation(const std::string& orientation);
  void SetDisplayRotation(int degrees);
};

#endif  // CORE_WEB_APP_MANAGER_SERVICE_H_
