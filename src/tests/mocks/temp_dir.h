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

#ifndef TESTS_MOCKS_TEMP_DIR_H_
#define TESTS_MOCKS_TEMP_DIR_H_

#include <stdlib.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

// A directory of the test's own, removed with everything in it when the test
// is done: what a test needs on disk goes here instead of the device's
// /usr/palm or /usr/lib, so it runs the same on the build host.
class TempDir {
 public:
  TempDir() {
    std::error_code error;
    std::string pattern =
        (std::filesystem::temp_directory_path(error) / "wam-test-XXXXXX")
            .string();
    if (!error && mkdtemp(pattern.data())) {
      path_ = pattern;
    }
  }
  ~TempDir() {
    if (!path_.empty()) {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  // Empty when the directory could not be made.
  const std::string& Path() const { return path_; }

  // Creates |name| (relative to the directory) holding |content|.
  bool WriteFile(const std::string& name, const std::string& content) const {
    std::ofstream file(path_ + "/" + name, std::ios::binary);
    file << content;
    return static_cast<bool>(file);
  }

  bool MakeDir(const std::string& name) const {
    std::error_code error;
    return std::filesystem::create_directory(path_ + "/" + name, error);
  }

 private:
  std::string path_;
};

#endif  // TESTS_MOCKS_TEMP_DIR_H_
