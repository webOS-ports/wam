// Copyright (c) 2008-2018 LG Electronics, Inc.
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

#include "network_status_manager.h"

#include "log_manager.h"

void NetworkStatusManager::UpdateNetworkStatus(const NetworkStatus& status) {
  if (current_.Type() != status.Type()) {
    AppendLogList(status.Type(), current_.Type(), status.Type());
  }

  CheckInformationChange(status.GetInformation());
  if (log_list_.size() > 0) {  // one more information was changed
    AppendLogList("date", current_.SavedDate(), status.SavedDate());
    PrintLog();
    current_ = status;
  }
}

void NetworkStatusManager::CheckInformationChange(
    const NetworkStatus::Information& information) {
  if (current_.GetInformation().IpAddress() != information.IpAddress()) {
    AppendLogList("ipAddress", current_.GetInformation().IpAddress(),
                  information.IpAddress());
  }
  if (current_.GetInformation().Dns1() != information.Dns1()) {
    AppendLogList("dns1", current_.GetInformation().Dns1(), information.Dns1());
  }
  if (current_.GetInformation().Dns2() != information.Dns2()) {
    AppendLogList("dns2", current_.GetInformation().Dns2(), information.Dns2());
  }
  if (current_.GetInformation().Method() != information.Method()) {
    AppendLogList("method", current_.GetInformation().Method(),
                  information.Method());
  }
  if (current_.GetInformation().State() != information.State()) {
    AppendLogList("state", current_.GetInformation().State(),
                  information.State());
  }
  if (current_.GetInformation().Gateway() != information.Gateway()) {
    AppendLogList("gateway", current_.GetInformation().Gateway(),
                  information.Gateway());
  }
  if (current_.GetInformation().InterfaceName() !=
      information.InterfaceName()) {
    AppendLogList("interfaceName", current_.GetInformation().InterfaceName(),
                  information.InterfaceName());
  }
  if (current_.GetInformation().OnInternet() != information.OnInternet()) {
    AppendLogList("onInternet", current_.GetInformation().OnInternet(),
                  information.OnInternet());
  }
}

void NetworkStatusManager::AppendLogList(const std::string& key,
                                         const std::string& previous,
                                         const std::string& current) {
  log_list_.emplace(std::piecewise_construct, std::forward_as_tuple(key),
                    std::forward_as_tuple(previous, current));
}

void NetworkStatusManager::PrintLog() {
  for (const auto& line : log_list_) {
    LOG_INFO(MSGID_NETWORKSTATUS_INFO, 3, PMLOGKS("CHANGE", line.first.c_str()),
             PMLOGKS("Previous", line.second.first.c_str()),
             PMLOGKS("Current", line.second.second.c_str()), "");
  }
  log_list_.clear();
}
