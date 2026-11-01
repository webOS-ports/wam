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

// WebPageBlink::CreateWindow(), which the engine calls for window.open(): the
// webOS "attributes=" feature is page-controlled JSON and must not be able to
// take WebAppMgr down, whatever it holds.

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glib.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "application_description.h"
#include "mocks/temp_dir.h"
#include "palm_system_blink.h"
#include "platform_module_factory_impl.h"
#include "service_sender.h"
#include "sound_notification.h"
#include "url.h"
#include "utils.h"
#include "web_app_base_mock.h"
#include "web_app_factory_manager.h"
#include "web_app_manager.h"
#include "web_page_blink.h"
#include "web_view_factory.h"
#include "web_view_mock.h"
#include "window_types.h"

namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyNumber;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

constexpr char kAppDesc[] =
    R"({"id":"com.example.opener","defaultWindowType":"card",)"
    R"("main":"index.html","folderPath":"/usr/palm/applications/)"
    R"(com.example.opener","trustLevel":"default"})";

class NewViewFactory : public WebViewFactory {
 public:
  std::unique_ptr<WebView> CreateWebView() override {
    return std::make_unique<NiceWebViewMock>();
  }
};

// A view mock that counts how many exist, to see that one is freed.
class CountedViewMock : public WebViewMock {
 public:
  CountedViewMock() { ++instances; }
  ~CountedViewMock() override { --instances; }
  CountedViewMock(const CountedViewMock&) = delete;
  CountedViewMock& operator=(const CountedViewMock&) = delete;

  static int instances;
};

int CountedViewMock::instances = 0;

class CountedViewFactory : public WebViewFactory {
 public:
  std::unique_ptr<WebView> CreateWebView() override {
    return std::make_unique<::testing::NiceMock<CountedViewMock>>();
  }
};

// window.open() features of a sign-in window watched for its redirect.
constexpr char kWatch[] =
    R"(attributes={"redirectPrefix":"https://example.com/cb"})";

// Hands out a view and remembers which, so a test can expect calls on it.
class RecordingViewFactory : public WebViewFactory {
 public:
  explicit RecordingViewFactory(WebViewMock** view) : view_(view) {}
  std::unique_ptr<WebView> CreateWebView() override {
    auto view = std::make_unique<NiceWebViewMock>();
    *view_ = view.get();
    return view;
  }

 private:
  WebViewMock** view_;
};

// What WAM leaves to the main loop (the window stopped and closed after a
// redirect) happens here.
void RunPending() {
  while (g_main_context_iteration(nullptr, FALSE)) {
  }
}

class CloseRequestCounter : public WebPageObserver {
 public:
  void WebPageClosePageRequested() override { ++requests; }
  int requests = 0;
};

using Properties = std::vector<std::pair<std::string, std::string>>;

// The application the window is created for. It takes the page and the
// description the page refers to as the real one does (WebAppBase), and
// records the window properties set on it.
// WebAppBaseMock makes both no-ops; the calls go past it on purpose.
class WindowAppMock : public WebAppBaseMock {
 public:
  void Attach(WebPageBase* page) override {
    WebAppBase::Attach(page);  // NOLINT(bugprone-parent-virtual-call)
  }
  void SetAppDescription(std::unique_ptr<ApplicationDescription> desc) override {
    // NOLINTNEXTLINE(bugprone-parent-virtual-call)
    WebAppBase::SetAppDescription(std::move(desc));
  }
  void SetWindowProperty(const std::string& name,
                         const std::string& value) override {
    properties_.emplace_back(name, value);
  }

  WebPageBase* AttachedPage() const { return Page(); }
  const Properties& WindowProperties() const { return properties_; }

 private:
  Properties properties_;
};

class WindowAppFactoryManager : public WebAppFactoryManager {
 public:
  // With |make_apps| false no app is made, as WebAppFactoryManagerImpl does
  // for a subtype no factory plugin handles.
  WindowAppFactoryManager(std::string* win_type,
                          WindowAppMock** app,
                          bool make_apps = true)
      : win_type_(win_type), app_(app), make_apps_(make_apps) {}

  WebAppBase* CreateWebApp(const std::string& win_type,
                           const ApplicationDescription& /*desc*/,
                           const std::string& /*app_type*/) override {
    *win_type_ = win_type;
    if (!make_apps_) {
      return nullptr;
    }
    // WebAppFactoryManager hands apps over as raw pointers; the test deletes
    // the app it gets back (CloseApp()).
    *app_ = new WindowAppMock();  // NOLINT(cppcoreguidelines-owning-memory)
    return *app_;
  }
  WebAppBase* CreateWebApp(const std::string& win_type,
                           WebPageBase* /*page*/,
                           const ApplicationDescription& desc,
                           const std::string& app_type) override {
    return CreateWebApp(win_type, desc, app_type);
  }
  WebPageBase* CreateWebPage(const std::string& /*win_type*/,
                             const wam::Url& /*url*/,
                             const ApplicationDescription& /*desc*/,
                             const std::string& /*app_type*/,
                             const std::string& /*launch_params*/) override {
    return nullptr;
  }

 private:
  std::string* win_type_;
  WindowAppMock** app_;
  bool make_apps_;
};

class CreateWindowTest : public ::testing::Test {
 protected:
  void SetUp() override {
    WebAppManager::Instance()->SetPlatformModules(
        std::make_unique<PlatformModuleFactoryImpl>());
    WebAppManager::Instance()->SetWebAppFactory(
        std::make_unique<WindowAppFactoryManager>(&win_type_, &app_));
    desc_ = ApplicationDescription::FromJsonString(kAppDesc);
    ASSERT_TRUE(desc_);
    opener_ = std::make_unique<WebPageBlink>(
        wam::Url("file:///index.html"), *desc_, "{}",
        std::make_unique<NewViewFactory>());
    opener_->Init();
  }

  void TearDown() override {
    CloseWindow();
    for (WindowAppMock*& app : kept_) {
      CloseApp(app);
    }
    opener_.reset();
  }

  void CloseWindow() { CloseApp(app_); }

  static void CloseApp(WindowAppMock*& app) {
    if (app) {
      WebAppManager::Instance()->WebPageRemoved(app->AttachedPage());
      WebAppManager::Instance()->AppDeleted(app);
      delete app;  // NOLINT(cppcoreguidelines-owning-memory): made by the factory above
      app = nullptr;
    }
  }

  // Keeps the window open now aside, so that another can be opened (from it):
  // the factory only ever hands back the last. Closed with the test, or by
  // CloseApp(kept_[i]).
  WebPageBlink* KeepWindow() {
    kept_.push_back(app_);
    app_ = nullptr;
    return static_cast<WebPageBlink*>(kept_.back()->AttachedPage());
  }

  // window.open() from a page that is itself a window, as a login page does.
  void OpenFrom(WebPageBlink* page, WebViewMock** view,
                const std::vector<std::string>& features = {}) {
    const WebView* made = page->CreateWindow(
        "about:blank", std::make_unique<RecordingViewFactory>(view), 0,
        features);
    EXPECT_NE(made, nullptr);
    ASSERT_NE(app_, nullptr);
  }

  // window.open() with these features. The new window has to be created
  // regardless of what they hold.
  void Open(const std::vector<std::string>& features) {
    const WebView* view = opener_->CreateWindow(
        "about:blank", std::make_unique<NewViewFactory>(), 0, features);
    EXPECT_NE(view, nullptr);
    ASSERT_NE(app_, nullptr);
  }

  // An opener whose view the test can watch, and a window from it whose view
  // it can watch too.
  void OpenWatched(const std::vector<std::string>& features) {
    opener_ = std::make_unique<WebPageBlink>(
        wam::Url("file:///index.html"), *desc_, "{}",
        std::make_unique<RecordingViewFactory>(&opener_view_));
    opener_->Init();
    // Whatever else the page runs in its frames is of no interest here.
    EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(_)).Times(AnyNumber());
    const WebView* view = opener_->CreateWindow(
        "about:blank", std::make_unique<RecordingViewFactory>(&window_view_), 0,
        features);
    EXPECT_NE(view, nullptr);
    ASSERT_NE(app_, nullptr);
  }

  WebPageBlink* Window() const {
    return static_cast<WebPageBlink*>(app_->AttachedPage());
  }

  // What the opener's appinfo.json asks for: the window type when the
  // attributes name none.
  static std::string DefaultType() {
    return WebAppManager::Instance()->WindowTypeFromString("card");
  }

  std::unique_ptr<ApplicationDescription> desc_;
  std::unique_ptr<WebPageBlink> opener_;
  std::string win_type_;
  WindowAppMock* app_ = nullptr;
  std::vector<WindowAppMock*> kept_;
  WebViewMock* opener_view_ = nullptr;
  WebViewMock* window_view_ = nullptr;
};

struct ServiceCallRecord {
  std::string url;
  std::string payload;
  std::string app_id;
};

// The bus calls an app makes, in place of the bus.
class RecordingServiceSender : public ServiceSender {
 public:
  explicit RecordingServiceSender(std::vector<ServiceCallRecord>* calls)
      : calls_(calls) {}
  void PostlistRunningApps(std::vector<ApplicationInfo>& /*apps*/) override {}
  void PostWebProcessCreated(const std::string& /*app_id*/,
                             const std::string& /*instance_id*/,
                             uint32_t /*pid*/) override {}
  void ServiceCall(const std::string& url,
                   const std::string& payload,
                   const std::string& app_id) override {
    calls_->push_back({url, payload, app_id});
  }
  void CloseApp(const std::string& /*id*/) override {}

 private:
  std::vector<ServiceCallRecord>* calls_;
};

class RecordingModules : public PlatformModuleFactoryImpl {
 public:
  explicit RecordingModules(std::vector<ServiceCallRecord>* calls)
      : calls_(calls) {}

 protected:
  std::unique_ptr<ServiceSender> CreateServiceSender() override {
    return std::make_unique<RecordingServiceSender>(calls_);
  }

 private:
  std::vector<ServiceCallRecord>* calls_;
};

// PalmSystem.playSoundNotification and setAlertSound from a page's window, as
// the page's shim sends them: anything may arrive here, as any page can call
// setWindowProperty itself.
class PalmSystemSoundTest : public CreateWindowTest {
 protected:
  void SetUp() override {
    CreateWindowTest::SetUp();
    WebAppManager::Instance()->SetPlatformModules(
        std::make_unique<RecordingModules>(&calls_));
    ASSERT_NO_FATAL_FAILURE(Open({}));
    app_->SetAppId("com.example.app");
    palm_system_ = std::make_unique<PalmSystemBlink>(app_);
  }

  void TearDown() override {
    palm_system_.reset();
    CreateWindowTest::TearDown();
    WebAppManager::Instance()->SetPlatformModules(
        std::make_unique<PlatformModuleFactoryImpl>());
  }

  void SetProperty(const std::vector<std::string>& arguments) {
    palm_system_->HandleBrowserControlMessage("setWindowProperty", arguments);
  }

  std::vector<ServiceCallRecord> calls_;
  std::unique_ptr<PalmSystemBlink> palm_system_;
};

}  // namespace

TEST_F(PalmSystemSoundTest, PlayGoesToTheShell) {
  SetProperty({sound_notification::kPlayProperty,
               R"({"soundClass":"alerts","soundFile":"/a/b.mp3","duration":3000})"});

  ASSERT_EQ(calls_.size(), 1U);
  EXPECT_EQ(calls_[0].url, sound_notification::kPlayUrl);
  EXPECT_EQ(calls_[0].app_id, "com.example.app");
  Json::Value payload;
  ASSERT_TRUE(util::StringToJson(calls_[0].payload, payload));
  EXPECT_EQ(payload["appId"].asString(), "com.example.app");
  EXPECT_EQ(payload["soundClass"].asString(), "alerts");
  EXPECT_EQ(payload["soundFile"].asString(), "/a/b.mp3");
  EXPECT_EQ(payload["duration"].asInt(), 3000);
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
}

// A page says nothing about who it is.
TEST_F(PalmSystemSoundTest, ThePageDoesNotNameTheApp) {
  SetProperty({sound_notification::kPlayProperty,
               R"({"soundClass":"alerts","appId":"com.other"})"});

  ASSERT_EQ(calls_.size(), 1U);
  EXPECT_EQ(calls_[0].app_id, "com.example.app");
  Json::Value payload;
  ASSERT_TRUE(util::StringToJson(calls_[0].payload, payload));
  EXPECT_EQ(payload["appId"].asString(), "com.example.app");
}

TEST_F(PalmSystemSoundTest, BadRequestsComeToNothing) {
  for (const char* payload :
       {"", "[]", "null", "{", "1", R"({"soundClass":1})",
        R"({"soundClass":"a","soundFile":"../x"})",
        R"({"soundClass":"a","soundFile":"http://x/y.mp3"})"}) {
    SetProperty({sound_notification::kPlayProperty, payload});
  }
  EXPECT_TRUE(calls_.empty());
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
}

// What Messaging asks for on every message: no such file, so no tone either.
TEST_F(PalmSystemSoundTest, RelativeFileNowhereIsSilent) {
  SetProperty({sound_notification::kPlayProperty,
               R"({"soundClass":"alerts","soundFile":"audios/sent.mp3"})"});
  EXPECT_TRUE(calls_.empty());
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
}

TEST_F(PalmSystemSoundTest, MissingArgument) {
  SetProperty({sound_notification::kPlayProperty});
  SetProperty({});
  EXPECT_TRUE(calls_.empty());
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
}

TEST_F(PalmSystemSoundTest, SetAlertSoundSetsTheProperty) {
  SetProperty({"LuneOS_soundclass", "calendar"});
  SetProperty({sound_notification::kSoundProperty, "/usr/palm/sounds/a.mp3"});
  EXPECT_TRUE(calls_.empty());
  EXPECT_THAT(app_->WindowProperties(),
              UnorderedElementsAre(Pair("LuneOS_soundclass", "calendar"),
                                   Pair("LuneOS_sound", "/usr/palm/sounds/a.mp3")));
}

TEST_F(PalmSystemSoundTest, SetAlertSoundBadPathIsNoSoundFile) {
  SetProperty({sound_notification::kSoundProperty, "../../etc/shadow"});
  EXPECT_THAT(app_->WindowProperties(),
              UnorderedElementsAre(Pair("LuneOS_sound", "")));
}

TEST_F(PalmSystemSoundTest, OtherPropertiesAreNotTouched) {
  SetProperty({"LuneOS_other", "../x"});
  EXPECT_THAT(app_->WindowProperties(),
              UnorderedElementsAre(Pair("LuneOS_other", "../x")));
}

TEST_F(CreateWindowTest, NoAttributes) {
  ASSERT_NO_FATAL_FAILURE(Open({}));
  EXPECT_EQ(win_type_, DefaultType());
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
}

TEST_F(CreateWindowTest, PopupAlert) {
  ASSERT_NO_FATAL_FAILURE(Open({R"(attributes={"window":"popupalert"})"}));
  EXPECT_EQ(win_type_, kWtSystemUi);
  EXPECT_THAT(app_->WindowProperties(),
              UnorderedElementsAre(Pair("LuneOS_window", "popupalert")));
}

// Calendar's reminder and a battery alert: the engine lowercases the features,
// so the file name has to come back from the disk.
TEST_F(CreateWindowTest, PopupSoundGetsItsCaseBack) {
  TempDir tones;
  ASSERT_FALSE(tones.Path().empty());
  ASSERT_TRUE(tones.WriteFile("Flurry.MP3", "x"));
  std::string lowered = tones.Path() + "/flurry.mp3";
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  ASSERT_NO_FATAL_FAILURE(
      Open({R"(attributes={"window":"popupalert","soundclass":"calendar",)"
            R"("sound":")" + lowered + R"("})"}));

  EXPECT_THAT(app_->WindowProperties(),
              UnorderedElementsAre(Pair("LuneOS_window", "popupalert"),
                                   Pair("LuneOS_soundclass", "calendar"),
                                   Pair("LuneOS_sound", tones.Path() + "/Flurry.MP3")));
}

TEST_F(CreateWindowTest, PopupSoundNotAFileNameIsDropped) {
  ASSERT_NO_FATAL_FAILURE(
      Open({R"(attributes={"window":"popupalert","sound":"../../etc/shadow"})"}));
  EXPECT_THAT(app_->WindowProperties(),
              UnorderedElementsAre(Pair("LuneOS_window", "popupalert")));
}

TEST_F(CreateWindowTest, AttributesBecomeWindowProperties) {
  ASSERT_NO_FATAL_FAILURE(
      Open({R"(attributes={"window":"dashboard","name":"x","height":5,)"
            R"("list":[1],"object":{"a":1},"empty":""})"}));

  EXPECT_EQ(win_type_, kWtFloating);
  // Values that have no string form (an array, an object) and empty ones are
  // left out; the others are passed on.
  EXPECT_THAT(app_->WindowProperties(),
              UnorderedElementsAre(Pair("LuneOS_window", "dashboard"),
                                   Pair("LuneOS_name", "x"),
                                   Pair("LuneOS_height", "5")));
}

TEST_F(CreateWindowTest, AttributesThatAreNotAnObject) {
  // Anything but an object is no attributes. An array parsed and then aborted
  // WebAppMgr inside jsoncpp, on the lookup of "window"; the scalars are
  // already rejected by the strict parser and are here so they stay harmless.
  for (const char* feature :
       {"attributes=[]", "attributes=[1]", "attributes=\"x\"",
        "attributes=5", "attributes=true", "attributes=null", "attributes=",
        "attributes={"}) {
    SCOPED_TRACE(feature);
    ASSERT_NO_FATAL_FAILURE(Open({feature}));
    EXPECT_EQ(win_type_, DefaultType());
    EXPECT_THAT(app_->WindowProperties(), IsEmpty());
    CloseWindow();
  }
}

TEST_F(CreateWindowTest, WindowValueThatIsNotAString) {
  ASSERT_NO_FATAL_FAILURE(Open({R"(attributes={"window":[]})"}));
  EXPECT_EQ(win_type_, DefaultType());
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
}

TEST_F(CreateWindowTest, LastAttributesFeatureWins) {
  ASSERT_NO_FATAL_FAILURE(
      Open({R"(attributes={"window":"popupalert"})", "attributes=[]"}));
  EXPECT_EQ(win_type_, DefaultType());
}

TEST_F(CreateWindowTest, AttributesOnlyAsTheFeatureKey) {
  // "attributes=" further into a feature is not the attributes feature; the
  // first 11 characters must not be skipped on the strength of it.
  ASSERT_NO_FATAL_FAILURE(
      Open({R"(01234567890{"window":"popupalert","a":"attributes="})"}));
  EXPECT_EQ(win_type_, DefaultType());
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
}

TEST_F(CreateWindowTest, NoAppForTheWindow) {
  WebAppManager::Instance()->SetWebAppFactory(
      std::make_unique<WindowAppFactoryManager>(&win_type_, &app_,
                                                /*make_apps=*/false));
  const int views = CountedViewMock::instances;

  const WebView* view = opener_->CreateWindow(
      "about:blank", std::make_unique<CountedViewFactory>(), 0, {});

  // No window, and nothing left behind: the page made for it, and the view
  // it took from the engine, are freed.
  EXPECT_EQ(view, nullptr);
  EXPECT_EQ(app_, nullptr);
  EXPECT_EQ(CountedViewMock::instances, views);
}

TEST_F(CreateWindowTest, PageCarriesTheInstanceIdOfItsApp) {
  ASSERT_NO_FATAL_FAILURE(Open({}));
  const WebPageBase* page = app_->AttachedPage();
  ASSERT_NE(page, nullptr);

  // A launched page has the instance id from its launch parameters; this one
  // is given its app's. A renderer crash looks the app up by it.
  EXPECT_FALSE(app_->InstanceId().empty());
  EXPECT_EQ(page->InstanceId(), app_->InstanceId());
  EXPECT_EQ(WebAppManager::Instance()->FindAppByInstanceId(page->InstanceId()),
            app_);
}

TEST_F(CreateWindowTest, RendererCrashLeavesThePageToItsApp) {
  ASSERT_NO_FATAL_FAILURE(Open({}));
  auto* page = static_cast<WebPageBlink*>(app_->AttachedPage());
  ASSERT_NE(page, nullptr);

  // The window shares its opener's renderer. When that one crashed as the
  // opener closed, the page did not find its app, deleted itself and left the
  // app owning freed memory: WebAppMgr crashed on the window's next focus
  // change. The app has to keep a live page, which CloseWindow() frees.
  page->RenderProcessCrashed();
  EXPECT_EQ(app_->AttachedPage(), page);
  EXPECT_EQ(page->InstanceId(), app_->InstanceId());
}

// A sign-in window: the opener cannot see it reach its redirect (another
// origin, and providers' Cross-Origin-Opener-Policy cuts it off), so WAM
// tells it, and the window goes before the redirect page can run.
TEST_F(CreateWindowTest, RedirectReachesTheOpener) {
  // Told at once that the window is watched: how a page knows this WAM does it.
  WebViewMock* opener_view = nullptr;
  opener_ = std::make_unique<WebPageBlink>(
      wam::Url("file:///index.html"), *desc_, "{}",
      std::make_unique<RecordingViewFactory>(&opener_view));
  opener_->Init();
  EXPECT_CALL(*opener_view, RunJavaScriptInAllFrames(_)).Times(AnyNumber());
  EXPECT_CALL(*opener_view, RunJavaScriptInAllFrames(AllOf(HasSubstr("webOSOAuthWindow"),
                                                HasSubstr(R"(state:"watching")"))))
      .Times(1);
  const WebView* view = opener_->CreateWindow(
      "about:blank", std::make_unique<RecordingViewFactory>(&window_view_), 0,
      {kWatch});
  ASSERT_NE(view, nullptr);
  ASSERT_NE(app_, nullptr);
  // The prefix is for WAM, not a window property.
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
  ::testing::Mock::VerifyAndClearExpectations(opener_view);

  WebPageBlink* window = Window();
  CloseRequestCounter closes;
  window->AddObserver(&closes);
  EXPECT_CALL(*opener_view, RunJavaScriptInAllFrames(_)).Times(AnyNumber());
  EXPECT_CALL(*opener_view, RunJavaScriptInAllFrames(HasSubstr("webOSOAuthWindow"))).Times(0);
  EXPECT_CALL(*window_view_, StopLoading()).Times(0);

  // The login pages, and a frame inside one going to the prefix: not it.
  window->DidStartNavigation("https://login.example.com/authorize?x=1", true);
  window->DidStartNavigation("https://example.com/other", true);
  window->DidStartNavigation("https://example.com/cb?code=frame", false);
  EXPECT_EQ(closes.requests, 0);
  ::testing::Mock::VerifyAndClearExpectations(opener_view);
  ::testing::Mock::VerifyAndClearExpectations(window_view_);

  // The redirect, with what would break out of the script string.
  EXPECT_CALL(*opener_view, RunJavaScriptInAllFrames(_)).Times(AnyNumber());
  EXPECT_CALL(*opener_view,
              RunJavaScriptInAllFrames(AllOf(HasSubstr(R"(state:"redirect")"),
                                  HasSubstr(R"(url:"https://example.com/cb?code=a\"b\\c\n")"),
                                  Not(HasSubstr("\n")))))
      .Times(1);
  // The opener is told at once; the window is stopped and closed only after
  // the engine's callback has returned (stopping it inside deletes the
  // navigation the engine is in the middle of, which aborts WebAppMgr).
  EXPECT_CALL(*window_view_, StopLoading()).Times(0);
  window->DidStartNavigation("https://example.com/cb?code=a\"b\\c\n", true);
  EXPECT_EQ(closes.requests, 0);
  ::testing::Mock::VerifyAndClearExpectations(opener_view);
  ::testing::Mock::VerifyAndClearExpectations(window_view_);
  EXPECT_CALL(*window_view_, StopLoading()).Times(::testing::AtLeast(1));
  RunPending();
  EXPECT_EQ(closes.requests, 1);

  // Once only, and going after the redirect is no "closed".
  EXPECT_CALL(*opener_view, RunJavaScriptInAllFrames(_)).Times(AnyNumber());
  EXPECT_CALL(*opener_view, RunJavaScriptInAllFrames(HasSubstr("webOSOAuthWindow"))).Times(0);
  window->DidStartNavigation("https://example.com/cb?code=again", true);
  RunPending();
  EXPECT_EQ(closes.requests, 1);
  window->RemoveObserver(&closes);
  CloseWindow();
}

// What the engine hands over: it lowercases the whole features string, so
// the key and the prefix arrive lowercased (seen on a device). The redirect
// is still caught, and handed on with its code as it was.
TEST_F(CreateWindowTest, FeaturesAsTheEngineLowercasesThem) {
  ASSERT_NO_FATAL_FAILURE(OpenWatched(
      {R"(attributes={"redirectprefix":"https://tidal.com/login/auth"})"}));
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(AllOf(
                                 HasSubstr(R"(state:"redirect")"),
                                 HasSubstr(R"(url:"https://Tidal.com/Login/auth?code=AbC")"))))
      .Times(1);
  Window()->DidStartNavigation("https://Tidal.com/Login/auth?code=AbC", true);
  CloseWindow();
}

// An HTTP redirect (30x) to the prefix starts no navigation of its own; the
// url it commits to is the redirect.
TEST_F(CreateWindowTest, RedirectSeenWhenItCommits) {
  ASSERT_NO_FATAL_FAILURE(OpenWatched({kWatch}));
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(AllOf(
                                 HasSubstr(R"(state:"redirect")"),
                                 HasSubstr(R"(url:"https://example.com/cb?code=c")"))))
      .Times(1);
  Window()->DidFinishNavigation("https://example.com/other", true);
  Window()->DidFinishNavigation("https://example.com/cb?code=f", false);
  Window()->DidFinishNavigation("https://example.com/cb?code=c", true);
  CloseWindow();
}

// A redirect to an address nothing listens on (http://localhost/...) fails to
// load: it is still the redirect.
TEST_F(CreateWindowTest, RedirectSeenWhenItFailsToLoad) {
  ASSERT_NO_FATAL_FAILURE(OpenWatched(
      {R"(attributes={"redirectPrefix":"http://localhost/cb"})"}));
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(AllOf(
                                 HasSubstr(R"(state:"redirect")"),
                                 HasSubstr(R"(url:"http://localhost/cb?code=l")"))))
      .Times(1);
  Window()->LoadFailed("http://localhost/elsewhere", -102);
  // Not stopped inside the engine's failed-load callback: the device aborted.
  EXPECT_CALL(*window_view_, StopLoading()).Times(0);
  Window()->LoadFailed("http://localhost/cb?code=l", -102);
  ::testing::Mock::VerifyAndClearExpectations(window_view_);
  EXPECT_CALL(*window_view_, StopLoading()).Times(::testing::AtLeast(1));
  RunPending();
  CloseWindow();
}

TEST_F(CreateWindowTest, WindowClosedWithoutRedirect) {
  ASSERT_NO_FATAL_FAILURE(OpenWatched({kWatch}));
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(AllOf(HasSubstr("webOSOAuthWindow"),
                                                 HasSubstr(R"(state:"closed")"))))
      .Times(1);
  CloseWindow();
}

TEST_F(CreateWindowTest, OpenerGoesFirst) {
  ASSERT_NO_FATAL_FAILURE(OpenWatched({kWatch}));
  WebPageBlink* window = Window();
  // The page that waited for the redirect is gone: nothing to tell, and
  // nothing of it may be reached (ASan in run-sanitizers.sh).
  opener_.reset();
  opener_view_ = nullptr;
  window->DidStartNavigation("https://example.com/cb?code=x", true);
  CloseWindow();
}

// A redirect to an app's own scheme (Teams' msauth.<client>://auth) is never
// loaded: the engine aborts it, and that is where it is caught. The opener is
// told from WAM, so it does not matter that the window runs in a renderer of
// its own, out of the opener's reach.
TEST_F(CreateWindowTest, NativeSchemeRedirectSeenWhenAborted) {
  ASSERT_NO_FATAL_FAILURE(OpenWatched(
      {R"(attributes={"redirectprefix":"msauth.com.microsoft.teams://auth"})"}));
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(_)).Times(AnyNumber());
  EXPECT_CALL(*opener_view_,
              RunJavaScriptInAllFrames(AllOf(
                  HasSubstr(R"(state:"redirect")"),
                  HasSubstr(R"(url:"msauth.com.microsoft.teams://auth/?code=M.C5_Ab")"))))
      .Times(1);
  // Not the redirect: another app's scheme, and the login page itself.
  Window()->LoadAborted("msauth.other://auth/?code=no");
  Window()->LoadAborted("https://login.live.com/oauth20_authorize.srf");
  EXPECT_CALL(*window_view_, StopLoading()).Times(0);
  Window()->LoadAborted("msauth.com.microsoft.teams://auth/?code=M.C5_Ab");
  ::testing::Mock::VerifyAndClearExpectations(window_view_);
  EXPECT_CALL(*window_view_, StopLoading()).Times(::testing::AtLeast(1));
  RunPending();
  CloseWindow();
}

TEST_F(CreateWindowTest, OnlyAnHttpUrlOrAnAppSchemeIsWatched) {
  for (const char* prefix :
       {"", "javascript:alert(1)", "file:///usr/palm", "https://", "https:///x",
        "http://", "about:blank", "msauth.x://", "msauth.x:///auth", "://auth",
        "1app://auth", "my app://auth", "JavaScript://x", "FILE://x/y",
        "data://x", "blob://x", "chrome://settings", "view-source://x"}) {
    SCOPED_TRACE(prefix);
    ASSERT_NO_FATAL_FAILURE(OpenWatched(
        {std::string("attributes={\"redirectPrefix\":\"") + prefix + "\"}"}));
    // Neither watched nor a window property.
    EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(HasSubstr("webOSOAuthWindow"))).Times(0);
    EXPECT_THAT(app_->WindowProperties(), IsEmpty());
    Window()->DidStartNavigation(prefix, true);
    CloseWindow();
    ::testing::Mock::VerifyAndClearExpectations(opener_view_);
  }
}

// Box's login opens each step of its sign-in in a new window of its own, and
// the redirect comes in the last (seen on a VM: windows 1001 and 1002 came
// from taps in the login, and the redirect failed to load in 1002, unseen).
// Such a window is watched for the same sign-in, for the page that started it.
TEST_F(CreateWindowTest, RedirectInAWindowTheLoginOpened) {
  ASSERT_NO_FATAL_FAILURE(OpenWatched(
      {R"(attributes={"redirectPrefix":"http://localhost/boxnet/oauth2callback"})"}));
  WebViewMock* first_view = window_view_;
  WebPageBlink* first = KeepWindow();
  CloseRequestCounter first_closes;
  first->AddObserver(&first_closes);
  WebViewMock* second_view = nullptr;
  ASSERT_NO_FATAL_FAILURE(OpenFrom(first, &second_view));
  WebPageBlink* second = KeepWindow();
  WebViewMock* third_view = nullptr;
  ASSERT_NO_FATAL_FAILURE(OpenFrom(second, &third_view));
  // The window the login opened is no window property of anything either.
  EXPECT_THAT(app_->WindowProperties(), IsEmpty());
  ::testing::Mock::VerifyAndClearExpectations(opener_view_);

  WebPageBlink* third = Window();
  CloseRequestCounter third_closes;
  third->AddObserver(&third_closes);
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(_)).Times(AnyNumber());
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(HasSubstr(R"(state:"closed")")))
      .Times(0);
  EXPECT_CALL(*opener_view_,
              RunJavaScriptInAllFrames(AllOf(
                  HasSubstr(R"(state:"redirect")"),
                  HasSubstr(R"(url:"http://localhost/boxnet/oauth2callback?state=cloud&code=X")"))))
      .Times(1);
  third->LoadFailed("http://localhost/boxnet/oauth2callback?state=cloud&code=X", -102);
  // The sign-in is over: every window of it goes, after the engine's callback.
  EXPECT_CALL(*first_view, StopLoading()).Times(::testing::AtLeast(1));
  EXPECT_CALL(*second_view, StopLoading()).Times(::testing::AtLeast(1));
  EXPECT_CALL(*third_view, StopLoading()).Times(::testing::AtLeast(1));
  RunPending();
  EXPECT_EQ(first_closes.requests, 1);
  EXPECT_EQ(third_closes.requests, 1);

  // Once only: another window of it reaching the redirect is not told again.
  first->DidStartNavigation("http://localhost/boxnet/oauth2callback?code=Y", true);
  RunPending();
  first->RemoveObserver(&first_closes);
  third->RemoveObserver(&third_closes);
  // Going after the redirect is no "closed", for any of them.
  CloseWindow();
  CloseApp(kept_[1]);
  CloseApp(kept_[0]);
  ::testing::Mock::VerifyAndClearExpectations(opener_view_);
}

// Closing the first window - the one the sign-in page opened, which the user
// may well swipe away - is no cancelled sign-in while the login goes on in
// another; the last one going without a redirect is.
TEST_F(CreateWindowTest, ClosedWhenTheLastWindowOfTheSignInGoes) {
  ASSERT_NO_FATAL_FAILURE(OpenWatched({kWatch}));
  WebPageBlink* first = KeepWindow();
  WebViewMock* second_view = nullptr;
  ASSERT_NO_FATAL_FAILURE(OpenFrom(first, &second_view));
  ::testing::Mock::VerifyAndClearExpectations(opener_view_);

  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(_)).Times(AnyNumber());
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(HasSubstr(R"(state:"closed")")))
      .Times(0);
  CloseApp(kept_[0]);
  ::testing::Mock::VerifyAndClearExpectations(opener_view_);

  // The window outlives the one it came from, and still reports.
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(_)).Times(AnyNumber());
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(HasSubstr(R"(state:"redirect")")))
      .Times(0);
  Window()->DidStartNavigation("https://example.com/elsewhere", true);
  ::testing::Mock::VerifyAndClearExpectations(opener_view_);
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(_)).Times(AnyNumber());
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(HasSubstr(R"(state:"closed")")))
      .Times(1);
  CloseWindow();
}

// Two sign-ins of one page (a second try while the first window is still
// open) are kept apart: each ends on its own.
TEST_F(CreateWindowTest, SignInsOfOnePageKeptApart) {
  ASSERT_NO_FATAL_FAILURE(OpenWatched({kWatch}));
  KeepWindow();
  WebViewMock* second_view = nullptr;
  ASSERT_NO_FATAL_FAILURE(OpenFrom(opener_.get(), &second_view, {kWatch}));
  ::testing::Mock::VerifyAndClearExpectations(opener_view_);

  // The first going, while the second is still open, is its own sign-in
  // cancelled.
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(_)).Times(AnyNumber());
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(HasSubstr(R"(state:"closed")")))
      .Times(1);
  CloseApp(kept_[0]);
  ::testing::Mock::VerifyAndClearExpectations(opener_view_);

  // The second carries on, and its redirect still comes through.
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(_)).Times(AnyNumber());
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(AllOf(
                                 HasSubstr(R"(state:"redirect")"),
                                 HasSubstr(R"(url:"https://example.com/cb?code=2")"))))
      .Times(1);
  Window()->DidStartNavigation("https://example.com/cb?code=2", true);
  RunPending();
  CloseWindow();
}

// A window from a page that is no sign-in window is no sign-in window.
TEST_F(CreateWindowTest, WindowOfAnUnwatchedWindowIsNotWatched) {
  ASSERT_NO_FATAL_FAILURE(OpenWatched({}));
  WebPageBlink* first = KeepWindow();
  EXPECT_CALL(*opener_view_, RunJavaScriptInAllFrames(HasSubstr("webOSOAuthWindow"))).Times(0);
  WebViewMock* second_view = nullptr;
  ASSERT_NO_FATAL_FAILURE(OpenFrom(first, &second_view));
  Window()->DidStartNavigation("https://example.com/cb?code=x", true);
  RunPending();
  CloseWindow();
}
