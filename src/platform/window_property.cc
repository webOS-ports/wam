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

#include "window_property.h"

#include "utils.h"

namespace window_property {

std::optional<std::string> Fit(const std::string& name,
                               const std::string& value) {
  if (name.size() > kMaxNameBytes) {
    return std::nullopt;
  }
  return util::TruncateUtf8(value, kMaxValueBytes);
}

}  // namespace window_property
