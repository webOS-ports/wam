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

// What WebAppWayland::SetWindowProperty() sends to the compositor.
// WebAppWayland itself cannot be built in a test (see stage_transitions_test),
// so this tests what it delegates to.

#include <string>

#include <gtest/gtest.h>

#include "window_property.h"

namespace {

// libwayland's limit for one message (WL_MAX_MESSAGE_SIZE).
constexpr size_t kWaylandMaxMessageBytes = 4096;

// The size of a request with two string arguments, as libwayland marshals
// it: an 8-byte header, then for each string a 32-bit length and its bytes
// with a terminating NUL, padded to 4 bytes.
size_t TwoStringRequestBytes(const std::string& a, const std::string& b) {
  auto string_bytes = [](const std::string& s) {
    return 4 + ((s.size() + 1 + 3) & ~static_cast<size_t>(3));
  };
  return 8 + string_bytes(a) + string_bytes(b);
}

}  // namespace

TEST(WindowPropertyTest, ShortValueIsSentAsItIs) {
  EXPECT_EQ(window_property::Fit("subtitle", "Inbox"), "Inbox");
  EXPECT_EQ(window_property::Fit("subtitle", ""), "");
}

// The Email dashboard that took WebAppMgr down: its page has no <title>, so
// it is titled with its URL, which carries the e-mail.
TEST(WindowPropertyTest, LongTitleIsCutToFitOneMessage) {
  const std::string title =
      "dashboard.html?enyoWindowParams=" + std::string(4100, 'x');
  ASSERT_GT(TwoStringRequestBytes("subtitle", title), kWaylandMaxMessageBytes);

  const auto fitted = window_property::Fit("subtitle", title);
  ASSERT_TRUE(fitted.has_value());
  const std::string sent = fitted.value_or(std::string());
  EXPECT_EQ(sent, title.substr(0, window_property::kMaxValueBytes));
  EXPECT_LE(TwoStringRequestBytes("subtitle", sent), kWaylandMaxMessageBytes);
}

TEST(WindowPropertyTest, CutDoesNotSplitACharacter) {
  // "a" then two-byte characters across the limit: the last one that does not
  // fit whole is left out.
  std::string value("a");
  while (value.size() <= window_property::kMaxValueBytes) {
    value += "\xC3\xA9";
  }
  EXPECT_EQ(window_property::Fit("subtitle", value),
            value.substr(0, window_property::kMaxValueBytes - 1));
}

TEST(WindowPropertyTest, TooLongNameIsNotSent) {
  EXPECT_FALSE(window_property::Fit(
                   std::string(window_property::kMaxNameBytes + 1, 'n'), "v")
                   .has_value());
  EXPECT_TRUE(window_property::Fit(
                  std::string(window_property::kMaxNameBytes, 'n'), "v")
                  .has_value());
}

// Whatever a page asks for, what is sent fits in one message.
TEST(WindowPropertyTest, LongestPropertyFitsOneMessage) {
  const std::string name(window_property::kMaxNameBytes, 'n');
  const auto fitted = window_property::Fit(name, std::string(65536, 'v'));
  ASSERT_TRUE(fitted.has_value());
  const std::string sent = fitted.value_or(std::string());
  EXPECT_EQ(sent.size(), window_property::kMaxValueBytes);
  EXPECT_LE(TwoStringRequestBytes(name, sent), kWaylandMaxMessageBytes);
}
