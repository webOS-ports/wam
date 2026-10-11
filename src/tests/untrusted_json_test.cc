// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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

// JSON that WebAppMgr does not write itself: an app's appinfo.json, launch
// parameters any app can pass, a page's PalmSystem calls and the replies of
// other services. jsoncpp throws on a value of the wrong type, and WebAppMgr
// catches nothing, so each of these used to end the process - and every
// running app with it.

#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "application_description.h"
#include "network_status.h"
#include "platform_module_factory_impl.h"
#include "utils.h"
#include "web_app_base_mock.h"
#include "web_app_factory_manager.h"
#include "web_app_manager.h"
#include "web_app_manager_service.h"
#include "web_app_manager_service_luna.h"
#include "web_app_manager_service_luna_impl.h"

namespace {

std::string Nested(int depth) {
  return std::string(static_cast<size_t>(depth), '[') +
         std::string(static_cast<size_t>(depth), ']');
}

// Makes no app, as WebAppFactoryManagerImpl does for a subtype no plugin
// handles: a launch that gets this far is refused with an error code.
class NoAppFactoryManager : public WebAppFactoryManager {
 public:
  WebAppBase* CreateWebApp(const std::string& /*win_type*/,
                           const ApplicationDescription& /*desc*/,
                           const std::string& /*app_type*/) override {
    return nullptr;
  }
  WebAppBase* CreateWebApp(const std::string& /*win_type*/,
                           WebPageBase* /*page*/,
                           const ApplicationDescription& /*desc*/,
                           const std::string& /*app_type*/) override {
    return nullptr;
  }
  WebPageBase* CreateWebPage(const std::string& /*win_type*/,
                             const wam::Url& /*url*/,
                             const ApplicationDescription& /*desc*/,
                             const std::string& /*app_type*/,
                             const std::string& /*launch_params*/) override {
    return nullptr;
  }
};

WebAppManagerServiceLunaImpl* LunaService() {
  return static_cast<WebAppManagerServiceLunaImpl*>(
      WebAppManagerServiceLuna::Instance());
}

}  // namespace

TEST(JsonAccessorsTest, ConvertWhatJsoncppConverts) {
  EXPECT_TRUE(util::JsonAsBool(Json::Value(true)));
  EXPECT_TRUE(util::JsonAsBool(Json::Value(1)));
  EXPECT_FALSE(util::JsonAsBool(Json::Value(), true));
  EXPECT_EQ(util::JsonAsInt(Json::Value(-7)), -7);
  EXPECT_EQ(util::JsonAsInt(Json::Value(3.0)), 3);
  EXPECT_EQ(util::JsonAsInt(Json::Value(true)), 1);
  EXPECT_DOUBLE_EQ(util::JsonAsDouble(Json::Value(2)), 2.0);
  EXPECT_EQ(util::JsonAsString(Json::Value("text")), "text");
  EXPECT_EQ(util::JsonAsString(Json::Value(5)), "5");
  EXPECT_EQ(util::JsonAsString(Json::Value(), "x"), "");
}

TEST(JsonAccessorsTest, FallBackWhereJsoncppThrows) {
  const Json::Value object(Json::objectValue);
  const Json::Value array(Json::arrayValue);
  EXPECT_TRUE(util::JsonAsBool(Json::Value("false"), true));
  EXPECT_FALSE(util::JsonAsBool(object));
  EXPECT_EQ(util::JsonAsInt(Json::Value("12"), -1), -1);
  EXPECT_EQ(util::JsonAsInt(Json::Value(1e12), -1), -1);
  EXPECT_EQ(util::JsonAsInt(array, -1), -1);
  EXPECT_DOUBLE_EQ(util::JsonAsDouble(Json::Value("1.5"), -1.0), -1.0);
  EXPECT_EQ(util::JsonAsString(object, "none"), "none");
  EXPECT_EQ(util::JsonAsString(array), "");
}

TEST(JsonAccessorsTest, MemberOfWhatIsNotAnObject) {
  Json::Value object;
  object["key"] = "value";
  EXPECT_EQ(util::JsonMember(object, "key").asString(), "value");
  EXPECT_TRUE(util::JsonMember(object, "other").isNull());

  Json::Value array(Json::arrayValue);
  array.append(1);
  EXPECT_TRUE(util::JsonMember(array, "key").isNull());
  EXPECT_TRUE(util::JsonMember(Json::Value("text"), "key").isNull());
  EXPECT_TRUE(util::JsonMember(Json::Value(), "key").isNull());
}

TEST(JsonAccessorsTest, DeeplyNestedDocumentIsRefused) {
  // jsoncpp's strict reader throws past a depth of 1000.
  Json::Value value;
  EXPECT_FALSE(util::StringToJson(Nested(1001), value));
  EXPECT_FALSE(util::StringToJson("{\"a\":" + Nested(2000) + "}", value));
  EXPECT_TRUE(util::StringToJson(Nested(100), value));
  EXPECT_TRUE(value.isArray());
  // Brackets inside a string are text, not nesting.
  EXPECT_TRUE(
      util::StringToJson("[\"" + Nested(1001) + "\\\"[[\", {\"a\": 1}]", value));
  EXPECT_EQ(value.size(), 2u);
}

TEST(UntrustedAppInfoTest, WronglyTypedMembersAreIgnored) {
  const auto desc = ApplicationDescription::FromJsonString(R"({
      "id": "com.example.types",
      "transparent": "true",
      "handlesRelaunch": "yes",
      "title": {"en": "Title"},
      "version": ["1"],
      "main": 3,
      "trustLevel": [],
      "allowVideoCapture": "true",
      "supportedEnyoBundleVersions": [{}, "2.5"],
      "keyFilterTable": [{"from": "a", "to": [], "modifier": {}}, 5],
      "windowGroup": {"name": "group", "owner": true,
                      "ownerInfo": {"layers": [1, {"name": "l", "z": 2}]}}
  })");
  ASSERT_TRUE(desc);
  EXPECT_EQ(desc->Id(), "com.example.types");
  EXPECT_FALSE(desc->IsTransparent());
  EXPECT_FALSE(desc->HandlesRelaunch());
  EXPECT_EQ(desc->Title(), "");
  EXPECT_EQ(desc->Version(), "");
  EXPECT_EQ(desc->EntryPoint(), "3");
  EXPECT_EQ(desc->TrustLevel(), "default");
  EXPECT_FALSE(desc->AllowVideoCapture());
  EXPECT_EQ(desc->SupportedEnyoBundleVersions().count("2.5"), 1u);
  const auto& key_filter = desc->KeyFilterTable();
  ASSERT_EQ(key_filter.size(), 1u);
  EXPECT_EQ(key_filter.begin()->first, 0);
  const auto owner = desc->GetWindowOwnerInfo();
  ASSERT_EQ(owner.layers.size(), 1u);
  EXPECT_EQ(owner.layers.at("l"), 2);
}

TEST(UntrustedAppInfoTest, WindowGroupThatIsNotAnObject) {
  const auto desc = ApplicationDescription::FromJsonString(
      R"({"id": "com.example.group", "windowGroup": [1, 2]})");
  ASSERT_TRUE(desc);
  EXPECT_TRUE(desc->GetWindowGroupInfo().name.empty());
  EXPECT_TRUE(desc->GetWindowOwnerInfo().layers.empty());
  EXPECT_TRUE(desc->GetWindowClientInfo().layer.empty());
}

class UntrustedLaunchParamsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    WebAppManager::Instance()->SetPlatformModules(
        std::make_unique<PlatformModuleFactoryImpl>());
    WebAppManager::Instance()->SetWebAppFactory(
        std::make_unique<NoAppFactoryManager>());
  }

  static Json::Value Launch(const Json::Value& parameters) {
    Json::Value request;
    request["appDesc"]["id"] = "com.example.params";
    request["appDesc"]["main"] = "index.html";
    request["instanceId"] = "8b1f0a46-6f71-4d23-9d6b-3f0a6a1f2b10";
    request["parameters"] = parameters;
    return WebAppManagerServiceLuna::Instance()->launchApp(request);
  }
};

TEST_F(UntrustedLaunchParamsTest, WronglyTypedParametersReachTheFactory) {
  // Any app can launch another with parameters of its choosing.
  Json::Value parameters;
  parameters["dockMode"] = "yes";
  parameters["windowType"] = Json::Value(Json::arrayValue);
  parameters["launchedHidden"] = "no";
  const Json::Value reply = Launch(parameters);
  EXPECT_FALSE(reply["returnValue"].asBool());
  EXPECT_EQ(reply["errorCode"].asInt(), kErrCodeLaunchappUnsupportedType);
}

TEST(UntrustedPreloadTest, PreloadStateFromWhateverIsPassed) {
  WebAppBaseMock app;
  app.SetPreloadState("[1]");
  EXPECT_EQ(app.GetPreloadState(), WebAppBase::kNonePreload);
  app.SetPreloadState(R"({"preload": {"full": true}})");
  EXPECT_EQ(app.GetPreloadState(), WebAppBase::kNonePreload);
  app.SetPreloadState(R"({"launchedHidden": "yes"})");
  EXPECT_EQ(app.GetPreloadState(), WebAppBase::kNonePreload);
  app.SetPreloadState(R"({"preload": "full"})");
  EXPECT_EQ(app.GetPreloadState(), WebAppBase::kFullPreload);
  app.SetAppProperties("[1]");
  app.SetAppProperties(R"("text")");
}

class UntrustedRepliesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    WebAppManager::Instance()->SetPlatformModules(
        std::make_unique<PlatformModuleFactoryImpl>());
    WebAppManager::Instance()->SetAccessibilityEnabled(false);
  }
  void TearDown() override {
    WebAppManager::Instance()->SetAccessibilityEnabled(false);
  }
};

TEST_F(UntrustedRepliesTest, SettingsReadEachKeyOnItsOwn) {
  // A subscription update carries only what changed. Audio guidance was read
  // only when "country" came with it, so switching it on was never seen.
  Json::Value reply;
  reply["returnValue"] = true;
  reply["settings"]["audioGuidance"] = "on";
  LunaService()->GetSystemOptionCallback(reply);
  EXPECT_TRUE(WebAppManager::Instance()->IsAccessibilityEnabled());

  reply["settings"]["country"] = "NLD";
  reply["settings"]["audioGuidance"] = Json::Value(Json::objectValue);
  reply["settings"]["screenRotation"] = Json::Value(Json::arrayValue);
  LunaService()->GetSystemOptionCallback(reply);
  EXPECT_TRUE(WebAppManager::Instance()->IsAccessibilityEnabled());
}

TEST_F(UntrustedRepliesTest, WronglyTypedRepliesAreIgnored) {
  Json::Value keyboard;
  keyboard["returnValue"] = "yes";
  keyboard["hardwareKeyboard"]["present"] = "yes";
  LunaService()->GetKeyboardStatusCallback(keyboard);
  keyboard["returnValue"] = true;
  LunaService()->GetKeyboardStatusCallback(keyboard);

  Json::Value close;
  close["pid"] = 1;
  close["instanceId"] = "8b1f0a46-6f71-4d23-9d6b-3f0a6a1f2b10";
  close["id"] = Json::Value(Json::objectValue);
  LunaService()->GetCloseAppIdCallback(close);

  Json::Value connection;
  connection["returnValue"] = "true";
  connection["isInternetConnectionAvailable"] = Json::Value(Json::objectValue);
  connection["wifi"]["netmask"] = Json::Value(Json::arrayValue);
  NetworkStatus status;
  status.FromJsonObject(connection);
  EXPECT_FALSE(status.IsInternetConnectionAvailable());
  connection["returnValue"] = true;
  status.FromJsonObject(connection);
  EXPECT_EQ(status.GetInformation().Netmask(), "");
}
