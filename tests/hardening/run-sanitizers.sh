#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# run-sanitizers.sh
#
# Builds WebAppMgrUnitTest from this tree with sanitizers and runs it on the
# x86-64 build host against the libraries of a Yocto qemux86-64 workdir:
#
#   clang-asan   AddressSanitizer + UndefinedBehaviorSanitizer, clang
#   gcc-asan     the same with GCC
#   clang-tsan   ThreadSanitizer, clang
#   valgrind     memcheck over a build with the recipe's own flags
#
# The Yocto toolchains are built without sanitizer runtimes, so the host's
# clang and GCC compile against the workdir's sysroot instead (same glibc
# major version needed: the test is loaded by the sysroot's ld.so). Every
# test runs in its own process, because some tests abort on the host (they
# reach the real web engine, which needs a device); the outcome of each test
# is compared with host-results.baseline and any sanitizer or valgrind
# report fails the run.
#
# Usage: run-sanitizers.sh -w WORKDIR [-o OUT_DIR] [-j JOBS] [-f GTEST_FILTER]
#                          [-u] [VARIANT...]
#   WORKDIR  e.g. tmp/work/qemux86_64-webos-linux/wam-clang/<version>
#   -u       write the per-test outcomes of the plain build to
#            host-results.baseline instead of comparing
#
# Environment: HOST_CLANG (clang-21), HOST_GCC (gcc-15), IPK_DIR (where the
# runtime-only libraries are unpacked from, default
# <TMPDIR>/deploy/ipk/corei7-64), RUNTIME_LIB_DIRS (extra library dirs,
# searched first), EXTRA_CMAKE_ARGS, MIN_FREE_GB (wait before each build
# until this much memory is available, default 30). -j defaults to one
# job per 4 GB available, at most 8.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
HOST_CLANG=${HOST_CLANG:-clang-21}
HOST_GCC=${HOST_GCC:-gcc-15}

die() { echo "run-sanitizers.sh: $*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

WORKDIR=""; OUT=""; # One job per 4 GB available: a TU with the web engine headers takes 2-7 GB
JOBS=$(awk -v n="$(nproc)" '/^MemAvailable/{j=int($2/4194304); if(j<1)j=1; if(j>n)j=n; if(j>8)j=8; print j}' /proc/meminfo); FILTER='*'; UPDATE=0
while getopts w:o:j:f:u opt; do
	case $opt in
	w) WORKDIR=$OPTARG ;; o) OUT=$OPTARG ;; j) JOBS=$OPTARG ;;
	f) FILTER=$OPTARG ;; u) UPDATE=1 ;; *) die "bad option" ;;
	esac
done
shift $((OPTIND - 1))
VARIANTS=${*:-clang-asan gcc-asan clang-tsan valgrind}
[ -n "$WORKDIR" ] || die "-w WORKDIR is required"
WORKDIR=$(cd "$WORKDIR" && pwd)
S=$WORKDIR/recipe-sysroot
NATIVE=$WORKDIR/recipe-sysroot-native
[ -f "$WORKDIR/toolchain.cmake" ] && [ -d "$S" ] || die "$WORKDIR has no toolchain.cmake/recipe-sysroot (run bitbake -c configure wam first)"
OUT=${OUT:-$ROOT/build-sanitizers}
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
TMPDIR_OE=$(cd "$WORKDIR/../../../.." && pwd)
IPK_DIR=${IPK_DIR:-$TMPDIR_OE/deploy/ipk/corei7-64}

PATCHELF=$(command -v patchelf || ls "$NATIVE/usr/bin/patchelf" "$TMPDIR_OE"/sysroots-components/x86_64/patchelf-native/usr/bin/patchelf 2>/dev/null | head -1)
[ -n "$PATCHELF" ] || die "patchelf not found (needed so the sanitizers can symbolize)"

# Libraries the test needs at run time but WAM does not build against (the
# libc++ builds of pbnjson, jsoncpp and the media libraries the web engine
# loads) are not in the recipe sysroot: unpack them from the deployed ipks.
stage_runtime() {
	RT=$OUT/runtime
	[ -d "$RT/usr/lib/cbe" ] && return
	have ar || die "ar not found"
	mkdir -p "$RT"
	for ipk in "$IPK_DIR"/*-clang_*.ipk; do
		[ -e "$ipk" ] || continue
		case $(basename "$ipk") in *-dev_*|*-dbg_*|*-src_*|*-staticdev_*|*-lic_*|*-doc_*|*-ptest_*) continue ;; esac
		tmp=$OUT/ipk.tmp; rm -rf "$tmp"; mkdir -p "$tmp"
		(cd "$tmp" && ar x "$ipk" && tar -xf data.tar.* -C "$RT") || die "cannot unpack $ipk"
		rm -rf "$tmp"
	done
	[ -d "$RT/usr/lib/cbe" ] || die "no libc++ runtime libraries found in $IPK_DIR (set IPK_DIR or RUNTIME_LIB_DIRS)"
}

# toolchain file for a variant: the recipe's, with the host compiler swapped
# in where a sanitizer runtime is needed
toolchain() {
	v=$1; tc=$OUT/toolchain-$v.cmake
	case $v in
	plain) cp "$WORKDIR/toolchain.cmake" "$tc"; return ;;
	esac
	sed -e '/CMAKE_C_COMPILER_LAUNCHER\|CMAKE_CXX_COMPILER_LAUNCHER\|find_program( CMAKE_AR/d' \
	    -e 's/-D_FORTIFY_SOURCE=2/-U_FORTIFY_SOURCE/' \
	    -e 's/ -O2 / -O1 /g' "$WORKDIR/toolchain.cmake" >"$tc"
	case $v in
	clang-*)
		cc=$(command -v "$HOST_CLANG") || return 1
		cxx=$(command -v "$(echo "$HOST_CLANG" | sed 's/clang/clang++/')") || return 1
		sed -i -e "s|set( CMAKE_C_COMPILER clang )|set( CMAKE_C_COMPILER $cc )|" \
		       -e "s|set( CMAKE_CXX_COMPILER clang++ )|set( CMAKE_CXX_COMPILER $cxx )|" \
		       -e "s|set( CMAKE_ASM_COMPILER clang )|set( CMAKE_ASM_COMPILER $cc )|" "$tc"
		echo "set( CMAKE_AR $(command -v llvm-ar-"${HOST_CLANG#clang-}" || command -v llvm-ar || command -v ar) )" >>"$tc" ;;
	gcc-*)
		cc=$(command -v "$HOST_GCC") || return 1
		cxx=$(command -v "$(echo "$HOST_GCC" | sed 's/gcc/g++/')") || return 1
		# GCC knows none of clang's target/libc++ driver flags: libc++ is
		# used by hand. Its headers trip GCC's C++23-only checks in C++20.
		sed -i -e "s|set( CMAKE_C_COMPILER clang )|set( CMAKE_C_COMPILER $cc )|" \
		       -e "s|set( CMAKE_CXX_COMPILER clang++ )|set( CMAKE_CXX_COMPILER $cxx )|" \
		       -e "s|set( CMAKE_ASM_COMPILER clang )|set( CMAKE_ASM_COMPILER $cc )|" \
		       -e 's/--dyld-prefix=[^ ]*//g; s/--target=[^ ]*//g; s/-stdlib=libc++//g' \
		       -e 's/-fuse-ld=[^ ]*//g; s/-Wno-unused-command-line-argument//g' \
		       -e 's/-Wformat /-Wno-template-body -Wformat /g' \
		       -e 's/\(set( CMAKE_CXX_LINK_FLAGS "\)/\1 -Wl,--allow-shlib-undefined -nostdlib++ /' "$tc"
		echo 'set( CMAKE_CXX_STANDARD_LIBRARIES "-lc++ -lc++abi -lm" CACHE STRING "" FORCE )' >>"$tc"
		# GCC links its sanitizer runtimes dynamically into the shared
		# libraries (a static one in the test as well is refused at start):
		# copy them next to the test, away from the host's glibc
		mkdir -p "$OUT/gcc-runtime"
		for l in libasan.so libubsan.so; do
			cp -L "$("$cc" -print-file-name=$l)" "$OUT/gcc-runtime/$(readelf -d "$("$cc" -print-file-name=$l)" | sed -n 's/.*SONAME.*\[\(.*\)\]/\1/p')"
		done
		echo "set( CMAKE_AR $(command -v "$(echo "$HOST_GCC" | sed 's/gcc/gcc-ar/')" || command -v ar) )" >>"$tc" ;;
	esac
}

libs() {
	b=$OUT/build-$1
	echo "${RUNTIME_LIB_DIRS:+$RUNTIME_LIB_DIRS:}$OUT/gcc-runtime:$OUT/runtime/usr/lib/cbe:$OUT/runtime/usr/lib:$S/usr/lib/cbe:$b/src/core:$b/src/platform:$b/src/util:$S/usr/lib"
}

build() {
	v=$1; san=$2; b=$OUT/build-$v
	# A sanitizer build of a file with the web engine's headers takes up to
	# 4 GB: wait for memory rather than push the machine into swap
	while [ "$(awk '/^MemAvailable/{print int($2/1048576)}' /proc/meminfo)" -lt "${MIN_FREE_GB:-30}" ]; do
		sleep 30
	done
	toolchain "$v" || { echo "$v: host compiler not installed"; return 1; }
	(
		export PKG_CONFIG_DISABLE_UNINSTALLED=yes PKG_CONFIG_PATH=
		export PKG_CONFIG_LIBDIR="$S/usr/lib/pkgconfig:$S/usr/share/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$S"
		export PKG_CONFIG_SYSTEM_INCLUDE_PATH="$S/usr/include" PKG_CONFIG_SYSTEM_LIBRARY_PATH="$S/usr/lib"
		# as in bitbake: the cross binutils live in a per-target subdirectory
		for d in "$NATIVE"/usr/bin/*-linux*; do [ -d "$d" ] && PATH="$d:$PATH"; done
		export PATH="$NATIVE/usr/bin:$PATH"
		# A cached toolchain would keep the flags of an earlier run
		rm -f "$b/CMakeCache.txt"
		# shellcheck disable=SC2086
		cmake -G Ninja -DCMAKE_MAKE_PROGRAM="$(command -v ninja)" -S "$ROOT" -B "$b" -DCMAKE_TOOLCHAIN_FILE="$OUT/toolchain-$v.cmake" \
			-DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_NO_SYSTEM_FROM_IMPORTED=1 \
			-DWEBOS_INSTALL_ROOT:PATH=/ -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DWEBOS_TARGET_MACHINE:STRING=qemux86-64 \
			-DWAM_DATA_DIR='"/media/cryptofs/.webappmanager/"' -DPLATFORM=PLATFORM_LUNEOS \
			-DWEBOS_TESTS_DIR=/usr/opt/webos/tests -DWAM_BUILD_DEFAULT_PLUGIN=1 -DWEBOS_SMACK_ENABLED:BOOLEAN=False \
			-DWAM_SANITIZE="$san" ${EXTRA_CMAKE_ARGS:-} -Wno-dev >"$OUT/configure-$v.log" 2>&1 &&
		ninja -C "$b" -j "$JOBS" WebAppMgrUnitTest >"$OUT/build-$v.log" 2>&1
	) || { echo "$v: build failed, see $OUT/build-$v.log"; return 1; }
	# Point the test at the sysroot's loader and libraries. Running it as
	# "ld.so test" instead would make /proc/self/exe the loader, and the
	# sanitizers could then neither symbolize nor apply suppressions.
	t=$b/src/tests/WebAppMgrUnitTest
	LIBS=$(libs "$v")
	cp "$t" "$t.host"
	"$PATCHELF" --set-interpreter "$S/usr/lib/ld-linux-x86-64.so.2" --force-rpath --set-rpath "$LIBS" "$t.host"
}

# Run every test of variant $1 alone; $2 is a wrapper (valgrind) or empty.
# Writes $OUT/results-$1.txt: "<outcome> <test>[ SAN [<first report>]]".
run_tests() {
	v=$1; wrap=$2; t=$OUT/build-$v/src/tests/WebAppMgrUnitTest.host; res=$OUT/results-$v.txt; logs=$OUT/logs-$v
	rm -rf "$logs"; mkdir -p "$logs"; : >"$res"
	export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:strict_string_checks=1:detect_stack_use_after_return=1
	export LSAN_OPTIONS=suppressions=$HERE/lsan.supp:print_suppressions=0
	export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
	export TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1:suppressions=$HERE/tsan.supp
	export G_SLICE=always-malloc G_DEBUG=gc-friendly DEBUGINFOD_URLS=
	# The WAM libraries carry a RUNPATH of their own (/usr/lib/cbe), which
	# makes the loader ignore the test's RPATH for their dependencies
	LD_LIBRARY_PATH=$(libs "$v"); export LD_LIBRARY_PATH
	"$t" --gtest_list_tests --gtest_filter="$FILTER" 2>/dev/null |
		awk '/^[^ ]/{s=$1} /^  /{print s $1}' >"$OUT/tests-$v.txt"
	[ -s "$OUT/tests-$v.txt" ] || { echo "$v: test binary does not start, see: $t --gtest_list_tests"; return 1; }
	# Run inside the log directory: valgrind leaves a vgcore file in the
	# current directory for every test that aborts
	xargs -P "$JOBS" -I{} sh -c '
		cd "$1" || exit 1
		log="$1/$(echo "$2" | tr "/" "_").log"
		# shellcheck disable=SC2086
		timeout 600 $3 "$4" --gtest_filter="$2" >"$log" 2>&1; rc=$?
		rep=$(grep -m1 -o -E "ERROR: (Address|Leak|Thread|UndefinedBehavior)Sanitizer: [a-z-]+|WARNING: ThreadSanitizer: [a-z -]+|runtime error: .{0,80}|== (Invalid|Mismatched|Conditional|Use of uninitialised|Syscall)[^(]{0,60}|[0-9,]+ bytes in [0-9,]+ blocks are (definitely|indirectly) lost" "$log" | head -1)
		if [ $rc -eq 0 ] && grep -q "\[  PASSED  \] 1 test" "$log"; then r=PASS
		elif grep -q "^\[  FAILED  \] $2" "$log"; then r=FAIL
		else r="CRASH($rc)"; fi
		echo "$r $2${rep:+ SAN [$rep]}"
	' _ "$logs" {} "$wrap" "$t" <"$OUT/tests-$v.txt" | sort -k2 >"$res"
}

# Compare a results file with host-results.baseline: a sanitizer report or a
# test that does worse than the baseline fails.
check() {
	v=$1; res=$OUT/results-$v.txt; bad=0
	total=$(wc -l <"$res"); pass=$(grep -c '^PASS ' "$res"); san=$(grep -c ' SAN ' "$res")
	echo "$v: $total tests, $pass passed, $san with a sanitizer/valgrind report"
	if grep -q ' SAN ' "$res"; then
		grep ' SAN ' "$res" | sed 's/^/  report: /'
		bad=1
	fi
	while read -r outcome test _; do
		want=$(awk -v t="$test" '$2==t{print $1}' "$HERE/host-results.baseline" 2>/dev/null)
		case $outcome in
		PASS) ;;
		*) [ "${want%%(*}" = "${outcome%%(*}" ] || { echo "  regression: $test $outcome (baseline: ${want:-PASS})"; bad=1; } ;;
		esac
	done <"$res"
	return $bad
}

stage_runtime
status=0; summary=""
need_plain=0
for v in $VARIANTS; do [ "$v" = valgrind ] && need_plain=1; done
[ $UPDATE = 1 ] && need_plain=1
if [ $need_plain = 1 ]; then
	build plain "" || exit 1
	run_tests plain "" || exit 1
	if [ $UPDATE = 1 ]; then
		{ echo "# Outcome of each test on the x86-64 host with the recipe's flags."
		  echo "# Tests that need the device (web engine, plugin paths) fail or abort"
		  echo "# here; only these are allowed to. Regenerate with run-sanitizers.sh -u."
		  grep -v '^PASS ' "$OUT/results-plain.txt" | cut -d' ' -f1-2; } >"$HERE/host-results.baseline"
		echo "host-results.baseline written"; exit 0
	fi
fi
for v in $VARIANTS; do
	case $v in
	clang-asan) san=address,undefined; wrap="" ;;
	gcc-asan) san=address,undefined; wrap="" ;;
	clang-tsan) san=thread; wrap="" ;;
	valgrind)
		if ! have valgrind; then summary="$summary
valgrind: SKIPPED (valgrind not installed)"; status=2; continue; fi
		run_tests plain "valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --num-callers=40 --suppressions=$HERE/valgrind.supp"
		cp "$OUT/results-plain.txt" "$OUT/results-valgrind.txt"
		check valgrind || status=1
		continue ;;
	*) die "unknown variant $v" ;;
	esac
	if ! build "$v" "$san"; then summary="$summary
$v: SKIPPED (could not build; see $OUT)"; status=1; continue; fi
	run_tests "$v" "" || { status=1; continue; }
	check "$v" || status=1
done
[ -n "$summary" ] && echo "$summary"
echo "results and logs: $OUT"
exit $status
