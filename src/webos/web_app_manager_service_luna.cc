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

#include "web_app_manager_service_luna.h"

#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <json/json.h>
#include "webos/public/runtime.h"
#include "webos/webview_base.h"

#include "application_description.h"
#include "log_manager.h"
#include "permission_prompt.h"
#include "utils.h"
#include "web_app_base.h"
#include "web_app_manager_tracer.h"

// just to save some typing, the template filled out with the name of this class
#define QCB(FUNC) \
  bus_callback_json<WebAppManagerServiceLuna, &WebAppManagerServiceLuna::FUNC>
#define QCB_subscription(FUNC)                             \
  bus_subscription_callback_json<WebAppManagerServiceLuna, \
                                 &WebAppManagerServiceLuna::FUNC>
#define LS2_METHOD_ENTRY(FUNC) \
  { #FUNC, QCB(FUNC), LUNA_METHOD_FLAGS_NONE }
#define LS2_SUBSCRIPTION_ENTRY(FUNC) \
  { #FUNC, QCB_subscription(FUNC), LUNA_METHOD_FLAGS_NONE }

#define GET_LS2_SERVER_STATUS(FUNC, PARAMS)                        \
  Call<WebAppManagerServiceLuna, &WebAppManagerServiceLuna::FUNC>( \
      "luna://com.palm.lunabus/signal/registerServerStatus", PARAMS, this)
#define LS2_CALL(FUNC, SERVICE, PARAMS)                            \
  Call<WebAppManagerServiceLuna, &WebAppManagerServiceLuna::FUNC>( \
      SERVICE, PARAMS, this)

LSMethod WebAppManagerServiceLuna::methods_[] = {
    LS2_METHOD_ENTRY(launchApp),
    LS2_METHOD_ENTRY(killApp),
    LS2_METHOD_ENTRY(pauseApp),
    LS2_METHOD_ENTRY(closeAllApps),
#if defined(ENABLE_API_SET_INSPECTOR_ENABLE)
    LS2_METHOD_ENTRY(setInspectorEnable),
#endif
    LS2_METHOD_ENTRY(logControl),
    LS2_METHOD_ENTRY(setOrientation),
    LS2_METHOD_ENTRY(setAppVisibility),
    LS2_METHOD_ENTRY(getWebProcessSize),
    LS2_METHOD_ENTRY(clearBrowsingData),
    LS2_METHOD_ENTRY(fireNotificationEvent),
    LS2_METHOD_ENTRY(answerPermissionPrompt),
    LS2_METHOD_ENTRY(getAppPermissions),
    LS2_METHOD_ENTRY(setAppPermission),
    LS2_METHOD_ENTRY(resetAppPermissions),
    LS2_SUBSCRIPTION_ENTRY(listRunningApps),
    LS2_SUBSCRIPTION_ENTRY(webProcessCreated),
    {}};

WebAppManagerServiceLuna::WebAppManagerServiceLuna() = default;

WebAppManagerServiceLuna::~WebAppManagerServiceLuna() = default;

bool WebAppManagerServiceLuna::StartService() {
  return PalmServiceBase::StartService();
}

Json::Value WebAppManagerServiceLuna::launchApp(const Json::Value& request) {
  PMTRACE_FUNCTION;

  int err_code = 0;
  std::string err_msg;
  Json::Value reply;

  if (!request.isObject() ||
      (!request.isMember("appDesc") || !request["appDesc"].isObject() ||
       !request["appDesc"]["id"].isString()) ||
      (request.isMember("parameters") && !request["parameters"].isObject()) ||
      (request.isMember("launchingAppId") &&
       !request["launchingAppId"].isString()) ||
      (request.isMember("launchingProcId") &&
       !request["launchingProcId"].isString()) ||
      (!request.isMember("instanceId") || !request["instanceId"].isString())) {
    reply["returnValue"] = false;
    reply["errorCode"] = kErrCodeLaunchappMissParam;
    reply["errorText"] = kErrMissParam;
    return reply;
  }

  Json::Value json_params = request["parameters"];
  if (request.isMember("launchHidden") && request["launchHidden"] == true) {
    json_params["launchedHidden"] = true;
  }

  if (request.isMember("noWindow") && request["noWindow"] == true) {
    json_params["noWindow"] = true;
  }

  // if "preload" parameter is not "full" or "partial" or "minimal", there is no
  // preload parameter.
  if (request.isMember("preload") && request["preload"].isString()) {
    json_params["preload"] = request["preload"];
  }

  if (request.isMember("keepAlive") && request["keepAlive"] == true) {
    json_params["keepAlive"] = true;
  }

  std::string instance_id = request["instanceId"].asString();
  if (!IsValidInstanceId(instance_id)) {
    reply["returnValue"] = false;
    reply["errorCode"] = kErrCodeLaunchappMissParam;
    reply["errorText"] = kErrMissParam;
    return reply;
  }
  json_params["instanceId"] = instance_id;

  std::string const str_params = util::JsonToString(json_params);

  std::string const app_id = request["appDesc"]["id"].asString();
  LOG_INFO_WITH_CLOCK(
      MSGID_APPLAUNCH_START, 4, PMLOGKS("PerfType", "AppLaunch"),
      PMLOGKS("PerfGroup", app_id.c_str()), PMLOGKS("APP_ID", app_id.c_str()),
      PMLOGKS("INSTANCE_ID", instance_id.c_str()), "params : %s",
      str_params.c_str());

  std::string const str_app_desc = util::JsonToString(request["appDesc"]);
  instance_id = WebAppManagerService::OnLaunch(
      str_app_desc, str_params, request["launchingAppId"].asString(), err_code,
      err_msg);

  if (instance_id.empty()) {
    reply["returnValue"] = false;
    reply["errorCode"] = err_code;
    reply["errorText"] = err_msg;
  } else {
    reply["returnValue"] = true;
    reply["appId"] = request["appDesc"]["id"].asString();
    reply["instanceId"] = instance_id;
  }
  return reply;
}

bool WebAppManagerServiceLuna::IsValidInstanceId(
    const std::string& instance_id) {
  return instance_id.find_first_not_of("\f\n\r\v") != std::string::npos;
}

Json::Value WebAppManagerServiceLuna::killApp(const Json::Value& request) {
  Json::Value reply;

  if (!request.isObject() ||
      (request.isMember("instanceId") && !request["instanceId"].isString()) ||
      (request.isMember("appId") && !request["appId"].isString()) ||
      (request.isMember("reason") && !request["reason"].isString())) {
    reply["returnValue"] = false;
    reply["errorCode"] = kErrCodeInvalidParam;
    reply["errorText"] = kErrInvalidParam;
    return reply;
  }

  bool instances = false;
  std::string const instance_id = request["instanceId"].asString();
  std::string const app_id = request["appId"].asString();
  std::string reason;

  if (request.isMember("reason")) {
    reason = request["reason"].asString();
  }

  LOG_INFO(MSGID_LUNA_API, 3, PMLOGKS("APP_ID", app_id.c_str()),
           PMLOGKS("INSTANCE_ID", instance_id.c_str()),
           PMLOGKS("API", "killApp"), "reason : %s", reason.c_str());

  bool const memory_reclaim =
      reason.empty() || reason.compare("com.webos.service.memorymanager") == 0;
  instances =
      WebAppManagerService::OnKillApp(app_id, instance_id, memory_reclaim);

  if (instances) {
    reply["appId"] = app_id;
    reply["instanceId"] = instance_id;
    reply["returnValue"] = true;
  } else {
    reply["returnValue"] = false;
    reply["errorCode"] = kErrCodeNoRunningApp;
    reply["errorText"] = kErrNoRunningApp;
  }
  return reply;
}

Json::Value WebAppManagerServiceLuna::pauseApp(const Json::Value& request) {
  Json::Value reply;

  if (!request.isObject() ||
      (!request.isMember("instanceId") || !request["instanceId"].isString())) {
    reply["returnValue"] = false;
    reply["errorCode"] = kErrCodeInvalidParam;
    reply["errorText"] = kErrInvalidParam;
    return reply;
  }

  std::string const id = request["instanceId"].asString();

  LOG_INFO(MSGID_LUNA_API, 2, PMLOGKS("INSTANCE_ID", id.c_str()),
           PMLOGKS("API", "pauseApp"), "");

  if (WebAppManagerService::OnPauseApp(id)) {
    reply["returnValue"] = true;
    reply["appId"] = request["appId"].asString();
    reply["instanceId"] = request["instanceId"].asString();
  } else {
    reply["returnValue"] = false;
    reply["errorCode"] = kErrCodeNoRunningApp;
    reply["errorText"] = kErrNoRunningApp;
  }
  return reply;
}

Json::Value WebAppManagerServiceLuna::setInspectorEnable(
    const Json::Value& /*request*/) {
  LOG_DEBUG("WebAppManagerService::SetInspectorEnable");
  Json::Value reply;
  std::string const error_message("Not supported on this platform");

  LOG_DEBUG("errorMessage : %s", error_message.c_str());
  reply["errorMessage"] = error_message;
  reply["returnValue"] = false;
  return reply;
}

Json::Value WebAppManagerServiceLuna::closeAllApps(
    const Json::Value& /*request*/) {
  bool const val = WebAppManagerService::OnCloseAllApps();

  Json::Value reply;
  reply["returnValue"] = val;
  return reply;
}

Json::Value WebAppManagerServiceLuna::logControl(const Json::Value& request) {
  if (!request.isObject() ||
      (!request.isMember("keys") || !request["keys"].isString()) ||
      (!request.isMember("value") || !request["value"].isString())) {
    Json::Value reply;
    reply["returnValue"] = false;
    reply["errorCode"] = kErrCodeInvalidParam;
    reply["errorText"] = kErrInvalidParam;
    return reply;
  }

  return WebAppManagerService::OnLogControl(request["keys"].asString(),
                                            request["value"].asString());
}

// Pushed in by the compositor whenever the shell's orientation settles, which
// is the link LunaSysMgr had internally as WebAppMgrProxy::setOrientation and
// which no longer exists now that the two are separate processes.
Json::Value WebAppManagerServiceLuna::setOrientation(
    const Json::Value& request) {
  Json::Value reply;

  if (!request.isObject() || !request.isMember("orientation") ||
      !request["orientation"].isString()) {
    reply["returnValue"] = false;
    reply["errorText"] = kErrInvalidParam;
    reply["errorCode"] = kErrCodeInvalidParam;
    return reply;
  }

  // Reported rather than swallowed: the value is validated in WebAppManager and
  // an unknown one is dropped, so answering true would tell the compositor its
  // orientation had been taken when it had not.
  if (!WebAppManagerService::SetOrientation(request["orientation"].asString())) {
    reply["returnValue"] = false;
    reply["errorText"] = kErrInvalidValue;
    reply["errorCode"] = kErrCodeInvalidParam;
    return reply;
  }

  reply["returnValue"] = true;
  return reply;
}

// The shell reporting whether a window it is about to take off the foreground
// stays on screen. A card shell keeps carded windows visible, and they have to
// go on painting; a shell where the foreground application is the only visible
// one leaves this alone and WAM suspends as it always did.
//
// Call this before changing the window state, and wait for the reply: the state
// travels over Wayland and this over the bus, and WAM has to have the answer by
// the time the state change arrives or it will suspend a window that is still
// being displayed.
Json::Value WebAppManagerServiceLuna::setAppVisibility(
    const Json::Value& request) {
  Json::Value reply;

  const bool has_instance_id =
      request.isObject() && request.isMember("instanceId") &&
      request["instanceId"].isString();
  const bool has_app_id = request.isObject() && request.isMember("appId") &&
                          request["appId"].isString();

  if ((!has_instance_id && !has_app_id) || !request.isMember("visible") ||
      !request["visible"].isBool()) {
    reply["returnValue"] = false;
    reply["errorText"] = kErrInvalidParam;
    reply["errorCode"] = kErrCodeInvalidParam;
    return reply;
  }

  const std::string instance_id =
      has_instance_id ? request["instanceId"].asString() : std::string();
  const std::string app_id =
      has_app_id ? request["appId"].asString() : std::string();
  const bool visible = request["visible"].asBool();

  if (!WebAppManagerService::SetAppShownWhileDeactivated(instance_id, app_id,
                                                         visible)) {
    reply["returnValue"] = false;
    reply["errorText"] = kErrNoRunningApp;
    reply["errorCode"] = kErrCodeNoRunningApp;
    return reply;
  }

  reply["returnValue"] = true;
  return reply;
}

Json::Value WebAppManagerServiceLuna::getWebProcessSize(
    const Json::Value& /*request*/) {
  return WebAppManagerService::GetWebProcessProfiling();
}

Json::Value WebAppManagerServiceLuna::listRunningApps(
    const Json::Value& request,
    bool /*subscribed*/) {
  bool const include_sys_apps = request["includeSysApps"] == true;

  std::vector<ApplicationInfo> const apps =
      WebAppManagerService::List(include_sys_apps);

  Json::Value reply;
  Json::Value running_apps;
  for (const ApplicationInfo& app_info : apps) {
    Json::Value app_json;
    app_json["id"] = app_info.app_id_;
    app_json["instanceId"] = app_info.instance_id_;
    app_json["webprocessid"] = std::to_string(app_info.pid_);
    running_apps.append(app_json);
  }
  reply["running"] = std::move(running_apps);
  reply["returnValue"] = true;
  return reply;
}

Json::Value WebAppManagerServiceLuna::clearBrowsingData(
    const Json::Value& request) {
  Json::Value reply;

  if (!request.isObject()) {
    reply["returnValue"] = false;
    reply["errorCode"] = kErrCodeInvalidParam;
    reply["errorText"] = kErrInvalidParam;
    return reply;
  }

  const Json::Value& clear_types = request["types"];
  bool return_value = true;
  int remove_browsing_data_mask = 0;

  switch (clear_types.type()) {
    case Json::ValueType::nullValue:
      remove_browsing_data_mask =
          WebAppManagerService::MaskForBrowsingDataType("all");
      break;
    case Json::ValueType::arrayValue: {
      if (clear_types.size() < 1) {
        reply["errorCode"] = kErrCodeClearDataBrawsingEmptyArray;
        reply["errorText"] = kErrEmptyArray;
        return_value = false;
        break;
      }

      for (const Json::Value& clear_type : clear_types) {
        if (!clear_type.isString()) {
          std::stringstream error_text;
          error_text << kErrInvalidValue << " (" << kErrOnlyAllowedForString
                     << ")";
          reply["errorCode"] = kErrCodeClearDataBrawsingInvalidValue;
          reply["errorText"] = error_text.str();
          return_value = false;
          break;
        }

        int const mask = WebAppManagerService::MaskForBrowsingDataType(
            clear_type.asString().c_str());
        if (mask == 0) {
          std::stringstream error_text;
          error_text << kErrUnknownData << " (" << clear_type.asString() << ")";
          reply["errorCode"] = kErrCodeClearDataBrawsingUnknownData;
          reply["errorText"] = error_text.str();
          return_value = false;
          break;
        }

        remove_browsing_data_mask |= mask;
      }
      break;
    }
    default:
      reply["errorCode"] = kErrCodeClearDataBrawsingInvalidValue;
      reply["errorText"] = kErrInvalidValue;
      return_value = false;
  }

  LOG_DEBUG("removeBrowsingDataMask: %d", remove_browsing_data_mask);

  if (return_value) {
    WebAppManagerService::OnClearBrowsingData(remove_browsing_data_mask);
  }

  reply["returnValue"] = return_value;
  return reply;
}

Json::Value WebAppManagerServiceLuna::fireNotificationEvent(
    const Json::Value& request) {
  std::string app_id;
  if (request.isMember("appId") && request["appId"].isString()) {
    app_id = request["appId"].asString();
  }
  std::string notification_id;
  if (request.isMember("notificationId") &&
      request["notificationId"].isString()) {
    notification_id = request["notificationId"].asString();
  }
  std::string origin;
  if (request.isMember("origin") && request["origin"].isString()) {
    origin = request["origin"].asString();
  }
  std::string type;
  if (request.isMember("type") && request["type"].isString()) {
    type = request["type"].asString();
  }
  if (app_id.empty() || notification_id.empty() || origin.empty() ||
      type.empty()) {
    Json::Value response;
    response["returnValue"] = false;
    response["errorCode"] = kErrCodeFireNotificationEventMissingParameter;
    response["errorText"] = kErrFireNotificationEventMissingParameter;
    return response;
  }
  if (type != "notificationclick" && type != "notificationclose") {
    Json::Value response;
    response["returnValue"] = false;
    response["errorCode"] = kErrCodeFireNotificationEventUnsupportedType;
    response["errorText"] = kErrFireNotificationEventUnsupportedType;
    return response;
  }
  auto action_index = std::make_pair(0, false);
  if (request.isMember("actionIndex") && request["actionIndex"].isInt()) {
    action_index.first = request["actionIndex"].asInt();
    action_index.second = true;
  }
  auto reply = std::make_pair(std::u16string(), false);
  if (request.isMember("reply") && request["reply"].isString()) {
    reply.first = util::Utf8ToUtf16(request["reply"].asString());
    reply.second = true;
  }
  auto dispatcher = neva_app_runtime::GetNotificationEventDispatcher();
  if (type == "notificationclose") {
    dispatcher->Close(notification_id, /*by_user=*/true);
  } else {
    dispatcher->Click(notification_id, std::move(origin), action_index, reply);
  }

  Json::Value response;
  response["returnValue"] = true;
  return response;
}

Json::Value WebAppManagerServiceLuna::answerPermissionPrompt(
    const Json::Value& request) {
  Json::Value response;
  if (!request["promptId"].isString() || !request["allow"].isBool()) {
    response["returnValue"] = false;
    response["errorCode"] = kErrCodeAnswerPermissionPromptMissingParameter;
    response["errorText"] = kErrAnswerPermissionPromptMissingParameter;
    return response;
  }
  if (!PermissionPrompt::Answer(request["promptId"].asString(),
                                request["allow"].asBool())) {
    response["returnValue"] = false;
    response["errorCode"] = kErrCodeAnswerPermissionPromptUnknownPrompt;
    response["errorText"] = kErrAnswerPermissionPromptUnknownPrompt;
    return response;
  }
  response["returnValue"] = true;
  return response;
}

namespace {

// The permissions Settings may look at and change, as the bus names them.
std::optional<PermissionRequest::RequestType> AppPermissionType(
    const Json::Value& value) {
  if (!value.isString()) {
    return std::nullopt;
  }
  const std::string name = value.asString();
  if (name == "geolocation") {
    return PermissionRequest::RequestType::kGeolocation;
  }
  if (name == "notifications") {
    return PermissionRequest::RequestType::kNotifications;
  }
  return std::nullopt;
}

// An application id as SAM knows them (reverse-DNS: letters, digits, '.', '-',
// '_'). It ends up in the per-app pattern Chromium stores the decision under,
// so nothing else may get that far.
bool IsValidAppId(const Json::Value& value) {
  std::string_view id;
  if (!value.isString() || !value.getString(&id)) {
    return false;
  }
  if (id.empty() || id.size() > 255) {
    return false;
  }
  for (const char c : id) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '.' || c == '-' ||
                    c == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}

Json::Value AppPermissionError(int code, const std::string& text) {
  Json::Value response;
  response["returnValue"] = false;
  response["errorCode"] = code;
  response["errorText"] = text;
  return response;
}

}  // namespace

// What each application was told for |permission|: the decisions stored when
// the user answered the prompt, plus the running system apps whose
// appinfo.json grants it without asking, which Settings cannot change.
Json::Value WebAppManagerServiceLuna::getAppPermissions(
    const Json::Value& request) {
  const std::optional<PermissionRequest::RequestType> type =
      AppPermissionType(request["permission"]);
  if (!type) {
    return AppPermissionError(kErrCodeAppPermissionInvalidPermission,
                              kErrAppPermissionInvalidPermission);
  }
  const std::string permission = request["permission"].asString();

  const std::map<std::string, std::string> stored =
      WebAppManagerService::GetAppPermissions(permission);

  Json::Value apps(Json::arrayValue);
  for (const auto& [app_id, setting] : stored) {
    Json::Value app;
    app["appId"] = app_id;
    app["setting"] = setting;
    app["system"] = false;
    apps.append(std::move(app));
  }

  // Only running apps: WAM holds no description of the others. A stored
  // decision wins over appinfo.json, since the prompt is never reached then.
  std::set<std::string> listed;
  for (const WebAppBase* app : WebAppManagerService::RunningApps()) {
    const ApplicationDescription* desc = app ? app->GetAppDescription() : nullptr;
    if (!desc || !desc->SystemApp() || stored.contains(desc->Id()) ||
        listed.contains(desc->Id()) ||
        !PermissionPrompt::GrantedByAppInfo(*desc, *type)) {
      continue;
    }
    listed.insert(desc->Id());
    Json::Value entry;
    entry["appId"] = desc->Id();
    entry["setting"] = "allow";
    entry["system"] = true;
    apps.append(std::move(entry));
  }

  Json::Value response;
  response["returnValue"] = true;
  response["permission"] = permission;
  response["apps"] = std::move(apps);
  return response;
}

Json::Value WebAppManagerServiceLuna::setAppPermission(
    const Json::Value& request) {
  if (!AppPermissionType(request["permission"])) {
    return AppPermissionError(kErrCodeAppPermissionInvalidPermission,
                              kErrAppPermissionInvalidPermission);
  }
  const Json::Value& setting = request["setting"];
  if (!setting.isString() ||
      (setting.asString() != "allow" && setting.asString() != "block" &&
       setting.asString() != "ask")) {
    return AppPermissionError(kErrCodeAppPermissionInvalidSetting,
                              kErrAppPermissionInvalidSetting);
  }
  const Json::Value& app_id = request["appId"];
  if (!IsValidAppId(app_id)) {
    return AppPermissionError(kErrCodeAppPermissionInvalidAppId,
                              kErrAppPermissionInvalidAppId);
  }

  WebAppManagerService::SetAppPermission(app_id.asString(),
                                         request["permission"].asString(),
                                         setting.asString());
  Json::Value response;
  response["returnValue"] = true;
  return response;
}

// Settings > Location "Clear My Location Data", and its notifications twin.
Json::Value WebAppManagerServiceLuna::resetAppPermissions(
    const Json::Value& request) {
  if (!AppPermissionType(request["permission"])) {
    return AppPermissionError(kErrCodeAppPermissionInvalidPermission,
                              kErrAppPermissionInvalidPermission);
  }
  WebAppManagerService::ResetAppPermissions(request["permission"].asString());
  Json::Value response;
  response["returnValue"] = true;
  return response;
}

void WebAppManagerServiceLuna::DidConnect() {
  Json::Value params;
  params["subscribe"] = true;

  params["serviceName"] = std::string("com.webos.settingsservice");
  if (!GET_LS2_SERVER_STATUS(SystemServiceConnectCallback, params)) {
    LOG_WARNING(MSGID_SERVICE_CONNECT_FAIL, 0,
                "Failed to connect to settingsservice");
  }

  params["serviceName"] = std::string("com.webos.service.memorymanager");
  if (!GET_LS2_SERVER_STATUS(MemoryManagerConnectCallback, params)) {
    LOG_WARNING(MSGID_MEMORY_CONNECT_FAIL, 0,
                "Failed to connect to memory manager");
  }

  params["serviceName"] = std::string("com.webos.applicationManager");
  if (!GET_LS2_SERVER_STATUS(ApplicationManagerConnectCallback, params)) {
    LOG_WARNING(MSGID_APPMANAGER_CONNECT_FAIL, 0,
                "Failed to connect to application manager");
  }

  params["serviceName"] = std::string("com.webos.service.systemservice");
  if (!GET_LS2_SERVER_STATUS(SystemPreferencesConnectCallback, params)) {
    LOG_WARNING(MSGID_SERVICE_CONNECT_FAIL, 0,
                "Failed to connect to systemservice");
  }

  params["serviceName"] = std::string("com.webos.service.config");
  if (!GET_LS2_SERVER_STATUS(ConfigServiceConnectCallback, params)) {
    LOG_WARNING(MSGID_SERVICE_CONNECT_FAIL, 0, "Failed to connect to configd");
  }

  params["serviceName"] = std::string("com.webos.service.connectionmanager");
  if (!GET_LS2_SERVER_STATUS(NetworkConnectionStatusCallback,
                             std::move(params))) {
    LOG_WARNING(MSGID_NETWORK_CONNECT_FAIL, 0,
                "Failed to connect to connectionmanager");
  }
}

// The compositor's output rotation, from the value the compositor itself uses.
//
// A panel can be mounted a quarter turn off from the way the device is held -
// the Minimal Phone MP01 scans out 800x600 in a portrait device - and
// surface-manager then rotates its whole output by the "r" field of
// com.webos.surfacemanager.compositorGeometry ("800x600+0+0r270s1"). The
// Wayland display this process sees still reports the panel's native size, and
// nothing in the window API carries the transform, so without this every app
// is initialised landscape: PalmSystem.deviceInfo reports 800 wide, Enyo lays
// out 800 pixels into a 600 pixel surface, and the page is only right once the
// device is turned the other way.
//
// Reading the same configd key keeps a single source for the geometry rather
// than a second declaration that could drift from the compositor's.
void WebAppManagerServiceLuna::ConfigServiceConnectCallback(
    const Json::Value& reply) {
  if (!reply.isObject() || reply["connected"] != true) {
    return;
  }
  Json::Value params;
  params["subscribe"] = true;
  Json::Value names;
  names.append("com.webos.surfacemanager.compositorGeometry");
  params["configNames"] = std::move(names);
  LS2_CALL(GetCompositorGeometryCallback,
           "luna://com.webos.service.config/getConfigs", std::move(params));
}

// Settings > Location "Location for Applications": whether applications may
// have the user's position at all. Subscribed again each time systemservice
// (re)connects; the value last heard is kept meanwhile, so a restart of the
// service does not switch location back on.
void WebAppManagerServiceLuna::SystemPreferencesConnectCallback(
    const Json::Value& reply) {
  if (!reply.isObject() || reply["connected"] != true) {
    return;
  }
  Json::Value params;
  params["subscribe"] = true;
  Json::Value keys(Json::arrayValue);
  keys.append("autoLocate");
  params["keys"] = std::move(keys);
  LS2_CALL(LocationPreferenceCallback,
           "luna://com.webos.service.systemservice/getPreferences",
           std::move(params));
}

void WebAppManagerServiceLuna::LocationPreferenceCallback(
    const Json::Value& reply) {
  // A key that was never set is left out of the reply, and later pushes carry
  // only keys that changed; both leave the current value alone.
  if (reply.isObject() && reply["autoLocate"].isBool()) {
    PermissionPrompt::SetLocationEnabled(reply["autoLocate"].asBool());
  }
}

void WebAppManagerServiceLuna::GetCompositorGeometryCallback(
    const Json::Value& reply) {
  const Json::Value& value =
      reply["configs"]["com.webos.surfacemanager.compositorGeometry"];
  if (!value.isString()) {
    // Absent on devices that leave the geometry to surface-manager, which
    // then applies no rotation either.
    return;
  }
  // "WxH+X+YrRsS" - only the rotation matters here.
  const std::string geometry = value.asString();
  const size_t r = geometry.find('r');
  if (r == std::string::npos) {
    return;
  }
  const int degrees = std::atoi(geometry.c_str() + r + 1);
  WebAppManagerService::SetDisplayRotation(degrees);
}

void WebAppManagerServiceLuna::SystemServiceConnectCallback(
    const Json::Value& reply) {
  if (!reply.isObject()) {
    LOG_WARNING(MSGID_APP_MGR_API_CALL_FAIL, 0, "%s", kErrInvalidParam.c_str());
    return;
  }

  if (reply["connected"] == true) {
    Json::Value locale_params;
    locale_params["subscribe"] = true;
    Json::Value locale_list;
    locale_list.append(std::string("localeInfo"));
    locale_params["keys"] = std::move(locale_list);
    LS2_CALL(GetSystemLocalePreferencesCallback,
             "luna://com.webos.settingsservice/getSystemSettings",
             std::move(locale_params));
  }
}

void WebAppManagerServiceLuna::GetSystemLocalePreferencesCallback(
    const Json::Value& reply) {
  if (!reply.isObject() || !reply["settings"].isObject()) {
    LOG_WARNING(MSGID_APP_MGR_API_CALL_FAIL, 0, "%s", kErrInvalidParam.c_str());
    return;
  }

  Json::Value locale_info = reply["settings"]["localeInfo"];

  // LocaleInfo(language, etc) is empty when service is crashed
  // The right value will be notified again when service is restarted
  if (!locale_info.isObject() || locale_info.empty() ||
      !locale_info["locales"].isObject() ||
      !locale_info["locales"]["UI"].isString()) {
    std::string const doc = util::JsonToString(reply);
    LOG_WARNING(MSGID_RECEIVED_INVALID_SETTINGS, 1,
                PMLOGKFV("MSG", "%s", doc.c_str()), "");
    return;
  }

  std::string const language(locale_info["locales"]["UI"].asString());

  LOG_INFO(MSGID_SETTING_SERVICE, 1,
           PMLOGKS("LANGUAGE", language.empty() ? "None" : language.c_str()),
           "");

  if (language.empty()) {
    return;
  }

  if (language.compare(WebAppManagerService::GetSystemLanguage()) == 0) {
    return;
  }

  WebAppManagerService::SetSystemLanguage(language.c_str());
}

void WebAppManagerServiceLuna::MemoryManagerConnectCallback(
    const Json::Value& reply) {
  if (!reply.isObject()) {
    LOG_WARNING(MSGID_APP_MGR_API_CALL_FAIL, 0, "%s", kErrInvalidParam.c_str());
    return;
  }

  if (reply["connected"] == true) {
    Json::Value close_app_obj;
    close_app_obj["subscribe"] = true;
    close_app_obj["type"] = "killing";

    if (!Call<WebAppManagerServiceLuna,
              &WebAppManagerServiceLuna::GetCloseAppIdCallback>(
            "luna://com.webos.service.memorymanager/getManagerEvent",
            std::move(close_app_obj), this)) {
      LOG_WARNING(MSGID_MEM_MGR_API_CALL_FAIL, 0,
                  "Failed to get close application identifier");
    }

    Json::Value threshold_changed;
    threshold_changed["subscribe"] = true;
    threshold_changed["category"] = "/com/webos/service/memorymanager";
    threshold_changed["method"] = "levelChanged";
    if (!Call<WebAppManagerServiceLuna,
              &WebAppManagerServiceLuna::ThresholdChangedCallback>(
            "luna://com.palm.bus/signal/addmatch", std::move(threshold_changed),
            this)) {
      LOG_WARNING(MSGID_SIGNAL_REGISTRATION_FAIL, 0,
                  "Failed to register a client for thresholdChanged");
    }
  }
}

void WebAppManagerServiceLuna::GetCloseAppIdCallback(const Json::Value& reply) {
  if (!reply.isObject() || !reply["pid"].isUInt() ||
      !reply["instanceId"].isString()) {
    LOG_WARNING(MSGID_APP_MGR_API_CALL_FAIL, 0, "%s", kErrInvalidParam.c_str());
    return;
  }

  std::string const app_id = reply["id"].asString();
  std::string const instance_id = reply["instanceId"].asString();

  if (!app_id.empty() && !instance_id.empty()) {
    WebAppManagerService::SetForceCloseApp(app_id.c_str(), instance_id.c_str());
  }
}

void WebAppManagerServiceLuna::ThresholdChangedCallback(
    const Json::Value& reply) {
  if (!reply.isObject() || !reply["current"].isString()) {
    LOG_WARNING(MSGID_APP_MGR_API_CALL_FAIL, 0, "%s", kErrInvalidParam.c_str());
    return;
  }

  std::string const current_level = reply["current"].asString();
  if (current_level.empty()) {
    LOG_DEBUG("thresholdChanged without level");
    return;
  }
  LOG_INFO(MSGID_NOTIFY_MEMORY_STATE, 1,
           PMLOGKS("State", current_level.c_str()), "");

  webos::WebViewBase::MemoryPressureLevel level =
      webos::WebViewBase::MEMORY_PRESSURE_NONE;
  if (current_level.compare("medium") == 0) {
    level = webos::WebViewBase::MEMORY_PRESSURE_LOW;
  } else if (current_level.compare("critical") == 0 ||
             current_level.compare("low") == 0) {
    level = webos::WebViewBase::MEMORY_PRESSURE_CRITICAL;
  }
  WebAppManagerService::NotifyMemoryPressure(level);
}

void WebAppManagerServiceLuna::ApplicationManagerConnectCallback(
    const Json::Value& reply) {
  if (!reply.isObject()) {
    LOG_WARNING(MSGID_APP_MGR_API_CALL_FAIL, 0, "%s", kErrInvalidParam.c_str());
    return;
  }

  if (reply["connected"] == true) {
    Json::Value params;
    params["subscribe"] = true;

    if (!Call<WebAppManagerServiceLuna,
              &WebAppManagerServiceLuna::GetAppStatusCallback>(
            "luna://com.webos.applicationManager/listApps", params, this)) {
      LOG_WARNING(MSGID_APP_MGR_API_CALL_FAIL, 0,
                  "Failed to get an application list");
    }

    params["extraInfo"] = true;
    if (!Call<WebAppManagerServiceLuna,
              &WebAppManagerServiceLuna::GetForegroundAppInfoCallback>(
            "luna://com.webos.applicationManager/getForegroundAppInfo",
            std::move(params), this)) {
      LOG_WARNING(MSGID_APP_MGR_API_CALL_FAIL, 0,
                  "Failed to get foreground application Information");
    }
  }
}

void WebAppManagerServiceLuna::GetAppStatusCallback(const Json::Value& reply) {
  if (!reply.isObject() || !reply["app"].isObject() ||
      !reply["change"].isString()) {
    LOG_WARNING(MSGID_APP_MGR_API_CALL_FAIL, 0, "%s", kErrInvalidParam.c_str());
    return;
  }

  std::string const change_kind = reply["change"].asString();
  const Json::Value& app_object = reply["app"];

  if (change_kind.compare("removed") == 0) {
    std::string const app_id =
        app_object["id"].isString() ? app_object["id"].asString() : "";
    LOG_INFO(MSGID_WAM_DEBUG, 0, "Application removed %s", app_id.c_str());
    WebAppManagerService::OnAppRemoved(app_id);
  }
  if (change_kind.compare("added") == 0) {
    std::string const app_id =
        app_object["id"].isString() ? app_object["id"].asString() : "";
    LOG_INFO(MSGID_WAM_DEBUG, 0, "Application installed %s", app_id.c_str());
    WebAppManagerService::OnAppInstalled(app_id);
  }
}

void WebAppManagerServiceLuna::GetForegroundAppInfoCallback(
    const Json::Value& reply) {
  if (!reply.isObject()) {
    LOG_WARNING(MSGID_APP_MGR_API_CALL_FAIL, 0, "%s", kErrInvalidParam.c_str());
    return;
  }

  if (reply["returnValue"] == true) {
    if (reply.isMember("appId") && reply["appId"].isString()) {
      std::string const app_id = reply["appId"].asString();
      webos::Runtime::GetInstance()->SetIsForegroundAppEnyo(
          WebAppManagerService::IsEnyoApp(app_id.c_str()));
    }
  }
}

void WebAppManagerServiceLuna::CloseApp(const std::string& id) {
  Json::Value json;
  json["instanceId"] = id;

  if (!LS2_CALL(CloseAppCallback, "luna://com.webos.applicationManager/close",
                std::move(json))) {
    LOG_WARNING(MSGID_CLOSE_CALL_FAIL, 0,
                "Failed to send closeByAppId command to SAM");
  }
}

void WebAppManagerServiceLuna::CloseAppCallback(const Json::Value& /*reply*/) {
  // TODO: check reply and close app again.
}

Json::Value WebAppManagerServiceLuna::webProcessCreated(
    const Json::Value& request,
    bool subscribed) {
  Json::Value reply;

  if (!request.isObject()) {
    reply["returnValue"] = false;
    reply["errorCode"] = kErrCodeInvalidParam;
    reply["errorText"] = kErrInvalidParam;
    return reply;
  }

  std::string const app_id =
      request["appId"].isString() ? request["appId"].asString() : "";
  if (!app_id.empty()) {
    std::string const instance_id = request["instanceId"].isString()
                                  ? request["instanceId"].asString()
                                  : "";
    int const pid = WebAppManagerService::GetWebProcessId(app_id.c_str(),
                                                    instance_id.c_str());
    reply["id"] = app_id;
    reply["instanceId"] = instance_id;

    if (pid) {
      reply["webprocessid"] = pid;
      reply["returnValue"] = true;
    } else {
      reply["returnValue"] = false;
      reply["errorText"] = "process is not running";
    }
  } else if (subscribed) {
    reply["returnValue"] = true;
  } else {
    reply["returnValue"] = false;
    reply["errorCode"] = kErrCodeInvalidParam;
    reply["errorText"] = kErrInvalidParam;
  }

  return reply;
}

void WebAppManagerServiceLuna::NetworkConnectionStatusCallback(
    const Json::Value& reply) {
  if (!reply.isObject()) {
    LOG_WARNING(MSGID_APP_MGR_API_CALL_FAIL, 0, "%s", kErrInvalidParam.c_str());
    return;
  }

  if (reply["connected"] == true) {
    LOG_DEBUG("connectionmanager is connected");
    Json::Value subscribe;
    subscribe["subscribe"] = true;
    if (!LS2_CALL(GetNetworkConnectionStatusCallback,
                  "luna://com.palm.connectionmanager/getStatus",
                  std::move(subscribe))) {
      LOG_WARNING(MSGID_LS2_CALL_FAIL, 0,
                  "Fail to subscribe to connection manager");
    }
  }
}

void WebAppManagerServiceLuna::GetNetworkConnectionStatusCallback(
    const Json::Value& reply) {
  // luna-send -f -n 1 luna://com.webos.service.connectionmanager/getstatus
  // '{"subscribe": true}'
  WebAppManagerService::UpdateNetworkStatus(reply);
}
