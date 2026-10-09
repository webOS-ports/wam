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

// What web apps may do without asking, how the permission prompt decides,
// and the bus methods Settings uses to list and change those decisions.

#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <glib.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "application_description.h"
#include "base_mock_initializer.h"
#include "blink_web_process_manager_mock.h"
#include "permission_prompt.h"
#include "platform_module_factory_impl_mock.h"
#include "utils.h"
#include "web_app_manager.h"
#include "web_app_manager_service.h"
#include "web_app_manager_service_luna.h"

namespace {

using RequestType = PermissionRequest::RequestType;

std::unique_ptr<ApplicationDescription> AppDesc(bool system_app,
                                                const std::string& extra) {
  // A stream rather than string concatenation: GCC 15 at -O2 warns about
  // a bogus allocation size inside libc++'s inlined string growth
  // (-Walloc-size-larger-than, armv7 and x86-64)
  std::ostringstream stream;
  stream << "{\"id\":\"com.example.app\",\"systemApp\":"
         << (system_app ? "true" : "false") << extra << '}';
  const std::string json = stream.str();
  return ApplicationDescription::FromJsonString(json.c_str());
}

class FakeRequest : public PermissionRequest {
 public:
  explicit FakeRequest(RequestType type) : type_(type) {}
  const std::string& RequestingOrigin() const override { return origin_; }
  RequestType GetRequestType() const override { return type_; }
  void PermissionGranted(bool) override {}
  void PermissionDenied() override {}
  void Cancelled() override {}
  void RequestFinished() override {}

 private:
  RequestType type_;
  std::string origin_ = "file:///";
};

// Counts what the prompt decided. The application is not running, so no
// appinfo.json grants anything.
class FakeDelegate : public neva_app_runtime::PermissionPrompt::Delegate {
 public:
  explicit FakeDelegate(std::vector<RequestType> types) {
    for (RequestType type : types) {
      owned_.push_back(std::make_unique<FakeRequest>(type));
      requests_.push_back(owned_.back().get());
    }
  }
  const std::vector<PermissionRequest*>& Requests() override {
    return requests_;
  }
  const std::string& GetAppId() const override { return app_id_; }
  void Accept() override { ++accepted; }
  void AcceptThisTime() override { ++accepted; }
  void Deny() override { ++denied; }
  void Closing() override { ++closed; }

  int accepted = 0;
  int denied = 0;
  int closed = 0;
  int Decisions() const { return accepted + denied + closed; }

 private:
  std::vector<std::unique_ptr<FakeRequest>> owned_;
  std::vector<PermissionRequest*> requests_;
  std::string app_id_ = "com.example.notrunning";
};

// WebAppManager keeps the process manager (and so the mock) for the rest of
// the run, so expectations are checked here rather than at its destruction.
BlinkWebProcessManagerMock* ProcessManagerMock() {
  auto* mock = static_cast<BlinkWebProcessManagerMock*>(
      WebAppManager::Instance()->GetWebProcessManager());
  testing::Mock::AllowLeak(mock);
  return mock;
}

void RunPendingIdle() {
  while (g_main_context_iteration(nullptr, FALSE)) {
  }
}

// Restores the location switch whatever a test did to it.
class LocationSwitch {
 public:
  ~LocationSwitch() { PermissionPrompt::SetLocationEnabled(true); }
};

}  // namespace

TEST(GrantedByAppInfoTest, NotificationsFromAnyApp) {
  const std::string grant = ",\"webAppPermissions\":[\"notifications\"]";
  EXPECT_TRUE(PermissionPrompt::GrantedByAppInfo(*AppDesc(false, grant),
                                                 RequestType::kNotifications));
  EXPECT_TRUE(PermissionPrompt::GrantedByAppInfo(*AppDesc(true, grant),
                                                 RequestType::kNotifications));
}

TEST(GrantedByAppInfoTest, GeolocationOnlyFromSystemApps) {
  const std::string grant = ",\"webAppPermissions\":[\"geolocation\"]";
  EXPECT_TRUE(PermissionPrompt::GrantedByAppInfo(*AppDesc(true, grant),
                                                 RequestType::kGeolocation));
  EXPECT_FALSE(PermissionPrompt::GrantedByAppInfo(*AppDesc(false, grant),
                                                  RequestType::kGeolocation));
}

TEST(GrantedByAppInfoTest, TrustLevelDoesNotMakeASystemApp) {
  // trustLevel is the app's own claim; only SAM's systemApp counts.
  EXPECT_FALSE(PermissionPrompt::GrantedByAppInfo(
      *AppDesc(false,
               ",\"trustLevel\":\"oem\",\"webAppPermissions\":[\"geolocation\"]"),
      RequestType::kGeolocation));
}

TEST(GrantedByAppInfoTest, NothingWithoutAGrant) {
  EXPECT_FALSE(PermissionPrompt::GrantedByAppInfo(*AppDesc(true, ""),
                                                  RequestType::kGeolocation));
  EXPECT_FALSE(PermissionPrompt::GrantedByAppInfo(*AppDesc(true, ""),
                                                  RequestType::kNotifications));
  EXPECT_FALSE(PermissionPrompt::GrantedByAppInfo(
      *AppDesc(true, ",\"webAppPermissions\":[\"notifications\"]"),
      RequestType::kGeolocation));
}

TEST(GrantedByAppInfoTest, CaptureFromAppInfoFlags) {
  EXPECT_TRUE(PermissionPrompt::GrantedByAppInfo(
      *AppDesc(false, ",\"allowVideoCapture\":true"),
      RequestType::kCameraStream));
  EXPECT_FALSE(PermissionPrompt::GrantedByAppInfo(*AppDesc(false, ""),
                                                  RequestType::kCameraStream));
  EXPECT_TRUE(PermissionPrompt::GrantedByAppInfo(
      *AppDesc(false, ",\"allowAudioCapture\":true"), RequestType::kMicStream));
  EXPECT_FALSE(PermissionPrompt::GrantedByAppInfo(*AppDesc(false, ""),
                                                  RequestType::kMicStream));
}

TEST(PermissionPromptTest, DecidesOnlyOnceTheMainLoopRuns) {
  FakeDelegate delegate({RequestType::kCameraStream});
  auto prompt = std::make_unique<PermissionPrompt>(&delegate);
  EXPECT_EQ(delegate.Decisions(), 0);
  RunPendingIdle();
  EXPECT_EQ(delegate.denied, 1);
  EXPECT_EQ(delegate.Decisions(), 1);
}

TEST(PermissionPromptTest, DestroyedBeforeDecidingDecidesNothing) {
  FakeDelegate delegate({RequestType::kCameraStream});
  auto prompt = std::make_unique<PermissionPrompt>(&delegate);
  prompt.reset();
  RunPendingIdle();
  EXPECT_EQ(delegate.Decisions(), 0);
}

TEST(PermissionPromptTest, UnknownRequestTypeIsDenied) {
  FakeDelegate delegate({RequestType::kInvalid});
  auto prompt = std::make_unique<PermissionPrompt>(&delegate);
  RunPendingIdle();
  EXPECT_EQ(delegate.denied, 1);
  EXPECT_EQ(delegate.Decisions(), 1);
}

TEST(PermissionPromptTest, UnknownTypeIsNotGrantedAlongWithOthers) {
  // Notifications alone would be asked; an unknown type next to it must not
  // ride along on that answer.
  FakeDelegate delegate({RequestType::kNotifications, RequestType::kInvalid});
  auto prompt = std::make_unique<PermissionPrompt>(&delegate);
  RunPendingIdle();
  EXPECT_EQ(delegate.accepted, 0);
  EXPECT_EQ(delegate.denied, 1);
}

TEST(PermissionPromptTest, LocationSwitchedOffIsDismissedNotDenied) {
  LocationSwitch restore;
  PermissionPrompt::SetLocationEnabled(false);
  FakeDelegate delegate({RequestType::kGeolocation});
  auto prompt = std::make_unique<PermissionPrompt>(&delegate);
  RunPendingIdle();
  // A denial would be stored as the app's own answer.
  EXPECT_EQ(delegate.denied, 0);
  EXPECT_EQ(delegate.closed, 1);
  EXPECT_EQ(delegate.accepted, 0);
}

TEST(PermissionPromptTest, MixedQuestionsAreLeftUndecided) {
  FakeDelegate delegate(
      {RequestType::kNotifications, RequestType::kGeolocation});
  auto prompt = std::make_unique<PermissionPrompt>(&delegate);
  RunPendingIdle();
  EXPECT_EQ(delegate.accepted, 0);
  EXPECT_EQ(delegate.denied, 0);
  EXPECT_EQ(delegate.closed, 1);
}

TEST(PermissionPromptTest, AnswerForAPromptThatIsNotWaiting) {
  EXPECT_FALSE(PermissionPrompt::Answer("com.example.app-424242", true));
  EXPECT_FALSE(PermissionPrompt::Answer("", false));
}

TEST(AppPermissionsLunaTest, GetRejectsUnknownPermission) {
  Json::Value request;
  request["permission"] = "camera";
  const auto reply =
      WebAppManagerServiceLuna::Instance()->getAppPermissions(request);
  EXPECT_FALSE(reply["returnValue"].asBool());
  EXPECT_EQ(reply["errorCode"].asInt(), kErrCodeAppPermissionInvalidPermission);

  const auto missing = WebAppManagerServiceLuna::Instance()->getAppPermissions(
      Json::Value(Json::objectValue));
  EXPECT_EQ(missing["errorCode"].asInt(),
            kErrCodeAppPermissionInvalidPermission);
}

TEST(AppPermissionsLunaTest, SetValidatesEveryParameter) {
  auto set = [](const Json::Value& request) {
    return WebAppManagerServiceLuna::Instance()
        ->setAppPermission(request)["errorCode"]
        .asInt();
  };
  Json::Value good;
  good["appId"] = "com.example.app";
  good["permission"] = "geolocation";
  good["setting"] = "block";

  Json::Value request = good;
  request["permission"] = "microphone";
  EXPECT_EQ(set(request), kErrCodeAppPermissionInvalidPermission);

  request = good;
  request["setting"] = "maybe";
  EXPECT_EQ(set(request), kErrCodeAppPermissionInvalidSetting);
  request["setting"] = true;
  EXPECT_EQ(set(request), kErrCodeAppPermissionInvalidSetting);

  for (const Json::Value& bad_id :
       {Json::Value(""), Json::Value("com/example"), Json::Value("a b"),
        Json::Value("app\"id"), Json::Value(std::string(256, 'a')),
        Json::Value(42), Json::Value(Json::nullValue)}) {
    request = good;
    request["appId"] = bad_id;
    EXPECT_EQ(set(request), kErrCodeAppPermissionInvalidAppId)
        << "appId " << bad_id.toStyledString();
  }
}

TEST(AppPermissionsLunaTest, ResetRejectsUnknownPermission) {
  Json::Value request;
  request["permission"] = 7;
  const auto reply =
      WebAppManagerServiceLuna::Instance()->resetAppPermissions(request);
  EXPECT_FALSE(reply["returnValue"].asBool());
  EXPECT_EQ(reply["errorCode"].asInt(), kErrCodeAppPermissionInvalidPermission);
}

TEST(AppPermissionsLunaTest, ValidRequestsReachTheProcessManager) {
  BaseMockInitializer<NiceWebViewMock, NiceWebAppWindowMock,
                      PlatformModuleFactoryImplMock> const mock_initializer;
  auto* process_manager = ProcessManagerMock();

  EXPECT_CALL(*process_manager,
              SetAppPermission("com.example.app_1", "notifications", "ask"));
  Json::Value set;
  set["appId"] = "com.example.app_1";
  set["permission"] = "notifications";
  set["setting"] = "ask";
  EXPECT_TRUE(WebAppManagerServiceLuna::Instance()
                  ->setAppPermission(set)["returnValue"]
                  .asBool());

  EXPECT_CALL(*process_manager, ResetAppPermissions("geolocation"));
  Json::Value reset;
  reset["permission"] = "geolocation";
  EXPECT_TRUE(WebAppManagerServiceLuna::Instance()
                  ->resetAppPermissions(reset)["returnValue"]
                  .asBool());

  EXPECT_CALL(*process_manager, GetAppPermissions("geolocation"))
      .WillOnce(testing::Return(std::map<std::string, std::string>{
          {"com.example.allowed", "allow"}, {"com.example.blocked", "block"}}));
  const auto list =
      WebAppManagerServiceLuna::Instance()->getAppPermissions(reset);
  ASSERT_TRUE(list["returnValue"].asBool());
  EXPECT_EQ(list["permission"].asString(), "geolocation");
  ASSERT_EQ(list["apps"].size(), 2u);
  EXPECT_EQ(list["apps"][0]["appId"].asString(), "com.example.allowed");
  EXPECT_EQ(list["apps"][0]["setting"].asString(), "allow");
  EXPECT_FALSE(list["apps"][0]["system"].asBool());
  EXPECT_EQ(list["apps"][1]["setting"].asString(), "block");
  EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(process_manager));
}

TEST(AppPermissionsLunaTest, InvalidRequestsDoNotReachTheProcessManager) {
  BaseMockInitializer<NiceWebViewMock, NiceWebAppWindowMock,
                      PlatformModuleFactoryImplMock> const mock_initializer;
  auto* process_manager = ProcessManagerMock();
  EXPECT_CALL(*process_manager, SetAppPermission).Times(0);
  EXPECT_CALL(*process_manager, ResetAppPermissions).Times(0);

  Json::Value set;
  set["appId"] = "../etc";
  set["permission"] = "geolocation";
  set["setting"] = "allow";
  WebAppManagerServiceLuna::Instance()->setAppPermission(set);
  Json::Value reset;
  reset["permission"] = "everything";
  WebAppManagerServiceLuna::Instance()->resetAppPermissions(reset);
  EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(process_manager));
}

TEST(AnswerPermissionPromptLunaTest, MissingAndUnknownPrompt) {
  const auto missing =
      WebAppManagerServiceLuna::Instance()->answerPermissionPrompt(
          Json::Value(Json::objectValue));
  EXPECT_EQ(missing["errorCode"].asInt(),
            kErrCodeAnswerPermissionPromptMissingParameter);

  Json::Value request;
  request["promptId"] = "com.example.app-999999";
  request["allow"] = true;
  const auto unknown =
      WebAppManagerServiceLuna::Instance()->answerPermissionPrompt(request);
  EXPECT_EQ(unknown["errorCode"].asInt(),
            kErrCodeAnswerPermissionPromptUnknownPrompt);
}

// The bus wrappers hand a request to a method only if it parses AND is an
// object: the parser accepts these, and a member lookup on them asserts.
TEST(BusRequestTest, ParserAcceptsPayloadsThatAreNotObjects) {
  for (const char* payload : {"[]", "[{\"a\":1}]"}) {
    Json::Value value;
    EXPECT_TRUE(util::StringToJson(payload, value)) << payload;
    EXPECT_FALSE(value.isObject()) << payload;
  }
  // A bare value either fails to parse or is not an object; either way the
  // wrappers refuse it.
  for (const char* payload : {"42", "\"text\"", "null"}) {
    Json::Value value;
    EXPECT_FALSE(util::StringToJson(payload, value) && value.isObject())
        << payload;
  }
  Json::Value object;
  EXPECT_TRUE(util::StringToJson("{}", object));
  EXPECT_TRUE(object.isObject());
  Json::Value broken;
  EXPECT_FALSE(util::StringToJson("{\"a\":", broken));
}
