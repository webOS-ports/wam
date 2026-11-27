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

#ifndef PLATFORM_WINDOW_PROPERTY_H_
#define PLATFORM_WINDOW_PROPERTY_H_

#include <cstddef>
#include <optional>
#include <string>

// A window property goes to the compositor as one Wayland request, and
// libwayland refuses a message over 4096 bytes: the connection fails with a
// protocol error, which the web engine treats as fatal, so WebAppMgr and every
// app in it go down. Values come from pages - the page title becomes the
// "subtitle" property, and an Enyo window without a <title> is titled with its
// URL and the parameters in it (the Email dashboard's held the whole e-mail) -
// and from setWindowProperty(), so they are cut to fit.
namespace window_property {

// The longest name and value sent. Together, with the request's header and
// the strings' length words and padding, they stay well below the limit.
inline constexpr size_t kMaxNameBytes = 256;
inline constexpr size_t kMaxValueBytes = 2048;

// The value to send for property |name|: |value|, or as much of it as fits
// without splitting a UTF-8 character. Nothing for a name longer than
// kMaxNameBytes: no property the shell knows has one.
std::optional<std::string> Fit(const std::string& name,
                               const std::string& value);

}  // namespace window_property

#endif  // PLATFORM_WINDOW_PROPERTY_H_
