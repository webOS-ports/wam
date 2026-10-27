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

// One stage transition at a time, whichever way it is asked for. WebAppWayland
// itself cannot be built in a test (its Init() reaches webos::WebOSPlatform,
// which needs a base::AtExitManager), so this tests what it delegates to.

#include <algorithm>
#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include "stage_transitions.h"

namespace {

class StageTransitionsTest : public ::testing::Test {
 protected:
  // Runs |body| as a transition, keeping count of how deep transitions nest.
  void Transition(webos::NativeWindowState state,
                  const std::function<void()>& body) {
    transitions_.Run(state, [this, &body]() { Track(body); });
  }

  void Track(const std::function<void()>& body) {
    ++depth_;
    max_depth_ = std::max(max_depth_, depth_);
    body();
    --depth_;
  }

  std::vector<webos::NativeWindowState> applied_;
  std::function<void(webos::NativeWindowState)> on_apply_;
  bool closing_ = false;
  int depth_ = 0;
  int max_depth_ = 0;

  StageTransitions transitions_{
      [this](webos::NativeWindowState state) {
        Track([this, state]() {
          applied_.push_back(state);
          if (on_apply_) {
            on_apply_(state);
          }
        });
      },
      [this]() { return closing_; }};
};

}  // namespace

TEST_F(StageTransitionsTest, RunsATransitionAtOnce) {
  bool ran = false;
  bool in_transition = false;
  EXPECT_TRUE(transitions_.Run(webos::NATIVE_WINDOW_DEFAULT, [&]() {
    ran = true;
    in_transition = transitions_.InTransition();
  }));
  EXPECT_TRUE(ran);
  EXPECT_TRUE(in_transition);
  EXPECT_FALSE(transitions_.InTransition());
  EXPECT_TRUE(applied_.empty());
}

// The relaunch of a hidden keepAlive app: the activation rebuilds the window,
// and the new window reports its state before the activation has returned.
TEST_F(StageTransitionsTest, TransitionAskedForDuringOneIsDeferred) {
  bool nested_ran = false;
  bool nested_started = true;
  Transition(webos::NATIVE_WINDOW_DEFAULT, [&]() {
    nested_started = transitions_.Run(webos::NATIVE_WINDOW_MINIMIZED,
                                      [&]() { nested_ran = true; });
  });

  EXPECT_FALSE(nested_started);
  EXPECT_FALSE(nested_ran);
  EXPECT_EQ(max_depth_, 1);
  // Not lost: applied as the state it stands for, after the first.
  EXPECT_EQ(applied_,
            std::vector<webos::NativeWindowState>{webos::NATIVE_WINDOW_MINIMIZED});
  EXPECT_FALSE(transitions_.InTransition());
}

TEST_F(StageTransitionsTest, OnlyTheNewestDeferredStateIsApplied) {
  Transition(webos::NATIVE_WINDOW_DEFAULT, [&]() {
    transitions_.Run(webos::NATIVE_WINDOW_FULLSCREEN, []() {});
    transitions_.Run(webos::NATIVE_WINDOW_MINIMIZED, []() {});
  });

  EXPECT_EQ(applied_,
            std::vector<webos::NativeWindowState>{webos::NATIVE_WINDOW_MINIMIZED});
}

// A deferred state whose own transition asks for another: applied one after
// the other, never nested.
TEST_F(StageTransitionsTest, DeferredStatesAreAppliedInALoop) {
  on_apply_ = [this](webos::NativeWindowState state) {
    if (state == webos::NATIVE_WINDOW_MINIMIZED) {
      transitions_.Run(webos::NATIVE_WINDOW_DEFAULT, []() {});
    }
  };
  Transition(webos::NATIVE_WINDOW_DEFAULT, [&]() {
    transitions_.Run(webos::NATIVE_WINDOW_MINIMIZED, []() {});
  });

  EXPECT_EQ(max_depth_, 1);
  EXPECT_EQ(applied_,
            (std::vector<webos::NativeWindowState>{
                webos::NATIVE_WINDOW_MINIMIZED, webos::NATIVE_WINDOW_DEFAULT}));
}

TEST_F(StageTransitionsTest, NothingIsAppliedOnceTheAppIsClosing) {
  Transition(webos::NATIVE_WINDOW_DEFAULT, [&]() {
    transitions_.Run(webos::NATIVE_WINDOW_MINIMIZED, []() {});
    closing_ = true;
  });
  EXPECT_TRUE(applied_.empty());

  // Nor later: the dropped state does not come back with the next transition.
  closing_ = false;
  Transition(webos::NATIVE_WINDOW_DEFAULT, []() {});
  EXPECT_TRUE(applied_.empty());
}

TEST_F(StageTransitionsTest, NextTransitionRunsAtOnceAfterDeferredOnes) {
  Transition(webos::NATIVE_WINDOW_DEFAULT, [&]() {
    transitions_.Run(webos::NATIVE_WINDOW_MINIMIZED, []() {});
  });

  bool ran = false;
  EXPECT_TRUE(
      transitions_.Run(webos::NATIVE_WINDOW_DEFAULT, [&]() { ran = true; }));
  EXPECT_TRUE(ran);
}
