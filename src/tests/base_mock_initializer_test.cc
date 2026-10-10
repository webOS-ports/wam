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

// The test helpers that stand in for the web engine's factories: what they
// allocate has to be freed, launched or not.

#include <memory>
#include <utility>

#include <gtest/gtest.h>

#include "application_description.h"
#include "base_mock_initializer.h"
#include "platform_module_factory_impl.h"
#include "url.h"
#include "web_app_factory_manager_mock.h"
#include "web_app_manager.h"
#include "web_app_window_factory_mock.h"
#include "web_app_window_mock.h"
#include "web_page_base.h"
#include "web_view_factory.h"
#include "web_view_factory_mock.h"
#include "web_view_mock.h"

namespace {

class CountingViewFactory : public WebViewFactory {
 public:
  CountingViewFactory() { ++instances; }
  ~CountingViewFactory() override { --instances; }
  CountingViewFactory(const CountingViewFactory&) = delete;
  CountingViewFactory& operator=(const CountingViewFactory&) = delete;
  std::unique_ptr<WebView> CreateWebView() override {
    return std::make_unique<NiceWebViewMock>();
  }

  static int instances;
};

int CountingViewFactory::instances = 0;

}  // namespace

TEST(BaseMockInitializerTest, FactoriesFreedWhenNothingIsLaunched) {
  const int view_factories = WebViewFactoryMock::Instances();
  const int window_factories = WebAppWindowFactoryMock::Instances();
  {
    BaseMockInitializer<> const mock_initializer;
    EXPECT_EQ(WebViewFactoryMock::Instances(), view_factories + 1);
    EXPECT_EQ(WebAppWindowFactoryMock::Instances(), window_factories + 1);
  }
  // They stay with the web app factory WebAppManager holds until it is
  // replaced, as the next test's initializer does.
  WebAppManager::Instance()->SetWebAppFactory(
      std::make_unique<WebAppFactoryManagerMock>());
  EXPECT_EQ(WebViewFactoryMock::Instances(), view_factories);
  EXPECT_EQ(WebAppWindowFactoryMock::Instances(), window_factories);
}

TEST(WebAppFactoryManagerMockTest, OneViewFactoryServesSeveralPages) {
  WebAppManager::Instance()->SetPlatformModules(
      std::make_unique<PlatformModuleFactoryImpl>());
  auto desc = ApplicationDescription::FromJsonString(
      R"({"id":"com.example.app","main":"index.html"})");
  ASSERT_TRUE(desc);

  auto manager = std::make_unique<WebAppFactoryManagerMock>();
  manager->SetWebViewFactory(std::make_unique<CountingViewFactory>());
  ASSERT_EQ(CountingViewFactory::instances, 1);

  // Each page used to be handed the factory itself, so two pages both
  // deleted it.
  std::unique_ptr<WebPageBase> first(manager->CreateWebPage(
      "card", wam::Url("file:///index.html"), *desc, {}, "{}"));
  std::unique_ptr<WebPageBase> second(manager->CreateWebPage(
      "card", wam::Url("file:///index.html"), *desc, {}, "{}"));
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  first.reset();
  manager.reset();
  EXPECT_EQ(CountingViewFactory::instances, 1);
  second.reset();
  EXPECT_EQ(CountingViewFactory::instances, 0);
}

TEST(FactoryMockTest, EachMockIsHandedOutOnce) {
  // Two apps launched with one initializer used to get the same view and
  // window, and closing them deleted both twice.
  WebViewFactoryMock view_factory;
  auto first_view = std::make_unique<NiceWebViewMock>();
  auto second_view = std::make_unique<NiceWebViewMock>();
  const WebView* const first_view_ptr = first_view.get();
  const WebView* const second_view_ptr = second_view.get();
  view_factory.SetWebView(std::move(first_view));
  view_factory.SetWebView(std::move(second_view));
  std::unique_ptr<WebView> const view_a(view_factory.CreateWebView());
  std::unique_ptr<WebView> const view_b(view_factory.CreateWebView());
  EXPECT_EQ(view_a.get(), first_view_ptr);
  EXPECT_EQ(view_b.get(), second_view_ptr);
  EXPECT_EQ(view_factory.CreateWebView(), nullptr);

  WebAppWindowFactoryMock window_factory;
  auto first_window = std::make_unique<NiceWebAppWindowMock>();
  auto second_window = std::make_unique<NiceWebAppWindowMock>();
  const WebAppWindow* const first_window_ptr = first_window.get();
  const WebAppWindow* const second_window_ptr = second_window.get();
  window_factory.SetWebAppWindow(std::move(first_window));
  window_factory.SetWebAppWindow(std::move(second_window));
  std::unique_ptr<WebAppWindow> const window_a(window_factory.CreateWindow());
  std::unique_ptr<WebAppWindow> const window_b(window_factory.CreateWindow());
  EXPECT_EQ(window_a.get(), first_window_ptr);
  EXPECT_EQ(window_b.get(), second_window_ptr);
  EXPECT_EQ(window_factory.CreateWindow(), nullptr);
}

TEST(FactoryMockTest, MocksNeverHandedOutAreFreed) {
  // Checked by the sanitizer and valgrind runs: nothing may leak.
  WebViewFactoryMock view_factory;
  view_factory.SetWebView(std::make_unique<NiceWebViewMock>());
  WebAppWindowFactoryMock window_factory;
  window_factory.SetWebAppWindow(std::make_unique<NiceWebAppWindowMock>());
}
