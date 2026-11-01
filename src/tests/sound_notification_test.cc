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

// What a page sends for PalmSystem.playSoundNotification and setAlertSound.
// Any page can call setWindowProperty with anything, and jsoncpp throws on a
// value of the wrong type, which WebAppMgr does not catch: a bad request has to
// come to nothing rather than end every running app, the alarm clock included.

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "mocks/temp_dir.h"
#include "sound_notification.h"
#include "utils.h"

using ::testing::HasSubstr;
using sound_notification::ParseRequest;
using sound_notification::ResolvePath;
using sound_notification::RestoreCase;
using sound_notification::ToServicePayload;

TEST(SoundNotificationTest, EmailsCall) {
  const auto request = ParseRequest(
      R"({"soundClass":"alerts","soundFile":"/usr/palm/x/e.mp3",)"
      R"("duration":3000,"wakeup":false})",
      "/usr/palm/applications/com.palm.app.email");
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->sound_class, "alerts");
  EXPECT_EQ(request->sound_file, "/usr/palm/x/e.mp3");
  EXPECT_EQ(request->duration_ms, 3000);
  EXPECT_FALSE(request->wakeup);
}

TEST(SoundNotificationTest, ClassAlone) {
  const auto request = ParseRequest(R"({"soundClass":"vibrate"})", "/app");
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->sound_class, "vibrate");
  EXPECT_EQ(request->sound_file, "");
  EXPECT_EQ(request->duration_ms, -1);
}

TEST(SoundNotificationTest, Wakeup) {
  EXPECT_TRUE(ParseRequest(R"({"soundClass":"a","wakeup":true})", "")->wakeup);
  EXPECT_FALSE(ParseRequest(R"({"soundClass":"a","wakeup":"true"})", "")->wakeup);
  EXPECT_FALSE(ParseRequest(R"({"soundClass":"a","wakeup":1})", "")->wakeup);
}

TEST(SoundNotificationTest, NothingToPlayIsNoRequest) {
  EXPECT_FALSE(ParseRequest(R"({})", "/app").has_value());
  EXPECT_FALSE(
      ParseRequest(R"({"soundClass":"","soundFile":""})", "/app").has_value());
}

TEST(SoundNotificationTest, WhatIsNotAnObjectIsNoRequest) {
  for (const char* payload :
       {"", "[]", "[1,2]", "null", "true", "1", "\"alerts\"", "{", "{]", "}{"}) {
    EXPECT_FALSE(ParseRequest(payload, "/app").has_value()) << payload;
  }
}

TEST(SoundNotificationTest, MembersOfTheWrongType) {
  for (const char* payload :
       {R"({"soundClass":1})", R"({"soundClass":[]})", R"({"soundClass":{}})",
        R"({"soundClass":null})", R"({"soundFile":7})", R"({"soundFile":[]})",
        R"({"soundClass":true,"soundFile":{"a":1}})"}) {
    EXPECT_FALSE(ParseRequest(payload, "/app").has_value()) << payload;
  }
  const auto request = ParseRequest(
      R"({"soundClass":"alerts","soundFile":[],"duration":{},"wakeup":[]})",
      "/app");
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->sound_file, "");
  EXPECT_EQ(request->duration_ms, -1);
  EXPECT_FALSE(request->wakeup);
}

// Numbers jsoncpp refuses to turn into an int, by throwing.
TEST(SoundNotificationTest, DurationsOutOfRange) {
  const auto duration = [](const std::string& number) {
    const auto request =
        ParseRequest(R"({"soundClass":"a","duration":)" + number + "}", "");
    EXPECT_TRUE(request.has_value()) << number;
    return request ? request->duration_ms : 0;
  };
  EXPECT_EQ(duration("0"), -1);
  EXPECT_EQ(duration("-1"), -1);
  EXPECT_EQ(duration("-5e300"), -1);
  EXPECT_EQ(duration("1e308"), 600000);
  EXPECT_EQ(duration("4294967296"), 600000);
  EXPECT_EQ(duration("18446744073709551615"), 600000);
  EXPECT_EQ(duration("1.5"), 1);
  EXPECT_EQ(duration("600001"), 600000);
}

TEST(SoundNotificationTest, HugeStrings) {
  const std::string huge(5000, 'a');
  EXPECT_FALSE(
      ParseRequest(R"({"soundClass":")" + huge + "\"}", "/app").has_value());
  EXPECT_FALSE(
      ParseRequest(R"({"soundFile":"/)" + huge + "\"}", "/app").has_value());
  EXPECT_FALSE(ParseRequest(R"({"soundClass":"a","x":")" +
                                std::string(100 * 1024, 'a') + "\"}",
                            "/app")
                   .has_value());
  EXPECT_FALSE(ParseRequest(std::string(100000, '['), "/app").has_value());
}

TEST(SoundNotificationTest, ControlCharacters) {
  EXPECT_FALSE(ParseRequest(R"({"soundClass":"a\u0007"})", "").has_value());
  EXPECT_FALSE(ParseRequest(R"({"soundFile":"/a\nb.mp3"})", "").has_value());
  EXPECT_EQ(ResolvePath(std::string("/a\0b.mp3", 8), ""), "");
}

TEST(SoundNotificationTest, Traversal) {
  for (const char* path : {"../x.mp3", "/usr/palm/sounds/../../../etc/shadow",
                           "a/../../b", "..", "/..", "file:///a/../b"}) {
    EXPECT_EQ(ResolvePath(path, "/app"), "") << path;
  }
  EXPECT_FALSE(
      ParseRequest(R"({"soundClass":"a","soundFile":"../x"})", "").has_value());
  // ".." only as a whole part of the path.
  EXPECT_EQ(ResolvePath("/a/..b/c...mp3", "/app"), "/a/..b/c...mp3");
}

TEST(SoundNotificationTest, OtherSchemes) {
  for (const char* path : {"http://example.com/x.mp3", "data://x",
                           "javascript://x", "smb://h/x"}) {
    EXPECT_EQ(ResolvePath(path, "/app"), "") << path;
  }
}

TEST(SoundNotificationTest, FileUrl) {
  EXPECT_EQ(ResolvePath("file:///usr/palm/sounds/x.mp3", "/app"),
            "/usr/palm/sounds/x.mp3");
  EXPECT_EQ(ResolvePath("file://", "/app"), "");
  EXPECT_EQ(ResolvePath("///", "/app"), "");
}

TEST(SoundNotificationTest, RelativeToTheAppThenTheSystem) {
  TempDir app;
  ASSERT_FALSE(app.Path().empty());
  ASSERT_TRUE(app.MakeDir("sounds"));
  ASSERT_TRUE(app.WriteFile("sounds/ping.mp3", "x"));
  EXPECT_EQ(ResolvePath("sounds/ping.mp3", app.Path()),
            app.Path() + "/sounds/ping.mp3");

  TempDir system;
  ASSERT_FALSE(system.Path().empty());
  ASSERT_TRUE(system.WriteFile("alert.wav", "x"));
  EXPECT_EQ(ResolvePath("alert.wav", app.Path(), system.Path()),
            system.Path() + "/alert.wav");
  EXPECT_EQ(ResolvePath("alert.wav", "", system.Path()),
            system.Path() + "/alert.wav");
}

TEST(SoundNotificationTest, AppFolderWinsOverTheSystem) {
  TempDir app;
  TempDir system;
  ASSERT_FALSE(app.Path().empty());
  ASSERT_FALSE(system.Path().empty());
  ASSERT_TRUE(app.WriteFile("alert.wav", "x"));
  ASSERT_TRUE(system.WriteFile("alert.wav", "x"));
  EXPECT_EQ(ResolvePath("alert.wav", app.Path(), system.Path()),
            app.Path() + "/alert.wav");
}

// Messaging's ConversationList.js sends this on every message, and its package
// has no such file. A tone for it would be a sound Messaging never had.
TEST(SoundNotificationTest, RelativeNameNowhereIsNoRequest) {
  TempDir app;
  TempDir system;
  ASSERT_FALSE(app.Path().empty());
  ASSERT_FALSE(system.Path().empty());
  EXPECT_EQ(ResolvePath("audios/sent.mp3", app.Path(), system.Path()), "");
  EXPECT_EQ(ResolvePath("audios/sent.mp3", "", system.Path()), "");
  EXPECT_FALSE(ParseRequest(R"({"soundClass":"alerts",)"
                            R"("soundFile":"audios/sent.mp3"})",
                            app.Path())
                   .has_value());
}

// A url stays one, even with an app folder that happens to spell it.
TEST(SoundNotificationTest, UrlIsNotARelativeName) {
  TempDir app;
  ASSERT_FALSE(app.Path().empty());
  ASSERT_TRUE(app.MakeDir("http:"));
  ASSERT_TRUE(app.WriteFile("http:/x.mp3", "x"));
  EXPECT_EQ(ResolvePath("http://x.mp3", app.Path()), "");
}

// A folder with that name is no sound.
TEST(SoundNotificationTest, RelativeFolderIsNotAFile) {
  TempDir app;
  TempDir system;
  ASSERT_FALSE(app.Path().empty());
  ASSERT_FALSE(system.Path().empty());
  ASSERT_TRUE(app.MakeDir("sounds"));
  ASSERT_TRUE(system.MakeDir("sounds"));
  EXPECT_EQ(ResolvePath("sounds", app.Path(), system.Path()), "");
}

// A ringtone deleted since it was picked still gets its class's tone.
TEST(SoundNotificationTest, AbsoluteMissingFileIsKept) {
  EXPECT_EQ(ResolvePath("/media/internal/ringtones/gone.mp3", "/app"),
            "/media/internal/ringtones/gone.mp3");
}

TEST(SoundNotificationTest, RestoresCase) {
  TempDir root;
  ASSERT_FALSE(root.Path().empty());
  ASSERT_TRUE(root.MakeDir("Ring Tones"));
  ASSERT_TRUE(root.WriteFile("Ring Tones/Flurry.MP3", "x"));
  const std::string real = root.Path() + "/Ring Tones/Flurry.MP3";

  EXPECT_EQ(RestoreCase(real), real);
  EXPECT_EQ(RestoreCase(root.Path() + "/ring tones/flurry.mp3"), real);
  // No such file: left as it was, and the shell picks a default.
  const std::string missing = root.Path() + "/ring tones/gone.mp3";
  EXPECT_EQ(RestoreCase(missing), missing);
  EXPECT_EQ(RestoreCase("relative/x.mp3"), "relative/x.mp3");
  EXPECT_EQ(RestoreCase(""), "");
  EXPECT_EQ(RestoreCase(real + "/not-a-directory"), real + "/not-a-directory");
}

TEST(SoundNotificationTest, RestoresCaseOfTheFirstByName) {
  TempDir root;
  ASSERT_FALSE(root.Path().empty());
  ASSERT_TRUE(root.WriteFile("tOne.mp3", "x"));
  ASSERT_TRUE(root.WriteFile("TONE.mp3", "x"));
  EXPECT_EQ(RestoreCase(root.Path() + "/tone.mp3"), root.Path() + "/TONE.mp3");
}

TEST(SoundNotificationTest, ServicePayload) {
  sound_notification::Request request;
  request.sound_class = "alerts";
  request.sound_file = "/a\"b.mp3";
  request.duration_ms = 3000;
  request.wakeup = true;
  Json::Value payload;
  ASSERT_TRUE(util::StringToJson(ToServicePayload(request, "com.x"), payload));
  EXPECT_EQ(payload["appId"].asString(), "com.x");
  EXPECT_EQ(payload["soundClass"].asString(), "alerts");
  EXPECT_EQ(payload["soundFile"].asString(), "/a\"b.mp3");
  EXPECT_EQ(payload["duration"].asInt(), 3000);
  EXPECT_TRUE(payload["wakeup"].asBool());
}

// The page side has no engine to run in here; it has to stay in step.
TEST(SoundNotificationTest, ShimUsesTheProperties) {
  const std::string shim = sound_notification::PageShim();
  EXPECT_THAT(shim, HasSubstr(std::string("\"") +
                              sound_notification::kPlayProperty + "\""));
  EXPECT_THAT(shim, HasSubstr(std::string("\"") +
                              sound_notification::kSoundProperty + "\""));
  EXPECT_THAT(shim, HasSubstr("ps.playSoundNotification = "));
  EXPECT_THAT(shim, HasSubstr("ps.setAlertSound = "));
}
