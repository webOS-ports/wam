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

#ifndef TESTS_MOCKS_BASE_MOCK_INITIALIZER_H_
#define TESTS_MOCKS_BASE_MOCK_INITIALIZER_H_

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "platform_module_factory_impl.h"
#include "web_app_factory_manager_mock.h"
#include "web_app_window_factory_mock.h"
#include "web_app_window_mock.h"
#include "web_view_factory_mock.h"
#include "web_view_mock.h"
#include "webos/window_group_configuration.h"

template <typename T = NiceWebViewMock,
          typename U = NiceWebAppWindowMock,
          typename P = PlatformModuleFactoryImpl>
class BaseMockInitializer {
 public:
  BaseMockInitializer() : mocks_(Install()) {}

  BaseMockInitializer(const BaseMockInitializer&) = delete;
  BaseMockInitializer& operator=(const BaseMockInitializer&) = delete;

  ~BaseMockInitializer() { WebAppManager::Instance()->CloseAllApps(); }

  // The mocks the first app launched gets.
  T* GetWebViewMock() { return mocks_.web_view; }
  U* GetWebAppWindowMock() { return mocks_.web_app_window; }

  // A view and a window for one more app: every page deletes its own view,
  // and the app its window.
  T* AddWebViewMock() { return AddWebViewMock(mocks_.web_view_factory); }
  U* AddWebAppWindowMock() {
    return AddWebAppWindowMock(mocks_.web_app_window_factory);
  }

 private:
  struct Mocks {
    // Owned by the web app factory WebAppManager holds.
    WebViewFactoryMock* web_view_factory;
    WebAppWindowFactoryMock* web_app_window_factory;
    T* web_view;
    U* web_app_window;
  };

  // Sets WebAppManager up with mock factories and one view and window.
  static Mocks Install() {
    WebAppManager::Instance()->SetPlatformModules(std::make_unique<P>());

    auto web_view_factory = std::make_unique<WebViewFactoryMock>();
    auto web_app_window_factory = std::make_unique<WebAppWindowFactoryMock>();
    const Mocks mocks{web_view_factory.get(), web_app_window_factory.get(),
                      AddWebViewMock(web_view_factory.get()),
                      AddWebAppWindowMock(web_app_window_factory.get())};

    auto web_app_factory_manager = std::make_unique<WebAppFactoryManagerMock>();
    web_app_factory_manager->SetWebViewFactory(std::move(web_view_factory));
    web_app_factory_manager->SetWebAppWindowFactory(
        std::move(web_app_window_factory));
    WebAppManager::Instance()->SetWebAppFactory(
        std::move(web_app_factory_manager));
    return mocks;
  }

  static T* AddWebViewMock(WebViewFactoryMock* factory) {
    auto web_view = std::make_unique<T>();
    T* const added = web_view.get();
    factory->SetWebView(std::move(web_view));
    return added;
  }
  static U* AddWebAppWindowMock(WebAppWindowFactoryMock* factory) {
    auto web_app_window = std::make_unique<U>();
    U* const added = web_app_window.get();
    factory->SetWebAppWindow(std::move(web_app_window));
    return added;
  }

  const Mocks mocks_;
};

#endif  // TESTS_MOCKS_BASE_MOCK_INITIALIZER_H_
