// Copyright (c) 2021 LG Electronics, Inc.
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

#include <memory>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "application_description.h"
#include "platform_module_factory_impl.h"
#include "temp_dir.h"
#include "utils.h"
#include "web_app_manager.h"
#include "web_app_manager_config.h"
#include "web_page_blink.h"
#include "web_page_blink_delegate.h"
#include "web_view.h"
#include "web_view_factory.h"
#include "web_view_mock.h"

namespace {

using ::testing::ByMove;
using ::testing::HasSubstr;
using ::testing::Return;

const std::string kAppDescString = R"({
  "accessibility": {
    "supportsAudioGuidance": false
  },
  "allowAudioCapture": true,
  "allowVideoCapture": true,
  "bgImage": "",
  "checkUpdateOnLaunch": true,
  "deeplinkingParams": "",
  "defaultWindowType": "card",
  "disableBackHistoryAPI": false,
  "favicon": "",
  "folderPath": "@FOLDER@",
  "handlesRelaunch": false,
  "hasPromotion": false,
  "icon": "icon.png",
  "icons": [],
  "id": "com.webos.app.test.webrtc",
  "imageForRecents": "",
  "inAppSetting": false,
  "inspectable": false,
  "largeIcon": "",
  "launchPointId": "com.webos.app.test.webrtc_default",
  "lockable": true,
  "lptype": "default",
  "main": "index.html",
  "noSplashOnLaunch": false,
  "privilegedJail": false,
  "removable": false,
  "customPlugin": true,
  "requiredPermissions": [
    "audio.operation",
    "media.operation",
    "mediapipeline.resourcemanagement",
    "mediapipeline.operation"
  ],
  "spinnerOnLaunch": true,
  "systemApp": true,
  "tileSize": "normal",
  "title": "Test WebRTC",
  "transparent": false,
  "trustLevel": "default",
  "type": "web",
  "uiRevision": 2,
  "unmovable": false,
  "vendor": "LG Silicon Valley Labs",
  "version": "1.0.0",
  "visible": true
})";

const std::string params = R"({"displayAffinity": 0, "instanceId": ""})";

class WebViewFactoryMock : public WebViewFactory {
 public:
  WebViewFactoryMock() = default;
  ~WebViewFactoryMock() override = default;

  MOCK_METHOD(std::unique_ptr<WebView>, CreateWebView, (), (override));

  // Handed to the page by CreateWebView(); the test keeps web_view_ to set
  // expectations on.
  std::unique_ptr<WebViewMock> owned_view_ =
      std::make_unique<NiceWebViewMock>();
  WebViewMock* web_view_ = owned_view_.get();
};

// Developer mode on, with a Tellurium nub of the test's choosing: the real
// configuration reads both from the device's file system.
class DevModeConfig : public WebAppManagerConfig {
 public:
  explicit DevModeConfig(std::string tellurium_nub_path)
      : tellurium_nub_path_(std::move(tellurium_nub_path)) {}
  bool IsDevModeEnabled() const override { return true; }
  std::string GetTelluriumNubPath() const override {
    return tellurium_nub_path_;
  }

 private:
  std::string tellurium_nub_path_;
};

class DevModeModules : public PlatformModuleFactoryImpl {
 public:
  explicit DevModeModules(std::string tellurium_nub_path)
      : tellurium_nub_path_(std::move(tellurium_nub_path)) {}

 protected:
  std::unique_ptr<WebAppManagerConfig> CreateWebAppManagerConfig() override {
    return std::make_unique<DevModeConfig>(tellurium_nub_path_);
  }

 private:
  std::string tellurium_nub_path_;
};

class WebPageBlinkTestSuite : public ::testing::Test {
 public:
  WebPageBlinkTestSuite();
  ~WebPageBlinkTestSuite() override;
  void SetUp() override;

 protected:
  // The application's folder: one of the test's own, with an entry page in
  // it, instead of one that only exists on a device.
  TempDir app_folder;
  std::shared_ptr<ApplicationDescription> description;
  std::unique_ptr<WebViewFactoryMock> factory;
};

}  // namespace

WebPageBlinkTestSuite::WebPageBlinkTestSuite() {
  WebAppManager::Instance()->SetPlatformModules(
      std::make_unique<PlatformModuleFactoryImpl>());
}

WebPageBlinkTestSuite::~WebPageBlinkTestSuite() = default;

void WebPageBlinkTestSuite::SetUp() {
  ASSERT_FALSE(app_folder.Path().empty());
  ASSERT_TRUE(app_folder.WriteFile("index.html", "<html></html>"));
  std::string desc = kAppDescString;
  util::ReplaceSubstr(desc, "@FOLDER@", app_folder.Path());
  description = ApplicationDescription::FromJsonString(desc.c_str());
  ASSERT_TRUE(description);
  factory = std::make_unique<WebViewFactoryMock>();
  EXPECT_CALL(*factory, CreateWebView())
      .WillOnce(Return(ByMove(std::move(factory->owned_view_))));
}

TEST_F(WebPageBlinkTestSuite, CheckWebViewInitializeCall) {
  description->SetDisplayAffinity(0);

  EXPECT_CALL(*factory->web_view_,
              Initialize("com.webos.app.test.webrtc0", app_folder.Path(),
                         "default", "", "", false));

  WebPageBlink web_page(wam::Url(description->EntryPoint()), *description,
                        params.c_str(), std::move(factory));
  web_page.Init();
}

TEST_F(WebPageBlinkTestSuite, CheckWebViewLoad) {
  EXPECT_CALL(*factory->web_view_,
              LoadUrl("file://" + app_folder.Path() + "/index.html"));

  WebPageBlink web_page(wam::Url(description->EntryPoint()), *description,
                        params.c_str(), std::move(factory));
  web_page.Init();
  web_page.Load();
}

TEST_F(WebPageBlinkTestSuite, AddCustomPluginDir) {
  ASSERT_TRUE(app_folder.MakeDir("plugins"));
  const std::string path = app_folder.Path() + "/plugins";

  EXPECT_CALL(*factory->web_view_, AddCustomPluginDir(path));
  EXPECT_CALL(*factory->web_view_, AddAvailablePluginDir(path));

  WebPageBlink web_page(wam::Url(description->EntryPoint()), *description,
                        params.c_str(), std::move(factory));
  web_page.Init();
}

TEST_F(WebPageBlinkTestSuite, PriviledgetPluginPath) {
  constexpr char path[] = "/usr/palm/applications/com.webos.app.test.webrtc/";
  constexpr char var_name[] = "PRIVILEGED_PLUGIN_PATH";
  // WebPageBlink reads PRIVILEGED_PLUGIN_PATH from the environment; the test
  // runs on a single thread.
  auto* const actual_value = getenv(var_name);  // NOLINT(concurrency-mt-unsafe)
  std::string test_value(path);
  if (!actual_value) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    int const result = setenv(var_name, path, false);
    ASSERT_FALSE(result);
  } else {
    test_value = actual_value;
  }

  EXPECT_CALL(*factory->web_view_, AddAvailablePluginDir(test_value));

  WebPageBlink web_page(wam::Url(description->EntryPoint()), *description,
                        params.c_str(), std::move(factory));
  web_page.Init();

  if (!actual_value) {
    int const result = unsetenv(var_name);  // NOLINT(concurrency-mt-unsafe)
    ASSERT_FALSE(result);
  }
}

TEST_F(WebPageBlinkTestSuite, addUserScript) {
  ASSERT_TRUE(app_folder.WriteFile("telluriumnub.js", "/* @class Telluriumnub */"));
  WebAppManager::Instance()->SetPlatformModules(
      std::make_unique<DevModeModules>(app_folder.Path() + "/telluriumnub.js"));
  ASSERT_TRUE(WebAppManager::Instance()->Config()->IsDevModeEnabled())
      << "Devmode should be enabled for this test";

  EXPECT_CALL(*factory->web_view_,
              AddUserScript(HasSubstr("@class Telluriumnub")));
  WebPageBlink web_page(wam::Url(description->EntryPoint()), *description,
                        params.c_str(), std::move(factory));
  web_page.Init();

  WebAppManager::Instance()->SetPlatformModules(
      std::make_unique<PlatformModuleFactoryImpl>());
}
