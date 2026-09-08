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

#include "utils.h"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <glib.h>
#include <json/json.h>
#include <sys/stat.h>

#include "log_manager.h"

#include "bcp47.h"

namespace util {

std::string GetString(const char* value) {
  return value ? std::string(value) : std::string();
}

std::vector<std::string> GetErrorPagePaths(
    const std::string& error_page_location,
    const std::string& language) {
  if (error_page_location.empty()) {
    return std::vector<std::string>();
  }

  namespace fs = std::filesystem;

  fs::path error_page_path(error_page_location);
  std::string filename = error_page_path.filename().string();
  std::string search_path = error_page_path.parent_path().string();
  auto bcp47_pieces = BCP47::FromString(language);

  // search order:
  // searchPath/resources/<language>/<script>/<region>/html/fileName
  // searchPath/resources/<language>/<region>/html/fileName
  // searchPath/resources/<language>/html/fileName
  // searchPath/resources/html/fileName
  // searchPath/fileName

  std::vector<std::string> result;
  result.reserve(5);
  if (bcp47_pieces) {
    if (bcp47_pieces->HasScript()) {
      std::stringstream ss;
      ss << search_path << "/resources/";
      ss << bcp47_pieces->Language() << "/";
      ss << bcp47_pieces->Script();

      if (bcp47_pieces->HasRegion()) {
        ss << "/" << bcp47_pieces->Region();
      }

      ss << "/html/" << filename;
      result.emplace_back(ss.str());
    }
    if (bcp47_pieces->HasRegion()) {
      std::stringstream ss;
      ss << search_path << "/resources/";
      ss << bcp47_pieces->Language() << "/";
      ss << bcp47_pieces->Region() << "/html/";
      ss << filename;
      result.emplace_back(ss.str());
    }
    std::stringstream ss;
    ss << search_path << "/resources/";
    ss << bcp47_pieces->Language() << "/html/";
    ss << filename;
    result.emplace_back(ss.str());
  }
  result.emplace_back(search_path + "/resources/html/" + filename);
  result.emplace_back(search_path + "/" + filename);

  return result;
}

std::string GetHostname(const std::string& url) {
  if (url.empty()) {
    return std::string();
  }

  // source https://datatracker.ietf.org/doc/html/rfc3986#appendix-B

  std::regex rfc3986_regex(
      R"(^(([^:\/?#]+):)?(\/\/([^\/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?)");
  std::regex authority_regex(R"(^(?:[\w\:]+[@])?([\w.]+)(?:[:])?(?:[0-9]+)?)");
  std::smatch matches;

  if (!std::regex_match(url, matches, rfc3986_regex)) {
    return std::string();
  }

  std::string authority = matches[4];
  if (!std::regex_match(authority, matches, authority_regex)) {
    return std::string();
  }

  return matches[1];
}

bool DoesPathExist(const std::string& path) {
  if (path.empty()) {
    return false;
  }

  struct stat st = {};
  if (stat(path.c_str(), &st)) {
    return false;
  }

  return st.st_mode & S_IFDIR || st.st_mode & S_IFREG;
}

std::string ReadFile(const std::string& path) {
  if (!DoesPathExist(path)) {
    return std::string();
  }

  std::ifstream file(path);
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

std::string UriToLocal(const std::string& uri) {
  g_autofree gchar* cpath = g_filename_from_uri(uri.c_str(), nullptr, nullptr);
  return GetString(cpath);
}

std::string LocalToUri(const std::string& uri) {
  g_autofree gchar* cpath = g_filename_to_uri(uri.c_str(), nullptr, nullptr);
  return GetString(cpath);
}

std::string GetEnvVar(const char* env) {
  return GetString(getenv(env));
}

// STRING
bool StrToInt(const std::string& str, int& num) {
  const char* strptr = str.c_str();
  char* endptr = nullptr;
  errno = 0;
  long long value = strtoll(strptr, &endptr, 10);
  if (endptr == strptr) {
    return false;
  }

  if (value > std::numeric_limits<int>::max() ||
      value < std::numeric_limits<int>::min() || errno == ERANGE) {
    return false;
  }

  if (errno) {
    return false;
  }

  num = value;
  return true;
}

int StrToIntWithDefault(const std::string& str, int default_value) {
  int converted_value = 0;
  return StrToInt(str, converted_value) ? converted_value : default_value;
}

std::vector<std::string> SplitString(const std::string& str, char delimiter) {
  std::vector<std::string> res_list;
  std::stringstream ss(str);
  std::string s;

  while (std::getline(ss, s, delimiter)) {
    res_list.push_back(s);
  }

  return res_list;
}

std::string TrimString(const std::string& str) {
  auto begin = str.begin();
  auto end = str.end();

  while (begin != end && isspace(static_cast<unsigned char>(*begin))) {
    ++begin;
  }

  while (begin != end && isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }

  std::string trimmed(begin, end);
  return trimmed;
}

void ReplaceSubstr(std::string& in,
                   const std::string& to_search,
                   const std::string& replace_str /* ="" */) {
  size_t pos = in.find(to_search);
  while (pos != std::string::npos) {
    in.replace(pos, to_search.size(), replace_str);
    pos = in.find(to_search, pos + replace_str.size());
  }
}

namespace {

constexpr char32_t kReplacementChar = 0xFFFD;
constexpr char32_t kMaxCodePoint = 0x10FFFF;

bool IsSurrogate(char32_t cp) {
  return cp >= 0xD800 && cp <= 0xDFFF;
}

void AppendUtf8(char32_t cp, std::string& out) {
  if (cp > kMaxCodePoint || IsSurrogate(cp)) {
    cp = kReplacementChar;
  }
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

void AppendUtf16(char32_t cp, std::u16string& out) {
  if (cp > kMaxCodePoint || IsSurrogate(cp)) {
    cp = kReplacementChar;
  }
  if (cp < 0x10000) {
    out.push_back(static_cast<char16_t>(cp));
  } else {
    cp -= 0x10000;
    out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
    out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
  }
}

}  // namespace

std::string Utf16ToUtf8(const std::u16string& utf16) {
  std::string out;
  out.reserve(utf16.size());
  for (size_t i = 0; i < utf16.size(); ++i) {
    const char32_t unit = utf16[i];
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      // High surrogate: needs a matching low surrogate to form a code point.
      if (i + 1 < utf16.size() && utf16[i + 1] >= 0xDC00 &&
          utf16[i + 1] <= 0xDFFF) {
        const char32_t low = utf16[++i];
        AppendUtf8(0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00), out);
      } else {
        AppendUtf8(kReplacementChar, out);
      }
    } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
      // Unpaired low surrogate.
      AppendUtf8(kReplacementChar, out);
    } else {
      AppendUtf8(unit, out);
    }
  }
  return out;
}

std::u16string Utf8ToUtf16(const std::string& utf8) {
  std::u16string out;
  out.reserve(utf8.size());
  size_t i = 0;
  while (i < utf8.size()) {
    const unsigned char lead = static_cast<unsigned char>(utf8[i]);
    size_t extra = 0;
    char32_t cp = 0;
    if (lead < 0x80) {
      cp = lead;
    } else if ((lead & 0xE0) == 0xC0) {
      cp = lead & 0x1F;
      extra = 1;
    } else if ((lead & 0xF0) == 0xE0) {
      cp = lead & 0x0F;
      extra = 2;
    } else if ((lead & 0xF8) == 0xF0) {
      cp = lead & 0x07;
      extra = 3;
    } else {
      // Stray continuation byte or invalid lead byte.
      AppendUtf16(kReplacementChar, out);
      ++i;
      continue;
    }

    if (i + extra >= utf8.size()) {
      AppendUtf16(kReplacementChar, out);
      ++i;
      continue;
    }

    bool valid = true;
    for (size_t k = 1; k <= extra; ++k) {
      const unsigned char cont = static_cast<unsigned char>(utf8[i + k]);
      if ((cont & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      cp = (cp << 6) | (cont & 0x3F);
    }

    // Reject truncated sequences, over-long encodings and surrogate halves.
    if (!valid || (extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
        (extra == 3 && cp < 0x10000) || cp > kMaxCodePoint || IsSurrogate(cp)) {
      AppendUtf16(kReplacementChar, out);
      ++i;
      continue;
    }

    AppendUtf16(cp, out);
    i += extra + 1;
  }
  return out;
}

// JSON
bool StringToJson(const std::string& str, Json::Value& value) {
  Json::CharReaderBuilder builder;
  Json::CharReaderBuilder::strictMode(&builder.settings_);
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());

  return reader->parse(str.c_str(), str.c_str() + str.size(), &value, nullptr);
}

Json::Value StringToJson(const std::string& str) {
  Json::Value result;
  return StringToJson(str, result) ? std::move(result)
                                   : Json::Value(Json::nullValue);
}

std::string JsonToString(const Json::Value& value) {
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "    ";
  builder["enableYAMLCompatibility"] = true;

  return Json::writeString(builder, value);
}

}  // namespace util
