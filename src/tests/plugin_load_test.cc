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

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "application_description.h"
#include "plugin_lib_wrapper.h"
#include "plugin_lib_wrapper_mock.h"
#include "temp_dir.h"
#include "web_app_base_mock.h"
#include "web_app_factory_interface_mock.h"
#include "web_app_factory_manager_impl.h"

namespace {

// What WebAppFactoryManagerImpl finds in the plugin directory decides what it
// asks the loader for, so the tests give it a directory of their own with the
// one file the device's /usr/lib/webappmanager/plugins would hold.
constexpr char kPluginName[] = "libwebappmgr-default-plugin.so";

struct PluginDir {
  PluginDir() { valid = !dir.Path().empty() && dir.WriteFile(kPluginName, ""); }
  std::string Plugin() const { return dir.Path() + "/" + kPluginName; }

  TempDir dir;
  bool valid = false;
};

}  // namespace

TEST(PluginLoadTest, Load) {
  const PluginDir plugins;
  ASSERT_TRUE(plugins.valid);
  std::string app_type = "default";
  void* fake_handle = &app_type;
  auto lib_wrapper = std::make_unique<PluginLibWrapperMock>();
  EXPECT_CALL(
      *lib_wrapper,
      Load(plugins.Plugin()))
      .Times(1)
      .WillRepeatedly(::testing::Return(fake_handle));
  EXPECT_CALL(*lib_wrapper, GetAppType(fake_handle))
      .Times(1)
      .WillRepeatedly(::testing::Return(app_type));
  WebAppFactoryInterfaceMock factory_interface_mock;
  EXPECT_CALL(*lib_wrapper, GetCreateInstanceFunction(fake_handle))
      .Times(1)
      .WillRepeatedly([&](void* /*handle*/) {
        return [&]() { return &factory_interface_mock; };
      });
  EXPECT_CALL(*lib_wrapper, GetDeleteInstanceFunction(fake_handle)).Times(1);

  bool const load_plugin_on_demand = false;
  WebAppFactoryManagerImpl::RemovableManagerPtr const factory_manager =
      WebAppFactoryManagerImpl::TestInstance(plugins.dir.Path(),
                                             "", load_plugin_on_demand,
                                             std::move(lib_wrapper));
  (void)factory_manager;
}

TEST(PluginLoadTest, LoadOnDemand) {
  std::string app_type = "default";
  void* fake_handle = &app_type;
  auto lib_wrapper = std::make_unique<PluginLibWrapperMock>();
  EXPECT_CALL(*lib_wrapper, Load("testing::_")).Times(0);
  EXPECT_CALL(*lib_wrapper, GetAppType(fake_handle)).Times(0);
  WebAppFactoryInterfaceMock const factory_interface_mock;
  EXPECT_CALL(*lib_wrapper, GetCreateInstanceFunction(fake_handle)).Times(0);

  bool const load_plugin_on_demand = true;
  WebAppFactoryManagerImpl::RemovableManagerPtr const factory_manager =
      WebAppFactoryManagerImpl::TestInstance("/usr/lib/webappmanager/plugins",
                                             "", load_plugin_on_demand,
                                             std::move(lib_wrapper));
  (void)factory_manager;
}

TEST(PluginLoadTest, DefaultWebApp) {
  const PluginDir plugins;
  ASSERT_TRUE(plugins.valid);
  std::string app_type = "default";
  void* fake_handle = &app_type;
  auto lib_wrapper = std::make_unique<PluginLibWrapperMock>();
  EXPECT_CALL(
      *lib_wrapper,
      Load(plugins.Plugin()))
      .Times(1)
      .WillRepeatedly(::testing::Return(fake_handle));
  EXPECT_CALL(*lib_wrapper, GetAppType(fake_handle))
      .Times(1)
      .WillRepeatedly(::testing::Return(app_type));
  WebAppFactoryInterfaceMock factory_interface_mock;
  WebAppBaseMock app_base_mock;
  EXPECT_CALL(factory_interface_mock,
              CreateWebApp(std::string("_WEBOS_WINDOW_TYPE_CARD"), testing::_))
      .Times(1)
      .WillRepeatedly([&](const std::string&, const ApplicationDescription&) {
        return &app_base_mock;
      });
  EXPECT_CALL(*lib_wrapper, GetCreateInstanceFunction(fake_handle))
      .Times(1)
      .WillRepeatedly([&](void* /*handle*/) {
        return [&]() { return &factory_interface_mock; };
      });
  EXPECT_CALL(*lib_wrapper, GetDeleteInstanceFunction(fake_handle)).Times(1);

  bool const load_plugin_on_demand = true;
  WebAppFactoryManagerImpl::RemovableManagerPtr factory_manager =
      WebAppFactoryManagerImpl::TestInstance(plugins.dir.Path(),
                                             "", load_plugin_on_demand,
                                             std::move(lib_wrapper));
  ASSERT_NE(factory_manager, nullptr);
  WebAppBase const* app_base =
      factory_manager->CreateWebApp("_WEBOS_WINDOW_TYPE_CARD", {}, "default");
  EXPECT_EQ(app_base, &app_base_mock);
}

TEST(PluginLoadTest, CustomWebApp) {
  const PluginDir plugins;
  ASSERT_TRUE(plugins.valid);
  std::string app_type = "custom";
  void* fake_handle = &app_type;
  auto lib_wrapper = std::make_unique<PluginLibWrapperMock>();
  EXPECT_CALL(
      *lib_wrapper,
      Load(plugins.Plugin()))
      .Times(1)
      .WillRepeatedly(::testing::Return(fake_handle));
  EXPECT_CALL(*lib_wrapper, GetAppType(fake_handle))
      .Times(1)
      .WillRepeatedly(::testing::Return(app_type));
  WebAppFactoryInterfaceMock factory_interface_mock;
  WebAppBaseMock app_base_mock;
  EXPECT_CALL(factory_interface_mock,
              CreateWebApp(std::string("_WEBOS_WINDOW_TYPE_CARD"), testing::_))
      .Times(1)
      .WillRepeatedly([&](const std::string&, const ApplicationDescription&) {
        return &app_base_mock;
      });
  EXPECT_CALL(*lib_wrapper, GetCreateInstanceFunction(fake_handle))
      .Times(1)
      .WillRepeatedly([&](void* /*handle*/) {
        return [&]() { return &factory_interface_mock; };
      });
  EXPECT_CALL(*lib_wrapper, GetDeleteInstanceFunction(fake_handle)).Times(1);

  bool const load_plugin_on_demand = true;
  WebAppFactoryManagerImpl::RemovableManagerPtr factory_manager =
      WebAppFactoryManagerImpl::TestInstance(plugins.dir.Path(), "extended:custom:minimal",
          load_plugin_on_demand, std::move(lib_wrapper));
  ASSERT_NE(factory_manager, nullptr);
  WebAppBase const* app_base =
      factory_manager->CreateWebApp("_WEBOS_WINDOW_TYPE_CARD", {}, "custom");
  EXPECT_EQ(app_base, &app_base_mock);
}

TEST(PluginLoadTest, NotAllowedCustomWebApp) {
  auto lib_wrapper = std::make_unique<PluginLibWrapperMock>();
  EXPECT_CALL(*lib_wrapper, Load(testing::_)).Times(0);
  EXPECT_CALL(*lib_wrapper, GetAppType(testing::_)).Times(0);
  EXPECT_CALL(*lib_wrapper, GetCreateInstanceFunction(testing::_)).Times(0);
  EXPECT_CALL(*lib_wrapper, GetDeleteInstanceFunction(testing::_)).Times(0);

  bool const load_plugin_on_demand = true;
  WebAppFactoryManagerImpl::RemovableManagerPtr factory_manager =
      WebAppFactoryManagerImpl::TestInstance(
          "/usr/lib/webappmanager/plugins", "extended:minimal",
          load_plugin_on_demand, std::move(lib_wrapper));
  ASSERT_NE(factory_manager, nullptr);
  WebAppBase const* app_base =
      factory_manager->CreateWebApp("_WEBOS_WINDOW_TYPE_CARD", {}, "custom");
  EXPECT_EQ(app_base, nullptr);
}

TEST(PluginLoadTest, LoadTestPlugin) {
  // The test plugin as installed on the device, or as built next to this
  // test on the build host.
  std::string plugin_dir = "/usr/libexec/tests/webappmanager/plugins";
  std::error_code error;
  if (!std::filesystem::is_directory(plugin_dir, error)) {
    plugin_dir = WAM_TEST_PLUGIN_BUILD_DIR;
  }
  auto lib_wrapper = std::make_unique<PluginLibWrapper>();
  bool const load_plugin_on_demand = true;
  WebAppFactoryManagerImpl::RemovableManagerPtr factory_manager =
      WebAppFactoryManagerImpl::TestInstance(plugin_dir, "testplugin",
                                             load_plugin_on_demand,
                                             std::move(lib_wrapper));
  ASSERT_NE(factory_manager, nullptr);
  // Freed before the factory manager unloads the plugin that made it.
  std::unique_ptr<const WebAppBase> const app_base(
      factory_manager->CreateWebApp("", {}, "testplugin"));
  ASSERT_NE(app_base, nullptr);
  EXPECT_EQ(app_base->AppId(), "pluginTestID");
}
