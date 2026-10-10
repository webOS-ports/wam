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

#ifndef UTIL_UTILS_H_
#define UTIL_UTILS_H_

#include <string>
#include <vector>

#include <json/json.h>

namespace util {

std::string GetString(const char* value);

std::vector<std::string> GetErrorPagePaths(
    const std::string& error_page_location,
    const std::string& language = {});

bool DoesPathExist(const std::string& path);
std::string ReadFile(const std::string& path);

std::string UriToLocal(const std::string& uri);

std::string GetEnvVar(const char* env);

// STRING
bool StrToInt(const std::string& str, int32_t& num);
int StrToIntWithDefault(const std::string& str, int default_value);
std::vector<std::string> SplitString(const std::string& str, char delimiter);
std::string TrimString(const std::string& str);
void ReplaceSubstr(std::string& in,
                   const std::string& to_search,
                   const std::string& replace_str = {});

// UTF-8 <-> UTF-16 conversion. Replaces std::wstring_convert /
// std::codecvt_utf8_utf16, which are deprecated since C++17 and removed in
// C++26. Malformed input is replaced with U+FFFD rather than throwing.
std::string Utf16ToUtf8(const std::u16string& utf16);
std::u16string Utf8ToUtf16(const std::string& utf8);

// JSON
//
// jsoncpp throws when it is handed something it cannot do: a value of the
// wrong type for asBool()/asInt()/asDouble()/asString(), a member looked up
// on something that is not an object, or a document nested deeper than its
// parser's stack limit. WebAppMgr is built without exceptions and catches
// none, so each of those ended the process, and every running app with it.
// What comes from a page, an app's appinfo.json or launch parameters, or
// another service has to go through these instead.

// Parses |str| as strict JSON. False for anything else, including a
// document nested too deep to parse safely.
bool StringToJson(const std::string& str, Json::Value& value);
Json::Value StringToJson(const std::string& str);
std::string JsonToString(const Json::Value& value);

// |value| converted as jsoncpp's asBool()/asInt()/asDouble()/asString()
// convert it, or |fallback| when it cannot be converted (and would throw).
bool JsonAsBool(const Json::Value& value, bool fallback = false);
int JsonAsInt(const Json::Value& value, int fallback = 0);
double JsonAsDouble(const Json::Value& value, double fallback = 0.0);
std::string JsonAsString(const Json::Value& value,
                         const std::string& fallback = {});

// The member |key| of |object|; null when there is none or |object| is not
// an object.
const Json::Value& JsonMember(const Json::Value& object, const char* key);

}  // namespace util

#endif  // UTIL_UTILS_H_
