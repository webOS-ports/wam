// Copyright (c) 2015-2018 LG Electronics, Inc.
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

#include <grp.h>
#include <pwd.h>
#include <unistd.h>

#include <webos/app/webos_main.h>
#include <webos/public/runtime.h>

#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

#include "log_manager.h"
#include "platform/platform_factory.h"
#include "platform_module_factory_impl.h"
#include "utils.h"
#include "web_app_manager.h"
#include "web_app_manager_service_luna.h"

namespace {

// getpwnam_r()/getgrnam_r() with a buffer that grows until the entry fits.
// The reentrant forms: the plain ones return static storage that any thread
// may overwrite.
template <typename Entry, typename Lookup>
Entry* LookUp(const std::string& name,
              Entry* entry,
              std::vector<char>* buffer,
              Lookup lookup) {
  Entry* result = nullptr;
  for (buffer->resize(1024); buffer->size() <= (1u << 20);
       buffer->resize(buffer->size() * 2)) {
    const int error =
        lookup(name.c_str(), entry, buffer->data(), buffer->size(), &result);
    if (error != ERANGE) {
      return error == 0 ? result : nullptr;
    }
  }
  return nullptr;
}

// Drops privileges when WAM_UID/WAM_GID ask for it. Returns false if a drop was
// requested but did not fully succeed. These checks used to be assert()s, which
// release builds compile out (-DNDEBUG), so a failed drop went unnoticed and
// WAM carried on with the privileges it was started with.
bool ChangeUserIDGroupID() {
  std::string const uid = util::GetEnvVar("WAM_UID");
  std::string const gid = util::GetEnvVar("WAM_GID");

  if (uid.empty() || gid.empty()) {
    return true;
  }

  struct passwd pwd_entry = {};
  std::vector<char> pwd_buffer;
  struct passwd const* pwd = LookUp(uid, &pwd_entry, &pwd_buffer, getpwnam_r);
  if (!pwd) {
    LOG_ERROR(MSGID_WAM_DEBUG, 1, PMLOGKS("WAM_UID", uid.c_str()),
              "Unknown user; cannot drop privileges");
    return false;
  }

  struct group grp_entry = {};
  std::vector<char> grp_buffer;
  struct group const* grp = LookUp(gid, &grp_entry, &grp_buffer, getgrnam_r);
  if (!grp) {
    LOG_ERROR(MSGID_WAM_DEBUG, 1, PMLOGKS("WAM_GID", gid.c_str()),
              "Unknown group; cannot drop privileges");
    return false;
  }

  // Order matters: the group has to be set while still privileged, otherwise
  // setgid()/initgroups() would fail after setuid() has already run.
  if (setgid(grp->gr_gid) != 0) {
    LOG_ERROR(MSGID_WAM_DEBUG, 1, PMLOGKS("WAM_GID", gid.c_str()),
              "setgid failed");
    return false;
  }

  if (initgroups(uid.c_str(), grp->gr_gid) != 0) {
    LOG_ERROR(MSGID_WAM_DEBUG, 1, PMLOGKS("WAM_UID", uid.c_str()),
              "initgroups failed");
    return false;
  }

  if (setuid(pwd->pw_uid) != 0) {
    LOG_ERROR(MSGID_WAM_DEBUG, 1, PMLOGKS("WAM_UID", uid.c_str()),
              "setuid failed");
    return false;
  }

  // The engine finds the profile through HOME, which has to be the dropped
  // user's. There is no thread-safe way to set it.
  setenv("HOME", pwd->pw_dir, 1);  // NOLINT(concurrency-mt-unsafe)
  return true;
}

void StartWebAppManager() {
  if (!ChangeUserIDGroupID()) {
    LOG_CRITICAL(MSGID_WAM_DEBUG, 0,
                 "Refusing to continue with undropped privileges");
    _exit(EXIT_FAILURE);
  }

  WebAppManagerServiceLuna* luna_service = WebAppManagerServiceLuna::Instance();
  if (!luna_service || !luna_service->StartService()) {
    LOG_ERROR(MSGID_WAM_DEBUG, 0, "Failed to start the Luna service");
  }

  WebAppManager::Instance()->SetPlatformModules(
      std::make_unique<PlatformModuleFactoryImpl>());
}

class WebOSMainDelegateWAM : public webos::WebOSMainDelegate {
 public:
  void BasicStartupComplete() override {
    webos::Runtime::GetInstance()->SetPlatformFactory(
        std::make_unique<PlatformFactory>());
  }
  void AboutToCreateContentBrowserClient() override { StartWebAppManager(); }
};

}  // namespace

int main(int argc, const char** argv) {
  WebOSMainDelegateWAM delegate;
  webos::WebOSMain webos_main(&delegate);
  return webos_main.Run(argc, argv);
}
