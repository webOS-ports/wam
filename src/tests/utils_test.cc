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

// NOTICE: the next ApplicationDescription fields are read from JSON
// but never used (at least in WebOS-OSE version of WAM):
// * ApplicationDescription::m_requestedWindowOrientation
//   (is read from "requestedWindowOrientation" tag)
// * ApplicationDescription::m_vendorExtension
//   (is read from "vendorExtension" tag)
//   is accessible via std::string& vendorExtension(), which is never called

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <limits>

#include "utils.h"

// Check for search order:
// searchPath/resources/<language>/<script>/<region>/html/fileName
// searchPath/resources/<language>/<region>/html/fileName
// searchPath/resources/<language>/html/fileName
// searchPath/resources/html/fileName
// searchPath/fileName

namespace {

using ::testing::StrEq;

constexpr char path[] = "/usr/share/localization/wam/loaderror.html";

}  // namespace

TEST(UtilsTestSuite, emptyErrorPagePath) {
  const auto& paths = util::GetErrorPagePaths("", "");
  ASSERT_TRUE(paths.empty());
}

TEST(UtilsTestSuite, nonEmptyErrorPagePath) {
  const auto& paths = util::GetErrorPagePaths(path, "");
  ASSERT_EQ(paths.size(), 2);
  EXPECT_THAT(
      paths[0],
      StrEq("/usr/share/localization/wam/resources/html/loaderror.html"));
  EXPECT_THAT(paths[1], ::testing::HasSubstr(path));
}

TEST(UtilsTestSuite, nonEmptyErrorPagePathWithLang) {
  constexpr char lang_tag[] = "en";
  const auto& paths = util::GetErrorPagePaths(path, lang_tag);
  ASSERT_EQ(paths.size(), 3);
  EXPECT_THAT(
      paths[0],
      StrEq("/usr/share/localization/wam/resources/en/html/loaderror.html"));
  EXPECT_THAT(
      paths[1],
      StrEq("/usr/share/localization/wam/resources/html/loaderror.html"));
  EXPECT_THAT(paths[2], StrEq(path));
}

TEST(UtilsTestSuite, nonEmptyErrorPagePathWithLangRegion) {
  constexpr char lang_tag[] = "en-US";
  const auto& paths = util::GetErrorPagePaths(path, lang_tag);
  ASSERT_EQ(paths.size(), 4);
  EXPECT_THAT(
      paths[0],
      StrEq("/usr/share/localization/wam/resources/en/US/html/loaderror.html"));
  EXPECT_THAT(
      paths[1],
      StrEq("/usr/share/localization/wam/resources/en/html/loaderror.html"));
  EXPECT_THAT(
      paths[2],
      StrEq("/usr/share/localization/wam/resources/html/loaderror.html"));
  EXPECT_THAT(paths[3], StrEq(path));
}

TEST(UtilsTestSuite, nonEmptyErrorPagePathWithLangRegionScript) {
  constexpr char lang_tag[] = "zh-Hant-HK";
  const auto& paths = util::GetErrorPagePaths(path, lang_tag);
  ASSERT_EQ(paths.size(), 5);
  EXPECT_THAT(paths[0], StrEq("/usr/share/localization/wam/resources/zh/Hant/"
                              "HK/html/loaderror.html"));
  EXPECT_THAT(
      paths[1],
      StrEq("/usr/share/localization/wam/resources/zh/HK/html/loaderror.html"));
  EXPECT_THAT(
      paths[2],
      StrEq("/usr/share/localization/wam/resources/zh/html/loaderror.html"));
  EXPECT_THAT(
      paths[3],
      StrEq("/usr/share/localization/wam/resources/html/loaderror.html"));
  EXPECT_THAT(paths[4], StrEq(path));
}

TEST(UtilsTestSuite, replaceAll) {
  std::string source("\\");
  util::ReplaceSubstr(source, "\\", "\\\\");
  EXPECT_STREQ("\\\\", source.c_str());
  source = "'";
  util::ReplaceSubstr(source, "'", "\\'");
  EXPECT_STREQ("\\'", source.c_str());
  source = "\n";
  util::ReplaceSubstr(source, "\n", "\\n");
  EXPECT_STREQ("\\n", source.c_str());
  source = "\r";
  util::ReplaceSubstr(source, "\r", "\\r");
  EXPECT_STREQ("\\r", source.c_str());
}

TEST(UtilsTestSuite, strToInt_IncorrectString) {
  std::string str = "not a number";
  int value = 0;
  bool result = util::StrToInt(str, value);
  EXPECT_FALSE(result);
}

TEST(UtilsTestSuite, strToInt_CorrectPositive) {
  std::string str = "10";
  const int32_t expected = 10;
  int value = 0;
  bool result = util::StrToInt(str, value);
  EXPECT_TRUE(result);
  EXPECT_EQ(value, expected);
}

TEST(UtilsTestSuite, strToInt_CorrectNegative) {
  std::string str = "-10";
  const int32_t expected = -10;
  int value = 0;
  bool result = util::StrToInt(str, value);
  EXPECT_TRUE(result);
  EXPECT_EQ(value, expected);
}

TEST(UtilsTestSuite, strToInt_Overflow) {
  std::string str = "+2147483648";
  int value = 0;
  bool result = util::StrToInt(str, value);
  EXPECT_FALSE(result);
}

TEST(UtilsTestSuite, strToInt_MAX_VALUE) {
  std::string str = "+2147483647";
  const int32_t expected = INT_MAX;
  int value = 0;
  bool result = util::StrToInt(str, value);
  EXPECT_TRUE(result);
  EXPECT_EQ(value, expected);
}

TEST(UtilsTestSuite, strToInt_Underflow) {
  std::string str = "-2147483649";
  int value = 0;
  bool result = util::StrToInt(str, value);
  EXPECT_FALSE(result);
}

TEST(UtilsTestSuite, strToInt_MIN_VALUE) {
  std::string str = "-2147483648";
  const int32_t expected = INT_MIN;
  int value = 0;
  bool result = util::StrToInt(str, value);
  EXPECT_TRUE(result);
  EXPECT_EQ(value, expected);
}

TEST(UtilsTestSuite, strToInt_Mixed) {
  std::string str = "21 some words";
  const int32_t expected = 21;
  int value = 0;
  bool result = util::StrToInt(str, value);
  EXPECT_TRUE(result);
  EXPECT_EQ(value, expected);
}

TEST(UtilsTestSuite, strToInt_Mixed_Underscore) {
  std::string str = "21_some_words";
  const int32_t expected = 21;
  int value = 0;
  bool result = util::StrToInt(str, value);
  EXPECT_TRUE(result);
  EXPECT_EQ(value, expected);
}

TEST(UtilsTestSuite, strToInt_Mixed_Underscore_Suffix) {
  std::string str = "some_words_21";
  int value = 0;
  bool result = util::StrToInt(str, value);
  EXPECT_FALSE(result);
}

// UTF-8 <-> UTF-16 conversion

TEST(UtilsTestSuite, utf16ToUtf8_Ascii) {
  EXPECT_EQ(util::Utf16ToUtf8(u"hello"), "hello");
}

TEST(UtilsTestSuite, utf8ToUtf16_Ascii) {
  EXPECT_EQ(util::Utf8ToUtf16("hello"), u"hello");
}

TEST(UtilsTestSuite, utf8ToUtf16_MultiByte) {
  // U+00E9 (2 bytes), U+20AC (3 bytes)
  EXPECT_EQ(util::Utf8ToUtf16("\xC3\xA9\xE2\x82\xAC"), u"é€");
}

TEST(UtilsTestSuite, utf16ToUtf8_MultiByte) {
  EXPECT_EQ(util::Utf16ToUtf8(u"é€"), "\xC3\xA9\xE2\x82\xAC");
}

TEST(UtilsTestSuite, utf8ToUtf16_SurrogatePair) {
  // U+1F600 GRINNING FACE encodes as a surrogate pair in UTF-16
  const std::u16string converted = util::Utf8ToUtf16("\xF0\x9F\x98\x80");
  ASSERT_EQ(converted.size(), 2u);
  EXPECT_EQ(converted[0], 0xD83D);
  EXPECT_EQ(converted[1], 0xDE00);
}

TEST(UtilsTestSuite, utf16ToUtf8_SurrogatePair) {
  const std::u16string emoji = {0xD83D, 0xDE00};
  EXPECT_EQ(util::Utf16ToUtf8(emoji), "\xF0\x9F\x98\x80");
}

TEST(UtilsTestSuite, utf_RoundTrip) {
  const std::string original = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z";
  EXPECT_EQ(util::Utf16ToUtf8(util::Utf8ToUtf16(original)), original);
}

TEST(UtilsTestSuite, utf8ToUtf16_InvalidIsReplaced) {
  // Stray continuation byte, over-long encoding and a surrogate half must all
  // decode to U+FFFD instead of throwing.
  EXPECT_EQ(util::Utf8ToUtf16("\x80"), u"�");
  EXPECT_EQ(util::Utf8ToUtf16("\xC0\xAF"), u"��");
  EXPECT_EQ(util::Utf8ToUtf16("\xED\xA0\x80"), u"���");
}

TEST(UtilsTestSuite, utf8ToUtf16_TruncatedIsReplaced) {
  EXPECT_EQ(util::Utf8ToUtf16("\xF0\x9F"), u"��");
}

TEST(UtilsTestSuite, utf16ToUtf8_UnpairedSurrogateIsReplaced) {
  const std::u16string lone_high = {0xD83D};
  const std::u16string lone_low = {0xDE00};
  EXPECT_EQ(util::Utf16ToUtf8(lone_high), "\xEF\xBF\xBD");
  EXPECT_EQ(util::Utf16ToUtf8(lone_low), "\xEF\xBF\xBD");
}
