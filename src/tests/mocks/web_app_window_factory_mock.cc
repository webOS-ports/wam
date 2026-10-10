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

#include "web_app_window_factory_mock.h"

#include <iostream>
#include <utility>

#include "web_app_window.h"

int WebAppWindowFactoryMock::instances_ = 0;

WebAppWindowFactoryMock::WebAppWindowFactoryMock() {
  ++instances_;
}

WebAppWindowFactoryMock::~WebAppWindowFactoryMock() {
  --instances_;
}

std::unique_ptr<WebAppWindow> WebAppWindowFactoryMock::CreateWindow() {
  if (!web_app_windows_.empty()) {
    std::unique_ptr<WebAppWindow> next = std::move(web_app_windows_.front());
    web_app_windows_.pop_front();
    return next;
  }
  std::cerr << "WebAppWindowFactoryMock::CreateWindow: no mock left; set one with SetWebAppWindow() for "
               "every window to be created"
            << '\n';
  return nullptr;
}

void WebAppWindowFactoryMock::SetWebAppWindow(std::unique_ptr<WebAppWindow> web_app_window) {
  web_app_windows_.push_back(std::move(web_app_window));
}
