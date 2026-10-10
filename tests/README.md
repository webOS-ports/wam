# WebAppMgr tests and hardening

The unit tests (`src/tests`, gtest/gmock, binary `WebAppMgrUnitTest`) are
built with every build. `tests/hardening/` adds two gates that run on the
x86-64 build host against a Yocto qemux86-64 workdir:

| script | what it runs |
|---|---|
| `static-analysis.sh` | g++ `-fanalyzer` (-O0 and -O2), clang `--analyze` (alpha unix/security/core), clang-tidy, cppcheck (exhaustive), flawfinder, Coccinelle rules in `cocci/`; sparse only for C files (sparse cannot parse C++; WAM has none) |
| `run-sanitizers.sh` | the unit tests with ASan+UBSan (clang and GCC), TSan (clang) and under valgrind memcheck |

Neither touches the workdir: builds go to `build-static-analysis/` and
`build-sanitizers/` in this tree (or `-o DIR`).

## Workdir

Both use the workdir of the `wam-clang` recipe for qemux86-64, after its
configure task has run (the build dir must hold `compile_commands.json`):

```sh
bitbake -c configure wam-clang          # MACHINE=qemux86-64
W=$BUILDDIR/tmp/work/qemux86_64-webos-linux/wam-clang/1.0.2-100
```

## Static analysis

```sh
tests/hardening/static-analysis.sh -p $W/build
tests/hardening/static-analysis.sh -p $W/build src/platform/permission_prompt.cc   # some files only
tests/hardening/static-analysis.sh --selftest                                    # the cocci rules
```

The workdir's compile database names the workdir's copy of the sources; the
script rewrites it to this tree, so it analyses what is checked out here.

Findings are compared with `static-analysis.baseline` (tool, file, check
and message; line numbers ignored). A file a tool could not analyse (memory
cap, timeout, compile error) is a finding of its own. The exit status is 1
for a finding that is not in the baseline, 2 when a tool is not installed or
left out with `SA_SKIP="gcc-O0 gcc-O2"` (reported as SKIPPED, never as
passed; `SA_ALLOW_SKIP=1` accepts that), 0 otherwise. After triaging every
new finding, refresh the baseline with `-u`.

The baseline was made on webOS-OSE 07a3adf. The analyzer was run on the
product code only (not `src/tests`); four files hit its 20G cap or the
30-minute timeout and are listed as such.

`-fanalyzer` takes 10-18 GB per file here, so it runs one file at a time,
last, under a memory cap (`SA_ANALYZER_MEM`, default 20G, through
`systemd-run --user --scope`, else `ulimit -v`); a file that reaches the
cap is reported as an error, not retried. Before each file the script
waits until `SA_MIN_FREE_GB` (30) GB are available. A full run takes
hours: `-r` resumes an interrupted one from what is already in the output
directory, e.g. `nohup tests/hardening/static-analysis.sh -r -p $W/build &`.

Tools are found in `PATH`; override with `GXX`, `CLANG`, `CLANG_TIDY`,
`CPPCHECK`, `FLAWFINDER`, `SPATCH`, `SPARSE`. `-fanalyzer` needs GCC 13 or
newer. GCC reads libc++'s headers in C++20 mode only with
`-Wno-template-body`, which the script adds.

## Sanitizers and valgrind

```sh
tests/hardening/run-sanitizers.sh -w $W                       # all four variants
tests/hardening/run-sanitizers.sh -w $W clang-asan            # one
tests/hardening/run-sanitizers.sh -w $W -f 'UrlTest.*' valgrind
```

Builds use one job per 4 GB available, at most 8 (`-j`), and wait for
`MIN_FREE_GB` (30) GB to be available. The Yocto toolchains have no sanitizer runtimes, so the host's `clang-21`
and `gcc-15` (`HOST_CLANG`, `HOST_GCC`) compile against the workdir's
sysroot and libc++; the test is then pointed at the sysroot's `ld.so` and
libraries with patchelf. The host's glibc must not be older than the
sysroot's. The CMake option `-DWAM_SANITIZE=<list>` adds the sanitizer
flags; it is empty by default and changes nothing then.

Libraries the test loads but WAM does not build against (the libc++ builds
of pbnjson, jsoncpp and the media libraries the web engine uses) are
unpacked from `$TMPDIR/deploy/ipk/corei7-64/*-clang_*.ipk` (`IPK_DIR`), so
build `webruntime-clang` and its dependencies' packages first. To test
against a web engine you built yourself, put its output directory first:
`RUNTIME_LIB_DIRS=<chromium>/out/Release`.

Every test runs in its own process: tests that launch an app reach the real
web engine, which aborts on the host. `host-results.baseline` lists the
tests allowed to fail or abort there (refresh with `-u`); any other failure
and any sanitizer or valgrind report fails the run. Known pre-existing
issues are suppressed, with the reason, in `lsan.supp`, `tsan.supp` and
`valgrind.supp`.
