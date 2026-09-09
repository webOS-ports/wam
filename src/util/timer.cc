// Copyright (c) 2016-2018 LG Electronics, Inc.
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

#include "timer.h"

#include <glib.h>

int Timer::OnTimeout(void* data) {
  Timer* timer = static_cast<Timer*>(data);
  bool const is_repeating = timer->IsRepeating();
  if (!is_repeating) {
    // GLib destroys the source as soon as we return false, so drop the id now.
    // Otherwise a later Stop() would pass a dead - and possibly already
    // recycled - id to g_source_remove().
    timer->source_id_ = 0;
  }
  timer->HandleCallback();
  return is_repeating;
}

int Timer::OnTimeoutAndDestroy(void* data) {
  Timer* timer = static_cast<Timer*>(data);
  timer->source_id_ = 0;
  timer->HandleCallback();
  delete timer;
  return 0;
}

void Timer::Start(int delay_in_milli_seconds, bool will_destroy) {
  // Cancel a source that is still pending. Without this the previous source
  // keeps the only reference to it out of reach of Stop(), so it stays armed
  // and fires into a receiver that may already be gone.
  Stop();
  is_running_ = true;
  source_id_ =
      g_timeout_add(delay_in_milli_seconds,
                    will_destroy ? OnTimeoutAndDestroy : OnTimeout, this);
}

void Timer::Stop() {
  is_running_ = false;
  if (source_id_) {
    g_source_remove(source_id_);
    source_id_ = 0;
  }
}

ElapsedTimer::ElapsedTimer() : timer_(g_timer_new()) {}

ElapsedTimer::~ElapsedTimer() {
  g_timer_destroy(timer_);
}

bool ElapsedTimer::IsRunning() const {
  return is_running_;
}

void ElapsedTimer::Start() {
  g_timer_start(timer_);
  is_running_ = true;
}

void ElapsedTimer::Stop() {
  g_timer_stop(timer_);
  is_running_ = false;
}

int ElapsedTimer::ElapsedMs() const {
  return static_cast<int>(g_timer_elapsed(timer_, nullptr) * 1000);
}

int ElapsedTimer::ElapsedUs() const {
  return static_cast<int>(g_timer_elapsed(timer_, nullptr) * 1000000);
}
