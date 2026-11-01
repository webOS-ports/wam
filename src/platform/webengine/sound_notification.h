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

// The two ways LunaSysMgr let a web app play a sound:
// PalmSystem.playSoundNotification(soundClass, path, duration, wakeup) and the
// "sound" and "soundclass" attributes of a popup alert window, which
// PalmSystem.setAlertSound() also sets. WebAppMgr only hands them on, checked:
// the shell owns the sound, with its stream classes, the tones the user picked
// and the rule for which files an app may play.

#ifndef PLATFORM_WEBENGINE_SOUND_NOTIFICATION_H_
#define PLATFORM_WEBENGINE_SOUND_NOTIFICATION_H_

#include <optional>
#include <string>

namespace sound_notification {

// The window property the page's shim sends a request under. webOSSystem is
// built by the web engine, and setWindowProperty is the one call it hands on
// with arbitrary text, so this is a request and never becomes a property.
inline constexpr char kPlayProperty[] = "_LuneOS_playSoundNotification";

// The window property a popup alert's sound file is carried in.
inline constexpr char kSoundProperty[] = "LuneOS_sound";

// The service in the shell that plays them.
inline constexpr char kPlayUrl[] =
    "luna://org.webosports.notifications/playSoundNotification";

// Where a relative name not in the app is looked for.
inline constexpr char kSystemSounds[] = "/usr/palm/sounds";

struct Request {
  std::string sound_class;
  std::string sound_file;
  int duration_ms = -1;
  bool wakeup = false;
};

// Adds playSoundNotification and setAlertSound to PalmSystem in a page.
const char* PageShim();

// A request as a page sent it. Anything may be in |payload|, as any page can
// call setWindowProperty itself; nothing comes back for what is not a request.
std::optional<Request> ParseRequest(const std::string& payload,
                                    const std::string& app_folder);

// |request| as the shell's playSoundNotification takes it.
std::string ToServicePayload(const Request& request, const std::string& app_id);

// |path| as an app gave it, as an absolute path: a file:// url or a path
// relative to the app's folder or, as LunaSysMgr did, the system sounds. Empty
// when it cannot be one: too long, control characters, another scheme, "..".
// Empty too for a relative name found in neither folder: Messaging asks for its
// "audios/sent.mp3" on every message and ships no such file, so that has to stay
// silent rather than fall back to the alert tone. An absolute path to a missing
// file is left for the shell: a deleted ringtone still gets the class's tone.
std::string ResolvePath(const std::string& path,
                        const std::string& app_folder,
                        const std::string& system_folder = kSystemSounds);

// |path| spelled as it is on disk, for a path that came through window.open()
// features, which Blink lowercases as a whole: /media/internal/ringtones/
// Flurry.mp3 arrives all in lower case. Unchanged when there is no such file.
std::string RestoreCase(const std::string& path);

}  // namespace sound_notification

#endif  // PLATFORM_WEBENGINE_SOUND_NOTIFICATION_H_
