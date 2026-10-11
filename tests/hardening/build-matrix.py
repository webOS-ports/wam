#!/usr/bin/env python3
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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
#
# The work behind build-matrix.sh; see there and tests/README.md.

import argparse
import collections
import concurrent.futures
import filecmp
import glob
import json
import os
import re
import shlex
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ARCHES = ("armv7", "aarch64", "x86-64")
# Flags only clang understands; GCC gets the rest of the recipe's command
CLANG_ONLY = ("--target=", "--dyld-prefix=", "-stdlib=", "-fuse-ld=")
CLANG_ONLY_EXACT = ("-Wno-unused-command-line-argument",)

DIAG = re.compile(r"^(?P<file>[^:\s][^:]*):(?P<line>\d+):(?:\d+:)? (?P<sev>warning|error|fatal error): "
                  r"(?P<msg>.*?)(?: \[(?P<id>-W[^\]]+|-Werror[^\]]*)\])?$")


def run(cmd, cwd=None, env=None):
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, errors="replace")
    return p.returncode, p.stdout


def ours(path, directory):
    """The path relative to the tree when it is one of WAM's own files."""
    p = os.path.normpath(os.path.join(directory, path))
    if not p.startswith(ROOT + os.sep):
        return None
    r = os.path.relpath(p, ROOT)
    return r if r.startswith("src/") else None


def load_units(build, wanted=()):
    """{tree file: (directory, argv)} from build/compile_commands.json, with
    the paths of the tree the build was configured from pointed at this one.
    A file in |wanted| the build does not know (one added since it was
    configured) borrows the command of another file of the same directory."""
    cache = open(os.path.join(build, "CMakeCache.txt")).read()
    src = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$", cache, re.M).group(1).rstrip("/")
    units = {}
    for e in json.load(open(os.path.join(build, "compile_commands.json"))):
        f = os.path.normpath(os.path.join(e["directory"], e["file"]))
        if not f.startswith(src + "/"):
            continue
        f = ROOT + f[len(src):]
        if not os.path.exists(f):
            continue
        argv = e.get("arguments") or shlex.split(e["command"])
        argv = [a.replace(src + "/", ROOT + "/") for a in argv]
        units[f] = (e["directory"], argv)
    for f in sorted(wanted):
        if f in units:
            continue
        siblings = sorted(u for u in units if os.path.dirname(u) == os.path.dirname(f))
        if not siblings:
            continue
        d, argv = units[siblings[0]]
        # (its -o and -MF are dropped by command())
        units[f] = (d, [f if a == siblings[0] else a for a in argv])
    return units


def overlay(workdir, reference, out, arch):
    """Web engine API headers that are newer in the reference (x86-64)
    sysroot than in this one: the arm sysroots are often staged from an
    older webruntime build. Generated per-arch headers (*buildflags.h,
    build_config) are never taken over."""
    mine = os.path.join(workdir, "recipe-sysroot/usr/include/webruntime")
    ref = os.path.join(reference, "recipe-sysroot/usr/include/webruntime")
    dst = os.path.join(out, "overlay-" + arch)
    shutil.rmtree(dst, ignore_errors=True)
    taken = []
    if not os.path.isdir(ref) or os.path.samefile(mine, ref):
        return None, taken
    for path in glob.glob(os.path.join(ref, "**/*.h"), recursive=True):
        r = os.path.relpath(path, ref)
        if "buildflags" in os.path.basename(r) or "build_config" in r:
            continue
        m = os.path.join(mine, r)
        if os.path.exists(m) and filecmp.cmp(m, path, shallow=False):
            continue
        if os.path.exists(m) and os.path.getmtime(m) >= os.path.getmtime(path):
            continue
        os.makedirs(os.path.dirname(os.path.join(dst, r)), exist_ok=True)
        shutil.copy2(path, os.path.join(dst, r))
        taken.append(r)
    return (dst if taken else None), taken


def command(argv, f, compiler, gxx, ovl):
    out, skip = [], False
    for a in argv[1:]:
        if skip:
            skip = False
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if a in ("-c", "-MD", "-MMD") or a == f:
            continue
        if compiler == "gcc" and (a.startswith(CLANG_ONLY) or a in CLANG_ONLY_EXACT):
            continue
        out.append(a)
    inc = ["-I" + ovl] if ovl else []
    # -Wall -Wextra first, so a -Wno-* of the recipe still wins
    if compiler == "gcc":
        # GCC reads libc++'s headers in C++20 only with -Wno-template-body
        return [gxx, *inc, "-Wall", "-Wextra", *out, "-Wno-template-body", "-c", "-o", "/dev/null", f]
    return [argv[0], *inc, "-Wall", "-Wextra", *out, "-c", "-o", "/dev/null", f]


def main():
    ap = argparse.ArgumentParser(description="warning gate for WebAppMgr across arches and compilers")
    ap.add_argument("--workdir", action="append", default=[], metavar="ARCH=DIR",
                    help="wam-clang workdir of an arch (armv7, aarch64, x86-64), after do_configure")
    ap.add_argument("--build", action="append", default=[], metavar="ARCH=DIR",
                    help="use this build's compile_commands.json for ARCH instead of <workdir>/build")
    ap.add_argument("--arch", default=",".join(ARCHES))
    ap.add_argument("--compiler", default="gcc,clang")
    ap.add_argument("--jobs", "-j", type=int, default=min(8, os.cpu_count() or 4))
    ap.add_argument("--out", default=os.path.join(ROOT, "build-matrix"))
    ap.add_argument("files", nargs="*", help="only these files (relative to the tree)")
    args = ap.parse_args()
    workdirs = dict(w.split("=", 1) for w in args.workdir)
    builds = dict(b.split("=", 1) for b in args.build)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    reference = workdirs.get("x86-64")

    # The files to compile are those of the x86-64 build: an arm build
    # configured before a file was added borrows a command for it. Files the
    # tree has but the build does not compile (DISABLE_PMLOG and tracing
    # options the recipe does not set) are left out.
    ref_units = {}
    if reference:
        ref_units = load_units(builds.get("x86-64", os.path.join(reference, "build")))
    wanted = set(ref_units)
    status, own_found, third, rc = collections.OrderedDict(), [], collections.Counter(), 0
    for arch in args.arch.split(","):
        if arch not in workdirs:
            sys.exit("build-matrix: --workdir %s=DIR is required" % arch)
        wd = os.path.abspath(workdirs[arch])
        units = load_units(builds.get(arch, os.path.join(wd, "build")), wanted)
        # An arm database configured before a CMake change lacks the -D
        # options the x86-64 build now gives a file: take those over.
        for f, (d, argv) in units.items():
            if f in ref_units:
                missing = [a for a in ref_units[f][1] if a.startswith("-D") and a not in argv]
                units[f] = (d, argv[:1] + missing + argv[1:])
        if args.files:
            want = {os.path.join(ROOT, f) for f in args.files}
            units = {f: u for f, u in units.items() if f in want}
        ovl, taken = overlay(wd, reference, out, arch) if reference and arch != "x86-64" else (None, [])
        if taken:
            print("== %s: newer web engine headers from the x86-64 sysroot: %s" % (arch, ", ".join(taken)))
        native = os.path.join(wd, "recipe-sysroot-native/usr/bin")
        gxx = (glob.glob(os.path.join(native, "*", "*-g++")) or [""])[0]
        env = dict(os.environ, PATH=os.path.dirname(gxx) + ":" + native + ":" + os.environ["PATH"])
        for compiler in args.compiler.split(","):
            cell = "%s %s" % (arch, compiler)
            cmds = [(f, d, command(argv, f, compiler, gxx, ovl)) for f, (d, argv) in sorted(units.items())]
            if not cmds or not shutil.which(cmds[0][2][0], path=env["PATH"]):
                status[cell] = "SKIPPED (no compiler)"
                rc = rc or 77
                continue
            print("== %s: %s" % (cell, run([cmds[0][2][0], "--version"], env=env)[1].splitlines()[0]))
            with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
                results = list(ex.map(lambda c: run(c[2], c[1], env), cmds))
            failed, cell_own, cell_third = [], set(), set()
            with open(os.path.join(out, "%s-%s.log" % (arch, compiler)), "w") as log:
                for (f, d, c), (code, text) in zip(cmds, results):
                    log.write("### %s (exit %d)\n%s\n%s\n" % (f, code, " ".join(map(shlex.quote, c)), text))
                    if code != 0:
                        failed.append(os.path.relpath(f, ROOT))
                    for line in text.splitlines():
                        m = DIAG.match(line)
                        if not m or m.group("sev") != "warning":
                            continue
                        r = ours(m.group("file"), d)
                        w = (m.group("file") if r is None else r, int(m.group("line")), m.group("id") or "", m.group("msg"))
                        (cell_third if r is None else cell_own).add(w)
            own_found += [(cell,) + w for w in sorted(cell_own)]
            for w in cell_third:
                third[(cell, re.sub(r"^.*/(usr/include|recipe-sysroot)/", "", w[0]), w[2])] += 1
            status[cell] = "%d file(s), %d warning(s) in WAM's own lines, %d in third-party headers" % (
                len(cmds), len(cell_own), len(cell_third))
            if failed:
                status[cell] += "; FAILED to compile: " + ", ".join(failed)
                rc = 1
            print("   " + status[cell])

    with open(os.path.join(out, "warnings.tsv"), "w") as w:
        for x in own_found:
            w.write("%s:%d\t%s\t%s\t%s\n" % (x[1], x[2], x[0], x[3], x[4]))
    with open(os.path.join(out, "third-party.tsv"), "w") as w:
        for (cell, f, opt), n in sorted(third.items()):
            w.write("%s\t%s\t%s\t%d\n" % (cell, f, opt, n))
    print("\n== summary")
    for cell, st in status.items():
        print("   %-15s %s" % (cell, st))
    if third:
        print("\n== third-party headers (reported, not ours to fix; details in third-party.tsv):")
        for (f, opt), n in sorted(collections.Counter({(k[1], k[2]): 0 for k in third}).items()):
            cells = sorted({k[0] for k in third if (k[1], k[2]) == (f, opt)})
            print("   %s [%s]: %s" % (f, opt, ", ".join(cells)))
    if own_found:
        print("\n== warnings in WAM's own lines (%d):" % len(own_found))
        for x in own_found:
            print("%s:%d: [%s] %s [%s]" % (x[1], x[2], x[0], x[4], x[3]))
        rc = 1
    print("logs: %s" % out)
    sys.exit(rc)


if __name__ == "__main__":
    main()
