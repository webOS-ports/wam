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

#include "web_app_manager_utils.h"

namespace {
// For PmLog, to meet maximum length(1024) 824 is selected approximately
constexpr std::string::size_type kUrlSizeLimit = 824;
}  // namespace

std::string WebAppManagerUtils::UrlForLog(const std::string& url) {
  std::string result = url.substr(0, url.find_first_of("?#"));

  const std::string::size_type scheme_end = result.find("://");
  if (scheme_end != std::string::npos) {
    const std::string::size_type authority = scheme_end + 3;
    const std::string::size_type at =
        result.rfind('@', result.find('/', authority));
    if (at != std::string::npos && at >= authority) {
      result.erase(authority, at + 1 - authority);
    }
  }
  return result.substr(0, kUrlSizeLimit);
}
