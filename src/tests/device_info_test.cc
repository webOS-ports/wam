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

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "device_info.h"
#include "device_info_impl.h"
#include "temp_dir.h"

namespace {

class DeviceInfoTest : public ::testing::Test {
 public:
  DeviceInfoTest() = default;
  ~DeviceInfoTest() override = default;

  // Used by the TEST_F bodies, which cppcheck does not see as members.
  // cppcheck-suppress unusedStructMember
  DeviceInfo device_info_;
};

}  // namespace

TEST_F(DeviceInfoTest, checkAsciStringDeviceInfo) {
  const char* expected_value = "ASCII String";
  const char* expected_value2 = "ASCII String2";
  std::string actual_value;

  device_info_.SetDeviceInfo("SimpleString", expected_value);
  ASSERT_TRUE(device_info_.GetDeviceInfo("SimpleString", actual_value));
  EXPECT_STREQ(expected_value, actual_value.c_str());

  device_info_.SetDeviceInfo("SimpleString", expected_value2);
  ASSERT_TRUE(device_info_.GetDeviceInfo("SimpleString", actual_value));
  EXPECT_STREQ(expected_value2, actual_value.c_str());
}

TEST_F(DeviceInfoTest, checkNotAsciStringDeviceInfo) {
  const char* expected_value = "String with not ASCII symbols (föö)";
  const char* expected_value2 = "String with not ASCII symbols (föö)2";
  std::string actual_value;

  device_info_.SetDeviceInfo("ComplexString", expected_value);
  ASSERT_TRUE(device_info_.GetDeviceInfo("ComplexString", actual_value));
  EXPECT_STREQ(expected_value, actual_value.c_str());

  device_info_.SetDeviceInfo("ComplexString", expected_value2);
  ASSERT_TRUE(device_info_.GetDeviceInfo("ComplexString", actual_value));
  EXPECT_STREQ(expected_value2, actual_value.c_str());
}

TEST_F(DeviceInfoTest, checkSystemLanguage) {
  const char* expected_value = "ENG";
  const char* expected_value2 = "KR";
  std::string actual_value;

  device_info_.SetSystemLanguage(expected_value);
  ASSERT_TRUE(device_info_.GetSystemLanguage(actual_value));
  EXPECT_STREQ(expected_value, actual_value.c_str());

  device_info_.SetSystemLanguage(expected_value2);
  ASSERT_TRUE(device_info_.GetSystemLanguage(actual_value));
  EXPECT_STREQ(expected_value2, actual_value.c_str());
}

TEST_F(DeviceInfoTest, checkDisplayWidth) {
  int const expected_value = 1900;
  int const expected_value2 = 2000;
  int actual_value = 0;

  device_info_.SetDisplayWidth(expected_value);
  ASSERT_TRUE(device_info_.GetDisplayWidth(actual_value));
  EXPECT_EQ(expected_value, actual_value);

  device_info_.SetDisplayWidth(expected_value2);
  ASSERT_TRUE(device_info_.GetDisplayWidth(actual_value));
  EXPECT_EQ(expected_value2, actual_value);
}

TEST_F(DeviceInfoTest, checkDisplayHeight) {
  int const expected_value = 1080;
  int const expected_value2 = 1090;
  int actual_value = 0;

  device_info_.SetDisplayHeight(expected_value);
  ASSERT_TRUE(device_info_.GetDisplayHeight(actual_value));
  EXPECT_EQ(expected_value, actual_value);

  device_info_.SetDisplayHeight(expected_value2);
  ASSERT_TRUE(device_info_.GetDisplayHeight(actual_value));
  EXPECT_EQ(expected_value2, actual_value);
}


namespace {

// Conf files and a ui-scale file of the test's own, read as the device's.
class DashboardHeightTest : public ::testing::Test {
 protected:
  std::vector<std::string> Paths(const std::vector<std::string>& names) const {
    std::vector<std::string> paths(names.size());
    std::transform(names.begin(), names.end(), paths.begin(),
                   [this](const std::string& name) {
                     return dir_.Path() + "/" + name;
                   });
    return paths;
  }

  int Read(const std::vector<std::string>& names,
           const std::string& ui_scale_name = "ui-scale") const {
    return DeviceInfoImpl::ReadDashboardHeight(Paths(names),
                                               dir_.Path() + "/" + ui_scale_name);
  }

  bool ReadTabletUi(const std::vector<std::string>& names) const {
    return DeviceInfoImpl::ReadTabletUi(Paths(names));
  }

  // cppcheck-suppress unusedStructMember
  TempDir dir_;
};

}  // namespace

TEST_F(DashboardHeightTest, isFiveAndAHalfGridUnitsRounded) {
  ASSERT_FALSE(dir_.Path().empty());
  ASSERT_TRUE(dir_.WriteFile("luna-platform.conf", "[UI]\nGridUnit=16\n"));

  // gu(5.6) on a q25: 89.6, which the shell rounds to 90.
  EXPECT_EQ(Read({"luna-platform.conf"}), 90);
}

TEST_F(DashboardHeightTest, theLastFileThatSetsAGridUnitWins) {
  ASSERT_FALSE(dir_.Path().empty());
  ASSERT_TRUE(dir_.WriteFile("luna.conf", "[UI]\nGridUnit=10\n"));
  ASSERT_TRUE(dir_.WriteFile("luna-platform.conf", "[UI]\nGridUnit=25\n"));
  ASSERT_TRUE(dir_.WriteFile("luna-hw.conf", "[General]\nShowReticle=true\n"));

  EXPECT_EQ(Read({"luna.conf", "luna-platform.conf", "luna-hw.conf",
                  "missing.conf"}),
            140);
}

TEST_F(DashboardHeightTest, noGridUnitAnywhereIsSettingsDefault) {
  ASSERT_FALSE(dir_.Path().empty());
  ASSERT_TRUE(dir_.WriteFile("luna.conf", "[UI]\nTabletUi=false\n"));

  // Settings starts from 8: 44.8, rounded.
  EXPECT_EQ(Read({"luna.conf", "missing.conf"}), 45);
}

TEST_F(DashboardHeightTest, aGridUnitThatIsNoSizeIsNotTaken) {
  ASSERT_FALSE(dir_.Path().empty());
  ASSERT_TRUE(dir_.WriteFile("a.conf", "[UI]\nGridUnit=17\n"));
  ASSERT_TRUE(dir_.WriteFile("b.conf", "[UI]\nGridUnit=0\n"));
  ASSERT_TRUE(dir_.WriteFile("c.conf", "[UI]\nGridUnit=big\n"));
  ASSERT_TRUE(dir_.WriteFile("d.conf", "[UI]\nGridUnit=nan\n"));

  // gu(5.6) on an sm-t520: 95.2.
  EXPECT_EQ(Read({"a.conf", "b.conf", "c.conf", "d.conf"}), 95);
}

TEST_F(DashboardHeightTest, theUserScaleMultipliesTheGridUnit) {
  ASSERT_FALSE(dir_.Path().empty());
  ASSERT_TRUE(dir_.WriteFile("luna.conf", "[UI]\nGridUnit=16\n"));
  ASSERT_TRUE(dir_.WriteFile("ui-scale", "1.25\n"));

  // 16 * 1.25 = 20, times 5.6.
  EXPECT_EQ(Read({"luna.conf"}), 112);
}

TEST_F(DashboardHeightTest, theUserScaleIsClampedAsTheShellClampsIt) {
  ASSERT_FALSE(dir_.Path().empty());
  ASSERT_TRUE(dir_.WriteFile("luna.conf", "[UI]\nGridUnit=10\n"));
  ASSERT_TRUE(dir_.WriteFile("big", "4\n"));
  ASSERT_TRUE(dir_.WriteFile("small", "0.1\n"));

  EXPECT_EQ(Read({"luna.conf"}, "big"), 84);    // 10 * 1.5 * 5.6
  EXPECT_EQ(Read({"luna.conf"}, "small"), 42);  // 10 * 0.75 * 5.6
}

TEST_F(DashboardHeightTest, aUserScaleThatIsNoNumberIsOne) {
  ASSERT_FALSE(dir_.Path().empty());
  ASSERT_TRUE(dir_.WriteFile("luna.conf", "[UI]\nGridUnit=16\n"));
  ASSERT_TRUE(dir_.WriteFile("nan", "nan\n"));
  ASSERT_TRUE(dir_.WriteFile("partial", "1.25x\n"));

  EXPECT_EQ(Read({"luna.conf"}, "nan"), 90);
  EXPECT_EQ(Read({"luna.conf"}, "partial"), 90);
  EXPECT_EQ(Read({"luna.conf"}, "missing"), 90);
}

TEST_F(DashboardHeightTest, theLastFileThatSetsTabletUiWins) {
  ASSERT_FALSE(dir_.Path().empty());
  // The q25's: on in luna.conf, off again in luna-platform.conf.
  ASSERT_TRUE(dir_.WriteFile("luna.conf", "[UI]\nTabletUi=true\n"));
  ASSERT_TRUE(dir_.WriteFile("luna-platform.conf", "[UI]\nTabletUi=false\n"));
  ASSERT_TRUE(dir_.WriteFile("luna-hw.conf", "[UI]\nGridUnit=16\n"));

  EXPECT_FALSE(ReadTabletUi({"luna.conf", "luna-platform.conf", "luna-hw.conf",
                             "missing.conf"}));
  EXPECT_TRUE(ReadTabletUi({"luna-platform.conf", "luna.conf"}));
}

TEST_F(DashboardHeightTest, noTabletUiAnywhereIsAPhone) {
  ASSERT_FALSE(dir_.Path().empty());
  ASSERT_TRUE(dir_.WriteFile("luna.conf", "[UI]\nGridUnit=16\n"));

  EXPECT_FALSE(ReadTabletUi({"luna.conf", "missing.conf"}));
  EXPECT_FALSE(ReadTabletUi({}));
}

TEST_F(DashboardHeightTest, aTabletUiThatIsNoBooleanIsNotTaken) {
  ASSERT_FALSE(dir_.Path().empty());
  ASSERT_TRUE(dir_.WriteFile("a.conf", "[UI]\nTabletUi=true\n"));
  ASSERT_TRUE(dir_.WriteFile("b.conf", "[UI]\nTabletUi=maybe\n"));
  ASSERT_TRUE(dir_.WriteFile("c.conf", "[General]\nTabletUi=false\n"));

  EXPECT_TRUE(ReadTabletUi({"a.conf", "b.conf", "c.conf"}));
}
