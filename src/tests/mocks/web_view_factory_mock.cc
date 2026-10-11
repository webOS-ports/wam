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

#include "web_view_factory_mock.h"

#include <iostream>
#include <utility>

#include "web_view.h"

int WebViewFactoryMock::instances_ = 0;

WebViewFactoryMock::WebViewFactoryMock() {
  ++instances_;
}

WebViewFactoryMock::~WebViewFactoryMock() {
  --instances_;
}

std::unique_ptr<WebView> WebViewFactoryMock::CreateWebView() {
  if (!web_views_.empty()) {
    std::unique_ptr<WebView> next = std::move(web_views_.front());
    web_views_.pop_front();
    return next;
  }
  std::cerr << "WebViewFactoryMock::CreateWebView: no mock left; set one with SetWebView() for "
               "every view to be created"
            << '\n';
  return nullptr;
}

void WebViewFactoryMock::SetWebView(std::unique_ptr<WebView> web_view) {
  web_views_.push_back(std::move(web_view));
}
