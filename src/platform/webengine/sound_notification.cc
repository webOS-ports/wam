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

#include "sound_notification.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <vector>

#include <json/json.h>

#include "utils.h"

namespace fs = std::filesystem;

namespace sound_notification {

namespace {

constexpr size_t kMaxPayload = 16 * 1024;
constexpr size_t kMaxPath = 4096;
constexpr size_t kMaxClass = 64;
// Longest a sound may be asked to play for. A number outside int is not an int
// to jsoncpp, which throws, and WebAppMgr catches nothing.
constexpr double kMaxDurationMs = 10 * 60 * 1000;
// How far RestoreCase() looks: a directory is read for each part it has to
// correct, on the thread that runs every app's page.
constexpr int kMaxEntries = 4096;
constexpr char kFileScheme[] = "file://";

bool HasControlCharacter(const std::string& text) {
  return std::any_of(text.begin(), text.end(), [](unsigned char c) {
    return c < 0x20 || c == 0x7f;
  });
}

std::vector<std::string> PathParts(const std::string& path) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string::npos) {
      end = path.size();
    }
    if (end > start) {
      parts.push_back(path.substr(start, end - start));
    }
    start = end + 1;
  }
  return parts;
}

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return text;
}

bool IsRegularFile(const std::string& path) {
  std::error_code ec;
  return fs::is_regular_file(path, ec);
}

// The entry of |folder| named |lower| but for case. The first by name, should
// more than one differ only by case.
std::string FindIgnoringCase(const fs::path& folder, const std::string& lower) {
  std::string found;
  std::error_code ec;
  fs::directory_iterator entry(
      folder, fs::directory_options::skip_permission_denied, ec);
  const fs::directory_iterator end;
  for (int seen = 0; !ec && entry != end && seen < kMaxEntries; ++seen) {
    const std::string name = entry->path().filename().string();
    if (Lower(name) == lower && (found.empty() || name < found)) {
      found = name;
    }
    entry.increment(ec);
  }
  return found;
}

}  // namespace

const char* PageShim() {
  return R"JS((function() {
  var ps = window.PalmSystem;
  if (!ps || typeof ps.setWindowProperty !== "function")
    return;
  var text = function(value) {
    return typeof value === "string" ? value : "";
  };
  ps.playSoundNotification = function(soundClass, path, duration, wakeup) {
    ps.setWindowProperty("_LuneOS_playSoundNotification", JSON.stringify({
      soundClass: text(soundClass).slice(0, 64),
      soundFile: text(path).slice(0, 4096),
      duration: typeof duration === "number" ? duration : -1,
      wakeup: wakeup === true
    }));
  };
  ps.setAlertSound = function(soundClass, path) {
    ps.setWindowProperty("LuneOS_soundclass", text(soundClass).slice(0, 64));
    ps.setWindowProperty("LuneOS_sound", text(path).slice(0, 4096));
  };
})();)JS";
}

std::optional<Request> ParseRequest(const std::string& payload,
                                    const std::string& app_folder) {
  if (payload.size() > kMaxPayload) {
    return std::nullopt;
  }
  Json::Value value;
  if (!util::StringToJson(payload, value) || !value.isObject()) {
    return std::nullopt;
  }

  Request request;
  if (value["soundClass"].isString()) {
    request.sound_class = value["soundClass"].asString();
  }
  if (request.sound_class.size() > kMaxClass ||
      HasControlCharacter(request.sound_class)) {
    return std::nullopt;
  }
  if (value["soundFile"].isString()) {
    const std::string file = value["soundFile"].asString();
    request.sound_file = ResolvePath(file, app_folder);
    if (!file.empty() && request.sound_file.empty()) {
      return std::nullopt;
    }
  }
  if (request.sound_class.empty() && request.sound_file.empty()) {
    return std::nullopt;
  }
  if (value["duration"].isNumeric()) {
    const double duration = value["duration"].asDouble();
    if (std::isfinite(duration) && duration > 0) {
      request.duration_ms = static_cast<int>(std::min(duration, kMaxDurationMs));
    }
  }
  request.wakeup = value["wakeup"].isBool() && value["wakeup"].asBool();
  return request;
}

std::string ToServicePayload(const Request& request,
                             const std::string& app_id) {
  Json::Value params(Json::objectValue);
  params["appId"] = app_id;
  params["soundClass"] = request.sound_class;
  params["soundFile"] = request.sound_file;
  params["duration"] = request.duration_ms;
  params["wakeup"] = request.wakeup;
  return util::JsonToString(params);
}

std::string ResolvePath(const std::string& path,
                        const std::string& app_folder,
                        const std::string& system_folder) {
  if (path.empty() || path.size() > kMaxPath || HasControlCharacter(path)) {
    return std::string();
  }
  std::string entry = path;
  if (entry.starts_with(kFileScheme)) {
    entry.erase(0, std::char_traits<char>::length(kFileScheme));
  } else if (entry.find("://") != std::string::npos) {
    return std::string();
  }
  const std::vector<std::string> parts = PathParts(entry);
  if (parts.empty() || std::ranges::find(parts, "..") != parts.end()) {
    return std::string();
  }
  if (entry.front() == '/') {
    return RestoreCase(entry);
  }
  if (!app_folder.empty()) {
    const std::string in_app = RestoreCase(app_folder + "/" + entry);
    if (IsRegularFile(in_app)) {
      return in_app;
    }
  }
  const std::string in_system = RestoreCase(system_folder + "/" + entry);
  return IsRegularFile(in_system) ? in_system : std::string();
}

std::string RestoreCase(const std::string& path) {
  if (path.empty() || path.front() != '/' || IsRegularFile(path)) {
    return path;
  }
  fs::path restored("/");
  for (const std::string& part : PathParts(path)) {
    std::error_code ec;
    if (fs::exists(restored / part, ec)) {
      restored /= part;
      continue;
    }
    const std::string name = FindIgnoringCase(restored, Lower(part));
    if (name.empty()) {
      return path;
    }
    restored /= name;
  }
  return restored.string();
}

}  // namespace sound_notification
