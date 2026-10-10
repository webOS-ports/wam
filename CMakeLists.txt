# Copyright (c) 2021 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.0)

project(WebAppMgr VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

include(webOS/webOS)
include(FindPkgConfig)

webos_modules_init(1 6 3)
webos_component(1 0 2)

webos_build_system_bus_files(${CMAKE_SOURCE_DIR}/files/sysbus)

# Sanitizer builds for tests/hardening/run-sanitizers.sh, e.g.
# -DWAM_SANITIZE=address,undefined. Empty, the default, changes nothing.
set(WAM_SANITIZE "" CACHE STRING "-fsanitize= list for test builds (empty: none)")
if(WAM_SANITIZE)
    add_compile_options(-fsanitize=${WAM_SANITIZE} -fno-sanitize-recover=all
                        -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${WAM_SANITIZE})
endif()

add_subdirectory(src)
