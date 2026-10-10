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

// The display rotation read from the compositor geometry configd value in the
// reply to com.webos.service.config/getConfigs.

#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "utils.h"
#include "web_app_manager.h"
#include "web_app_manager_service_luna.h"

namespace {

Json::Value GeometryReply(const std::string& geometry) {
  Json::Value reply;
  reply["returnValue"] = true;
  reply["configs"]["com.webos.surfacemanager.compositorGeometry"] = geometry;
  return reply;
}

void Deliver(const Json::Value& reply) {
  WebAppManagerServiceLuna::Instance()->GetCompositorGeometryCallback(reply);
}

int Rotation() {
  return WebAppManager::Instance()->DisplayRotation();
}

class DisplayRotationTest : public ::testing::Test {
 protected:
  // The manager is a singleton: start each test from a known quarter turn,
  // so that a value wrongly taken as 0 shows.
  void SetUp() override {
    Deliver(GeometryReply("800x600+0+0r270s1"));
    ASSERT_EQ(Rotation(), 270);
  }
  void TearDown() override { Deliver(GeometryReply("800x600+0+0r0s1")); }
};

}  // namespace

TEST_F(DisplayRotationTest, ValidRotations) {
  Deliver(GeometryReply("800x600+0+0r90s1"));
  EXPECT_EQ(Rotation(), 90);
  Deliver(GeometryReply("800x600+0+0r180s1"));
  EXPECT_EQ(Rotation(), 180);
  Deliver(GeometryReply("800x600+0+0r0s1"));
  EXPECT_EQ(Rotation(), 0);
  Deliver(GeometryReply("800x600+0+0r-90s1"));
  EXPECT_EQ(Rotation(), 270);
  Deliver(GeometryReply("800x600+0+0r450"));
  EXPECT_EQ(Rotation(), 90);
}

TEST_F(DisplayRotationTest, NoRotationField) {
  Deliver(GeometryReply("800x600+0+0"));
  EXPECT_EQ(Rotation(), 270);
  Deliver(GeometryReply(""));
  EXPECT_EQ(Rotation(), 270);
}

TEST_F(DisplayRotationTest, RotationThatIsNotANumber) {
  // atoi() read these as 0 and turned the display back.
  Deliver(GeometryReply("800x600+0+0r"));
  EXPECT_EQ(Rotation(), 270);
  Deliver(GeometryReply("800x600+0+0rs1"));
  EXPECT_EQ(Rotation(), 270);
  Deliver(GeometryReply("800x600+0+0rxs1"));
  EXPECT_EQ(Rotation(), 270);
}

TEST_F(DisplayRotationTest, RotationOutOfRange) {
  // Undefined behaviour with atoi(); with glibc on 64-bit targets the value
  // was truncated, so 2^32 + 90 came out as a quarter turn.
  Deliver(GeometryReply("800x600+0+0r4294967386s1"));
  EXPECT_EQ(Rotation(), 270);
  Deliver(GeometryReply("800x600+0+0r-4294967386s1"));
  EXPECT_EQ(Rotation(), 270);
  Deliver(GeometryReply("800x600+0+0r99999999999999999999999s1"));
  EXPECT_EQ(Rotation(), 270);
}

TEST_F(DisplayRotationTest, ReplyThatIsNotAnObject) {
  Deliver(util::StringToJson("[]"));
  EXPECT_EQ(Rotation(), 270);
  Deliver(Json::Value());
  EXPECT_EQ(Rotation(), 270);
  Deliver(util::StringToJson(R"({"returnValue":false,"configs":[]})"));
  EXPECT_EQ(Rotation(), 270);
  Deliver(util::StringToJson(R"({"returnValue":true,"configs":"x"})"));
  EXPECT_EQ(Rotation(), 270);
  Deliver(util::StringToJson(
      R"({"configs":{"com.webos.surfacemanager.compositorGeometry":5}})"));
  EXPECT_EQ(Rotation(), 270);
}
