// Copyright (c) 2008-2018 LG Electronics, Inc.
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

#include "network_status.h"

#include <glib.h>
#include <json/json.h>

#include "utils.h"

NetworkStatus::NetworkStatus() = default;

void NetworkStatus::FromJsonObject(const Json::Value& object) {
  if (!object.isObject()) {
    return;
  }
  return_value_ = util::JsonAsBool(object["returnValue"]);
  is_internet_connection_available_ =
      util::JsonAsBool(object["isInternetConnectionAvailable"]);
  if (return_value_) {
    if (object["wired"].isObject()) {
      type_ = "wired";
      information_.FromJsonObject(object["wired"]);
    } else if (object["wifi"].isObject()) {
      type_ = "wifi";
      information_.FromJsonObject(object["wifi"]);
    } else {
      type_ = "wifiDirect";
      information_.FromJsonObject(object["wifiDirect"]);
    }
  }

  if (GDateTime* date_time = g_date_time_new_now_local()) {
    if (gchar* saved_date = g_date_time_format(date_time, "%c")) {
      saved_date_ = std::string(saved_date);
      g_free(saved_date);
    }
    g_date_time_unref(date_time);
  }
}

void NetworkStatus::Information::FromJsonObject(const Json::Value& info) {
  if (!info.isObject()) {
    return;
  }
  netmask_ = util::JsonAsString(info["netmask"]);
  dns1_ = util::JsonAsString(info["dns1"]);
  if (info["dns2"].isString()) {
    dns2_ = info["dns2"].asString();
  }
  ip_address_ = util::JsonAsString(info["ipAddress"]);
  method_ = util::JsonAsString(info["method"]);
  state_ = util::JsonAsString(info["state"]);
  gateway_ = util::JsonAsString(info["gateway"]);
  interface_name_ = util::JsonAsString(info["interfaceName"]);
  on_internet_ = util::JsonAsString(info["onInternet"]);
}
