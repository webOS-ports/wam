// Copyright (c) 2017-2021 LG Electronics, Inc.
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

#include <string.h>

#include <map>
#include <optional>
#include <string>

#include "webos/webview_profile.h"

#include "blink_web_view_profile_helper.h"

void BlinkWebViewProfileHelper::ClearBrowsingData(
    const int remove_browsing_data_mask,
    webos::WebViewProfile* profile) {
  if (profile) {
    profile->RemoveBrowsingData(remove_browsing_data_mask);
  } else {
    BlinkWebViewProfileHelper::ClearDefaultBrowsingData(
        remove_browsing_data_mask);
  }
}

void BlinkWebViewProfileHelper::ClearDefaultBrowsingData(
    const int remove_browsing_data_mask) {
  webos::WebViewProfile::GetDefaultProfile()->RemoveBrowsingData(
      remove_browsing_data_mask);
}

int BlinkWebViewProfileHelper::MaskForBrowsingDataType(const char* type) {
  if (strcmp(type, browsing_data_types::kAll) == 0) {
    return webos::WebViewProfile::REMOVE_ALL;
  }
  if (strcmp(type, browsing_data_types::kCache) == 0) {
    return webos::WebViewProfile::REMOVE_CACHE;
  }
  if (strcmp(type, browsing_data_types::kCodeCache) == 0) {
    return webos::WebViewProfile::REMOVE_CODE_CACHE;
  }
  if (strcmp(type, browsing_data_types::kCookies) == 0) {
    return webos::WebViewProfile::REMOVE_COOKIES |
           webos::WebViewProfile::REMOVE_WEBRTC_IDENTITY;
  }
  if (strcmp(type, browsing_data_types::kFileSystems) == 0) {
    return webos::WebViewProfile::REMOVE_FILE_SYSTEMS;
  }
  if (strcmp(type, browsing_data_types::kIndexedDB) == 0) {
    return webos::WebViewProfile::REMOVE_INDEXEDDB;
  }
  if (strcmp(type, browsing_data_types::kLocalStorage) == 0) {
    return webos::WebViewProfile::REMOVE_LOCAL_STORAGE;
  }
  if (strcmp(type, browsing_data_types::kChannelIDs) == 0) {
    return webos::WebViewProfile::REMOVE_CHANNEL_IDS;
  }
  if (strcmp(type, browsing_data_types::kServiceWorkers) == 0) {
    return webos::WebViewProfile::REMOVE_SERVICE_WORKERS;
  }
  if (strcmp(type, browsing_data_types::kCacheStorage) == 0) {
    return webos::WebViewProfile::REMOVE_CACHE_STORAGE;
  }
  if (strcmp(type, browsing_data_types::kWebSQL) == 0) {
    return webos::WebViewProfile::REMOVE_WEBSQL;
  }

  return 0;
}

void BlinkWebViewProfileHelper::SetNotifierEnabled(const std::string& app_id,
                                                   bool enabled) {
  webos::WebViewProfile::GetDefaultProfile()->SetNotifierEnabled(app_id,
                                                                 enabled);
}

namespace {

using AppPermission = webos::WebViewProfile::AppPermission;
using AppPermissionSetting = webos::WebViewProfile::AppPermissionSetting;

std::optional<AppPermission> ToAppPermission(const std::string& name) {
  if (name == "geolocation")
    return AppPermission::kGeolocation;
  if (name == "notifications")
    return AppPermission::kNotifications;
  return std::nullopt;
}

std::optional<AppPermissionSetting> ToAppPermissionSetting(
    const std::string& name) {
  if (name == "allow")
    return AppPermissionSetting::kAllow;
  if (name == "block")
    return AppPermissionSetting::kBlock;
  if (name == "ask")
    return AppPermissionSetting::kAsk;
  return std::nullopt;
}

const char* ToString(AppPermissionSetting setting) {
  switch (setting) {
    case AppPermissionSetting::kAllow:
      return "allow";
    case AppPermissionSetting::kBlock:
      return "block";
    case AppPermissionSetting::kAsk:
      return "ask";
  }
  return "ask";
}

}  // namespace

std::map<std::string, std::string> BlinkWebViewProfileHelper::GetAppPermissions(
    const std::string& permission) {
  std::map<std::string, std::string> result;
  const std::optional<AppPermission> type = ToAppPermission(permission);
  if (!type)
    return result;
  for (const auto& [app_id, setting] :
       webos::WebViewProfile::GetDefaultProfile()->GetAppPermissions(*type)) {
    // A stored "ask" is no decision; leave it out as never asked.
    if (setting != AppPermissionSetting::kAsk)
      result.emplace(app_id, ToString(setting));
  }
  return result;
}

void BlinkWebViewProfileHelper::SetAppPermission(const std::string& app_id,
                                                 const std::string& permission,
                                                 const std::string& setting) {
  const std::optional<AppPermission> type = ToAppPermission(permission);
  const std::optional<AppPermissionSetting> value =
      ToAppPermissionSetting(setting);
  if (!type || !value || app_id.empty())
    return;
  webos::WebViewProfile::GetDefaultProfile()->SetAppPermission(app_id, *type,
                                                               *value);
}

void BlinkWebViewProfileHelper::ResetAppPermissions(
    const std::string& permission) {
  if (const std::optional<AppPermission> type = ToAppPermission(permission))
    webos::WebViewProfile::GetDefaultProfile()->ResetAppPermissions(*type);
}
