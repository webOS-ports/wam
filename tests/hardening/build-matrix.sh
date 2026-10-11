#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# build-matrix.sh --workdir armv7=DIR --workdir aarch64=DIR --workdir x86-64=DIR
#                 [--build ARCH=DIR] [--arch LIST] [--compiler gcc,clang]
#                 [-j JOBS] [--out DIR] [FILE...]
#
# Compiles every C++ file of the x86-64 build (product code, plugins and unit
# tests) for armv7, aarch64 and x86-64, each with the Yocto GCC cross compiler
# and with the recipe's clang, using the recipe's flags from that arch's
# compile database plus -Wall -Wextra, and fails on any warning in WAM's own
# lines (src/). Warnings that come from third-party headers (libc++, the web
# engine's, gtest/gmock, glib) are counted and listed, not failed on: they are
# not ours to fix. A file that does not compile fails the run.
#
# DIR is a wam-clang workdir after do_configure (it needs build/
# compile_commands.json, recipe-sysroot and recipe-sysroot-native), e.g.
#   tmp/work/halium_arm-webos-linux-gnueabi/wam-clang/<ver>     (armv7)
#   tmp/work/halium_arm64-webos-linux/wam-clang/<ver>           (aarch64)
#   tmp/work/qemux86_64-webos-linux/wam-clang/<ver>             (x86-64)
# The database names the workdir's copy of the sources; it is pointed at this
# tree, and a file of the x86-64 build it does not know yet borrows the
# command of another file in the same directory; -D options the x86-64 build
# gives a file and an older arm database lacks are added. --build ARCH=DIR takes the
# database from another build of this tree (e.g. one configured with the
# recipe's toolchain.cmake) instead.
#
# The arm sysroots are often staged from an older web engine build than the
# x86-64 one: a web engine header that is newer in the x86-64 sysroot is put
# in front of the arm one (generated *buildflags.h headers excepted), and
# the run says which.
#
# GCC is given -Wno-template-body: without it GCC 15 rejects libc++'s
# headers in C++20 mode. Only compiles (-c -o /dev/null): linking adds no
# warnings in our code.
#
# Exit status: 0 clean, 1 a warning in our lines or a compile failure,
# 77 a compiler was not found.
exec python3 "$(dirname "$0")/build-matrix.py" "$@"
