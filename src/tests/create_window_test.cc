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

// WebPageBlink::CreateWindow(), which the engine calls for window.open(): the
// webOS "attributes=" feature is page-controlled JSON and must not be able to
// take WebAppMgr down, whatever it holds.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "application_description.h"
#include "platform_module_factory_impl.h"
#include "url.h"
#include "web_app_base_mock.h"
#include "web_app_factory_manager.h"
#include "web_app_manager.h"
#include "web_page_blink.h"
#include "web_view_factory.h"
#include "web_view_mock.h"
#include "window_types.h"

namespace {

using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

constexpr char kAppDesc[] =
    R"({"id":"com.example.opener","defaultWindowType":"card",)"
    R"("main":"index.html","folderPath":"/usr/palm/applications/)"
    R"(com.example.opener","trustLevel":"default"})";

class NewViewFactory : public WebViewFactory {
 public:
  WebView* CreateWebView() override { return new NiceWebViewMock(); }
};

// A view mock that counts how many exist, to see that one is freed.
class CountedViewMock : public WebViewMock {
 public:
  CountedViewMock() { ++instances; }
  ~CountedViewMock() override { --instances; }
  CountedViewMock(const CountedViewMock&) = delete;
  CountedViewMock& operator=(const CountedViewMock&) = delete;

  static int instances;
};

int CountedViewMock::instances = 0;

class CountedViewFactory : public WebViewFactory {
 public:
  WebView* CreateWebView() override {
    return new ::testing::NiceMock<CountedViewMock>();
  }
};

using Properties = std::vector<std::pair<std::string, std::string>>;

// The application the window is created for. It keeps the page, as the real
// one does, and records the window properties set on it.
class WindowAppMock : public WebAppBaseMock {
 public:
  void Attach(WebPageBase* page) override { page_.reset(page); }
  void SetWindowProperty(const std::string& name,
                         const std::string& value) override {
    properties_.emplace_back(name, value);
  }

  WebPageBase* AttachedPage() const { return page_.get(); }
  const Properties& WindowProperties() const { return properties_; }

 private:
  std::unique_ptr<WebPageBase> page_;
  Properties properties_;
};

class WindowAppFactoryManager : public WebAppFactoryManager {
 public:
  // With |make_apps| false no app is made, as WebAppFactoryManagerImpl does
  // for a subtype no factory plugin handles.
  WindowAppFactoryManager(std::string* win_type,
                          WindowAppMock** app,
                          bool make_apps = true)
      : win_type_(win_type), app_(app), make_apps_(make_apps) {}

  WebAppBase* CreateWebApp(const std::string& win_type,
                           const ApplicationDescription& /*desc*/,
                           const std::string& /*app_type*/) override {
    *win_type_ = win_type;
    if (!make_apps_) {
      return nullptr;
    }
    *app_ = new WindowAppMock();
    return *app_;
  }
  WebAppBase* CreateWebApp(const std::string& win_type,
                           WebPageBase* /*page*/,
                           const ApplicationDescription& desc,
                           const std::string& app_type) override {
    return CreateWebApp(win_type, desc, app_type);
  }
  WebPageBase* CreateWebPage(const std::string& /*win_type*/,
                             const wam::Url& /*url*/,
                             const ApplicationDescription& /*desc*/,
                             const std::string& /*app_type*/,
                             const std::string& /*launch_params*/) override {
    return nullptr;
  }

 private:
  std::string* win_type_;
  WindowAppMock** app_;
  bool make_apps_;
};

class CreateWindowTest : public ::testing::Test {
 protected:
  void SetUp() override {
    WebAppManager::Instance()->SetPlatformModules(
        std::make_unique<PlatformModuleFactoryImpl>());
    WebAppManager::Instance()->SetWebAppFactory(
        std::make_unique<WindowAppFactoryManager>(&win_type_, &app_));
    desc_ = ApplicationDescription::FromJsonString(kAppDesc);
    ASSERT_TRUE(desc_);
    opener_ = std::make_unique<WebPageBlink>(
        wam::Url("file:///index.html"), *desc_, "{}",
        std::make_unique<NewViewFactory>());
    opener_->Init();
  }

  void TearDown() override {
    CloseWindow();
    opener_.reset();
  }

  void CloseWindow() {
    if (app_) {
      WebAppManager::Instance()->WebPageRemoved(app_->AttachedPage());
      WebAppManager::Instance()->AppDeleted(app_);
      delete app_;
      app_ = nullptr;
    }
  }

  // window.open() with these features. The new window has to be created
  // regardless of what they hold.
  void Open(const std::vector<std::string>& features) {
    const WebView* view = opener_->CreateWindow(
        "about:blank", std::make_unique<NewViewFactory>(), 0, features);
    EXPECT_NE(view, nullptr);
    ASSERT_NE(app_, nullptr);
  }

  // What the opener's appinfo.json asks for: the window type when the
  // attributes name none.
  static std::string DefaultType() {
    return WebAppManager::Instance()->WindowTypeFromString("card");
  }

  std::unique_ptr<ApplicationDescription> desc_;
  std::unique_ptr<WebPageBlink> opener_;
  std::string win_type_;
  WindowAppMock* app_ = nullptr;
};

}  // namespace

TEST_F(CreateWindowTest, NoAttributes) {
  ASSERT_NO_FATAL_FAILURE(Open({}));
  EXPECT_EQ(win_type_, DefaultType());
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
}

TEST_F(CreateWindowTest, PopupAlert) {
  ASSERT_NO_FATAL_FAILURE(Open({R"(attributes={"window":"popupalert"})"}));
  EXPECT_EQ(win_type_, kWtSystemUi);
  EXPECT_THAT(app_->WindowProperties(),
              UnorderedElementsAre(Pair("LuneOS_window", "popupalert")));
}

TEST_F(CreateWindowTest, AttributesBecomeWindowProperties) {
  ASSERT_NO_FATAL_FAILURE(
      Open({R"(attributes={"window":"dashboard","name":"x","height":5,)"
            R"("list":[1],"object":{"a":1},"empty":""})"}));

  EXPECT_EQ(win_type_, kWtFloating);
  // Values that have no string form (an array, an object) and empty ones are
  // left out; the others are passed on.
  EXPECT_THAT(app_->WindowProperties(),
              UnorderedElementsAre(Pair("LuneOS_window", "dashboard"),
                                   Pair("LuneOS_name", "x"),
                                   Pair("LuneOS_height", "5")));
}

TEST_F(CreateWindowTest, AttributesThatAreNotAnObject) {
  // Anything but an object is no attributes. An array parsed and then aborted
  // WebAppMgr inside jsoncpp, on the lookup of "window"; the scalars are
  // already rejected by the strict parser and are here so they stay harmless.
  for (const char* feature :
       {"attributes=[]", "attributes=[1]", "attributes=\"x\"",
        "attributes=5", "attributes=true", "attributes=null", "attributes=",
        "attributes={"}) {
    SCOPED_TRACE(feature);
    ASSERT_NO_FATAL_FAILURE(Open({feature}));
    EXPECT_EQ(win_type_, DefaultType());
    EXPECT_THAT(app_->WindowProperties(), IsEmpty());
    CloseWindow();
  }
}

TEST_F(CreateWindowTest, WindowValueThatIsNotAString) {
  ASSERT_NO_FATAL_FAILURE(Open({R"(attributes={"window":[]})"}));
  EXPECT_EQ(win_type_, DefaultType());
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
}

TEST_F(CreateWindowTest, LastAttributesFeatureWins) {
  ASSERT_NO_FATAL_FAILURE(
      Open({R"(attributes={"window":"popupalert"})", "attributes=[]"}));
  EXPECT_EQ(win_type_, DefaultType());
}

TEST_F(CreateWindowTest, AttributesOnlyAsTheFeatureKey) {
  // "attributes=" further into a feature is not the attributes feature; the
  // first 11 characters must not be skipped on the strength of it.
  ASSERT_NO_FATAL_FAILURE(
      Open({R"(01234567890{"window":"popupalert","a":"attributes="})"}));
  EXPECT_EQ(win_type_, DefaultType());
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
}

TEST_F(CreateWindowTest, NoAppForTheWindow) {
  WebAppManager::Instance()->SetWebAppFactory(
      std::make_unique<WindowAppFactoryManager>(&win_type_, &app_,
                                                /*make_apps=*/false));
  const int views = CountedViewMock::instances;

  const WebView* view = opener_->CreateWindow(
      "about:blank", std::make_unique<CountedViewFactory>(), 0, {});

  // No window, and nothing left behind: the page made for it, and the view
  // it took from the engine, are freed.
  EXPECT_EQ(view, nullptr);
  EXPECT_EQ(app_, nullptr);
  EXPECT_EQ(CountedViewMock::instances, views);
}
