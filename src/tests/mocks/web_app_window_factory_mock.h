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

#ifndef TESTS_MOCKS_WEB_APP_WINDOW_FACTORY_MOCK_H_
#define TESTS_MOCKS_WEB_APP_WINDOW_FACTORY_MOCK_H_

#include <deque>
#include <memory>

#include "web_app_window_factory.h"

class WebAppWindow;

class WebAppWindowFactoryMock : public WebAppWindowFactory {
 public:
  WebAppWindowFactoryMock();
  ~WebAppWindowFactoryMock() override;

  // How many exist: lets a test see that they are freed.
  static int Instances() { return instances_; }

  // Hands out what SetWebAppWindow() was given, each once and in order: the caller
  // takes it over. Two pages sharing one mock would both delete it.
  std::unique_ptr<WebAppWindow> CreateWindow() override;
  void SetWebAppWindow(std::unique_ptr<WebAppWindow> web_app_window);

 private:
  static int instances_;
  std::deque<std::unique_ptr<WebAppWindow>> web_app_windows_;
};

#endif  // TESTS_MOCKS_WEB_APP_WINDOW_FACTORY_MOCK_H_
