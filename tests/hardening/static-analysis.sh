#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# static-analysis.sh
#
# Static-analysis gate for WebAppMgr. Runs, over every C++ file of the tree
# that the build compiles:
#
#   gcc -fanalyzer    use after free, double free, leaks, NULL dereference,
#                     uninitialised reads; at -O0 and at -O2
#   clang analyzer    the same from a second engine, with the alpha checkers
#                     for unix/security/core
#   clang-tidy        clang-analyzer-*, bugprone-* (use after move among
#                     them), cert-*, concurrency-*, misc-*, performance-*,
#                     portability-*, cppcoreguidelines-owning-memory
#   cppcheck          all checks, exhaustive, inconclusive
#   flawfinder        race-prone calls (TOCTOU), unsafe buffer functions;
#                     level 1 and up
#   coccinelle        the rules in cocci/ (spatch --c++)
#   sparse            C files only; sparse cannot parse C++
#
# The compiler flags come from the compile_commands.json of a build of this
# tree, normally the Yocto workdir's: -p <workdir>/build. That database names
# the workdir's copy of the sources; it is rewritten to point at this tree.
#
# Findings are compared with static-analysis.baseline: the gate fails on a
# finding that is not in the baseline (line numbers are ignored, so moving
# code does not trip it). A tool that is not installed is reported as
# SKIPPED, never as passed, and makes the exit status 2 unless
# SA_ALLOW_SKIP=1. A file a tool could not analyse (compile error, timeout,
# memory cap) counts as a finding of its own and fails the gate unless the
# baseline lists it.
#
# Usage: static-analysis.sh -p BUILD_DIR [-o OUT_DIR] [-b BASELINE] [-u] [-r]
#                           [-j JOBS] [FILE...]
#        static-analysis.sh --selftest
#   -u     write the current findings to the baseline instead of comparing
#   -r     resume: keep the results of an earlier run in OUT_DIR and only
#          analyse what it did not finish (the -fanalyzer runs take hours)
#   FILE   limit the run to these files (paths relative to the tree)
#
# Environment: GXX (default g++), CLANG (default: the build's compiler),
# CLANG_TIDY, CPPCHECK, FLAWFINDER, SPATCH, SPARSE, SA_TIMEOUT (seconds per
# file and tool, default 1800), SA_ANALYZER_MEM (memory cap of one
# -fanalyzer run, default 20G; a file that hits it is reported, not
# retried), SA_SKIP (tools to leave out, e.g. "gcc-O0 gcc-O2"; reported as
# SKIPPED), SA_MIN_FREE_GB (wait before each file until this much memory
# is available, default 30).
#
# -fanalyzer runs one file at a time: on these files it takes 10-18 GB.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SELF="$HERE/$(basename "$0")"

GXX=${GXX:-g++}
CLANG_TIDY=${CLANG_TIDY:-clang-tidy}
CPPCHECK=${CPPCHECK:-cppcheck}
FLAWFINDER=${FLAWFINDER:-flawfinder}
SPATCH=${SPATCH:-spatch}
SPARSE=${SPARSE:-sparse}
SA_TIMEOUT=${SA_TIMEOUT:-1800}
SA_ANALYZER_MEM=${SA_ANALYZER_MEM:-20G}
SA_MIN_FREE_GB=${SA_MIN_FREE_GB:-30}

ANALYZER_CHECKERS=core,unix,deadcode,security,nullability,optin.portability,optin.core,alpha.unix,alpha.security,alpha.core
# The IR gate's list; the exclusions are style or false alarms for this code:
# swappable parameters and unused parameters (signatures are Chromium's and
# LS2's), include-cleaner, reserved identifiers in macros, Annex K advice.
TIDY_CHECKS='clang-analyzer-*,bugprone-*,cert-*,concurrency-*,misc-*,performance-*,portability-*,readability-misleading-indentation,readability-suspicious-call-argument,cppcoreguidelines-owning-memory,bugprone-use-after-move,-bugprone-easily-swappable-parameters,-misc-include-cleaner,-misc-unused-parameters,-cert-err33-c,-bugprone-assignment-in-if-condition,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-bugprone-reserved-identifier,-cert-dcl37-c,-cert-dcl51-cpp'

die() { echo "static-analysis.sh: $*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# Print the compile command of $2 from database $1 as shell words, adapted
# for $3: "clang" keeps it, "gcc" drops the clang-only flags. Output and
# dependency-file arguments are always dropped.
db_args() {
	python3 - "$1" "$2" "$3" <<'EOF'
import json, shlex, sys
db, src, flavour = sys.argv[1:4]
for e in json.load(open(db)):
    if e["file"] != src:
        continue
    a = e.get("arguments") or shlex.split(e["command"])
    out, skip = [], False
    for x in a[1:]:
        if skip:
            skip = False
            continue
        if x in ("-o", "-MT", "-MF", "-MQ"):
            skip = True
            continue
        if x in ("-c", "-MD", "-MMD", "-pipe", src) or x.startswith(("-ffile-prefix-map=", "-fdebug-prefix-map=", "-fmacro-prefix-map=", "-Wl,")):
            continue
        if flavour == "gcc" and (x.startswith(("--target=", "--dyld-prefix=", "-stdlib=", "-fuse-ld=")) or x == "-Wno-unused-command-line-argument"):
            continue
        out.append(x)
    print(" ".join(shlex.quote(x) for x in out))
    break
EOF
}

# --tu TOOL FILE: analyse one file with one tool (run in parallel by xargs).
# Raw output goes to $OUT/raw/<tool>/<file>.log, failures to errors.
if [ "${1:-}" = "--tu" ]; then
	tool=$2; src=$3
	log="$OUT/raw/$tool/$(echo "$src" | sed "s|^$ROOT/||; s|/|_|g").log"
	mkdir -p "$(dirname "$log")"
	# -r: a file finished by an earlier, interrupted run is not redone
	[ "${SA_RESUME:-0}" = 1 ] && [ -e "$log" ] && exit 0
	final=$log; log=$log.part; rm -f "$final.err"
	db="$OUT/compile_commands.json"
	# Wait for memory rather than push the machine into swap
	while [ "$(awk '/^MemAvailable/{print int($2/1048576)}' /proc/meminfo)" -lt "$SA_MIN_FREE_GB" ]; do
		sleep 30
	done
	case $tool in
	gcc-O0|gcc-O2)
		eval "set -- $(db_args "$db" "$src" gcc)"
		# -fanalyzer takes 10-18 GB on a file with the web engine's
		# headers: one at a time (see per_tu), under a hard memory cap
		if have systemd-run && systemd-run --user --scope --quiet true 2>/dev/null; then
			set -- systemd-run --user --scope --quiet -p MemoryMax="$SA_ANALYZER_MEM" \
				-p MemorySwapMax=0 timeout "$SA_TIMEOUT" "$GXX" "$@"
		else
			ulimit -v $(($(echo "$SA_ANALYZER_MEM" | tr -d G) * 1048576))
			set -- timeout "$SA_TIMEOUT" "$GXX" "$@"
		fi
		"$@" -Wno-template-body -${tool#gcc-} -fanalyzer \
			-Wanalyzer-too-complex --param=analyzer-bb-explosion-factor=50 \
			--param=analyzer-max-enodes-per-program-point=64 -c -o /dev/null "$src" >"$log" 2>&1 ;;
	clang-analyzer)
		eval "set -- $(db_args "$db" "$src" clang)"
		timeout "$SA_TIMEOUT" "$CLANG" "$@" --analyze -Xanalyzer -analyzer-output=text \
			-Xanalyzer -analyzer-checker=$ANALYZER_CHECKERS -o /dev/null "$src" >"$log" 2>&1 ;;
	clang-tidy)
		timeout "$SA_TIMEOUT" "$CLANG_TIDY" --quiet -p "$OUT" --checks="$TIDY_CHECKS" \
			--header-filter="^$ROOT/src/" --extra-arg=-Wno-unknown-warning-option \
			"$src" >"$log" 2>&1 ;;
	cppcheck)
		# Our headers only: Chromium, glib and libc++ are not ours to lint
		incs=$(find "$ROOT/src" -name '*.h' -exec dirname {} \; | sort -u | sed 's/^/-I/')
		# shellcheck disable=SC2086
		timeout "$SA_TIMEOUT" "$CPPCHECK" --enable=all --check-level=exhaustive --inconclusive \
			--language=c++ --std=c++20 --library=gnu --library=posix --force --inline-suppr --quiet \
			--suppress=missingIncludeSystem --suppress=missingInclude --suppress=unmatchedSuppression \
			--suppress=checkersReport --suppress=unusedFunction \
			--template='{file}:{line}: {severity}: {message} [{id}]' \
			-D__linux__ -D__GNUC__ $incs "$src" >"$log" 2>&1 ;;
	*) die "unknown tool $tool" ;;
	esac
	status=$?
	# The scope is stopped (SIGTERM, 143) or the compiler killed (137)
	# when it reaches the cap
	if [ "${tool#gcc-}" != "$tool" ] && { [ $status -eq 137 ] || [ $status -eq 143 ] ||
		grep -q -E 'Killed signal|out of memory|virtual memory exhausted|std::bad_alloc' "$log"; }; then
		echo "$tool	$src	analyzer ran out of memory (cap $SA_ANALYZER_MEM)" >"$final.err"
	elif [ $status -eq 124 ]; then
		echo "$tool	$src	timed out after ${SA_TIMEOUT}s" >"$final.err"
	elif [ $status -ne 0 ] && grep -q -E '(^|: )(fatal )?error:' "$log"; then
		echo "$tool	$src	could not analyse: $(grep -m1 -E 'error:' "$log" | cut -c1-200)" >"$final.err"
	elif [ $status -gt 128 ]; then
		echo "$tool	$src	killed by signal $((status - 128))" >"$final.err"
	fi
	mv "$log" "$final"
	exit 0
fi

# Normalise raw logs to: tool<TAB>file<TAB>line<TAB>check<TAB>message, for
# files of this tree only.
normalise() {
	python3 - "$ROOT" "$OUT" <<'EOF'
import os, re, sys
root, out = sys.argv[1], sys.argv[2]
rows = set()
diag = re.compile(r"^(?P<f>[^:\s][^:]*):(?P<l>\d+):(?:\d+:)? (?:fatal )?(?P<sev>warning|error|style|performance|portability|information|inconclusive): (?P<m>.*?)(?: \[(?P<c>[^\]]+)\])?$")
flaw = re.compile(r"^(?P<f>[^:]+):(?P<l>\d+):(?:\d+:)?\s+\[(?P<lvl>\d)\] \((?P<cat>[^)]+)\) (?P<fn>\w+):(?P<m>.*)$")
coc = re.compile(r"^(?P<f>[^:]+):(?P<l>\d+):[\d-]+: (?P<c>[a-z-]+): (?P<m>.*)$")
def rel(f):
    p = os.path.normpath(f if os.path.isabs(f) else os.path.join(root, f))
    return os.path.relpath(p, root) if p.startswith(root + "/") else None
for dirpath, _, files in os.walk(os.path.join(out, "raw")):
    tool = os.path.relpath(dirpath, os.path.join(out, "raw")).split(os.sep)[0]
    for name in files:
        if not name.endswith(".log"):
            continue
        for line in open(os.path.join(dirpath, name), errors="replace"):
            line = line.rstrip("\n")
            if tool == "flawfinder":
                m = flaw.match(line)
                if m and rel(m["f"]):
                    rows.add((tool, rel(m["f"]), m["l"], "level%s/%s" % (m["lvl"], m["fn"]), m["m"].strip()))
                continue
            if tool in ("coccinelle", "sparse"):
                m = coc.match(line) if tool == "coccinelle" else diag.match(line)
                if m and rel(m["f"]):
                    rows.add((tool, rel(m["f"]), m["l"], m["c"] if tool == "coccinelle" else "sparse", m["m"]))
                continue
            m = diag.match(line)
            if not m or not rel(m["f"]):
                continue
            check = m["c"] or ""
            # Compiler warnings belong to the warning builds, not here
            if tool.startswith("gcc") and not check.startswith("-Wanalyzer"):
                continue
            if tool == "clang-analyzer" and (not check or check.startswith("-W")):
                continue
            if tool == "clang-tidy" and (not check or check.startswith("clang-diagnostic")):
                continue
            if tool == "cppcheck":
                check = "%s/%s" % (m["sev"], check)
            if m["sev"] == "error" and tool != "cppcheck" and not check:
                continue
            rows.add((tool, rel(m["f"]), m["l"], check, m["m"]))
# A file a tool could not analyse (memory cap, timeout, compile error) is a
# finding too: it fails the gate unless the baseline already lists it
errors = os.path.join(out, "errors.tsv")
if os.path.exists(errors):
    for line in open(errors):
        parts = line.rstrip("\n").split("\t")
        if len(parts) == 3 and rel(parts[1]):
            rows.add((parts[0], rel(parts[1]), "0", "analysis-error", parts[2]))
with open(os.path.join(out, "findings.tsv"), "w") as f:
    # Long messages (analyzer call strings, padding advice) are cut: the
    # tail changes with unrelated code and would defeat the baseline
    for r in sorted({r[:4] + (r[4].replace("\t", " ")[:160],) for r in rows}):
        f.write("\t".join(r) + "\n")
EOF
}

# Compare findings with the baseline; print the summary and the new findings.
compare() {
	python3 - "$OUT" "$BASELINE" "$UPDATE" "$TOOLS_RUN" "$SKIPPED" <<'EOF'
import collections, os, re, sys
out, baseline, update, tools_run, skipped = sys.argv[1:6]
def key(r):
    # Line numbers and counts inside messages move with the code
    return "\t".join((r[0], r[1], r[3], re.sub(r"\d+", "N", r[4])))
rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(out, "findings.tsv")) if l.strip()]
now = collections.Counter(key(r) for r in rows)
if update == "1":
    with open(baseline, "w") as f:
        f.write("# static-analysis.sh baseline: tool, file, check, message (digits as N).\n")
        f.write("# Regenerate with static-analysis.sh -u after triaging every new entry.\n")
        for k in sorted(now.elements()):
            f.write(k + "\n")
    print("baseline written: %d findings" % sum(now.values()))
    sys.exit(0)
base = collections.Counter()
if os.path.exists(baseline):
    base.update(l.rstrip("\n") for l in open(baseline) if l.strip() and not l.startswith("#"))
new = now - base
fixed = base - now
per = collections.defaultdict(lambda: [0, 0])
for r in rows:
    per[r[0]][0] += 1
left = collections.Counter(new)
newrows = []
for r in rows:
    k = key(r)
    if left[k] > 0:
        left[k] -= 1
        per[r[0]][1] += 1
        newrows.append(r)
print("%-16s %8s %8s  %s" % ("tool", "findings", "new", "status"))
for t in tools_run.split():
    n, nn = per[t]
    print("%-16s %8d %8d  %s" % (t, n, nn, "NEW FINDINGS" if nn else "ok"))
for t in skipped.split():
    name, reason = t.split("=", 1)
    print("%-16s %8s %8s  SKIPPED (%s)" % (name, "-", "-", reason.replace("_", " ")))
if newrows:
    print("\nnew findings:")
    for r in newrows:
        print("  %s:%s: [%s] %s: %s" % (r[1], r[2], r[0], r[3], r[4]))
if fixed:
    print("\n%d baseline entries no longer found (refresh with -u)" % sum(fixed.values()))
sys.exit(1 if newrows else 0)
EOF
}

selftest() {
	have "$SPATCH" || { echo "spatch not installed: selftest SKIPPED"; exit 2; }
	fail=0
	for rule in "$HERE"/cocci/*.cocci; do
		name=$(basename "$rule" .cocci)
		want=$(grep -n "expect: $name\$" "$HERE/cocci/selftest.cc" | cut -d: -f1 | sort -n | tr '\n' ' ')
		got=$("$SPATCH" --c++=20 -D report --very-quiet --no-show-diff --sp-file "$rule" \
			"$HERE/cocci/selftest.cc" </dev/null 2>&1 | sed -n "s/^[^:]*:\([0-9]*\):.*$name:.*/\1/p" | sort -n | tr '\n' ' ')
		if [ "$want" = "$got" ]; then echo "ok   $name"; else echo "FAIL $name: want lines $want, got $got"; fail=1; fi
	done
	exit $fail
}

BUILD=""; OUT=""; BASELINE="$HERE/static-analysis.baseline"; UPDATE=0; # One job per 4 GB available: a TU with the web engine headers takes 2-7 GB
JOBS=$(awk -v n="$(nproc)" '/^MemAvailable/{j=int($2/4194304); if(j<1)j=1; if(j>n)j=n; if(j>8)j=8; print j}' /proc/meminfo)
[ "${1:-}" = "--selftest" ] && selftest
while getopts p:o:b:urj: opt; do
	case $opt in
	p) BUILD=$OPTARG ;; o) OUT=$OPTARG ;; b) BASELINE=$OPTARG ;;
	u) UPDATE=1 ;; r) SA_RESUME=1 ;; j) JOBS=$OPTARG ;; *) die "bad option" ;;
	esac
done
shift $((OPTIND - 1))
[ -n "$BUILD" ] || die "-p BUILD_DIR (with compile_commands.json) is required"
BUILD=$(cd "$BUILD" && pwd)
[ -f "$BUILD/compile_commands.json" ] || die "no compile_commands.json in $BUILD"
OUT=${OUT:-$ROOT/build-static-analysis}
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
[ "${SA_RESUME:-0}" = 1 ] || rm -rf "$OUT/raw"
mkdir -p "$OUT/raw"; rm -f "$OUT/errors.tsv"
export SA_RESUME=${SA_RESUME:-0} SA_ANALYZER_MEM SA_MIN_FREE_GB OUT ROOT GXX CLANG_TIDY CPPCHECK SA_TIMEOUT ANALYZER_CHECKERS TIDY_CHECKS

# A Yocto build dir calls the compiler by name from recipe-sysroot-native
WORKDIR=$(dirname "$BUILD")
[ -d "$WORKDIR/recipe-sysroot-native/usr/bin" ] && PATH="$WORKDIR/recipe-sysroot-native/usr/bin:$PATH" && export PATH

# Point the database at this tree instead of the one it was configured from
DBSRC=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD/CMakeCache.txt" 2>/dev/null)
[ -n "$DBSRC" ] || die "cannot read CMAKE_HOME_DIRECTORY from $BUILD/CMakeCache.txt"
python3 - "$BUILD/compile_commands.json" "$DBSRC" "$ROOT" "$OUT/compile_commands.json" <<'EOF'
import json, sys
src, old, new, dst = sys.argv[1:5]
db = json.load(open(src))
s = json.dumps(db).replace(old.rstrip("/") + "/", new + "/")
json.dump(json.loads(s), open(dst, "w"), indent=1)
EOF

if [ $# -gt 0 ]; then
	FILES=$(for f in "$@"; do echo "$ROOT/${f#"$ROOT"/}"; done)
else
	FILES=$(python3 -c 'import json,os,sys; [print(e["file"]) for e in json.load(open(sys.argv[1])) if e["file"].startswith(sys.argv[2] + "/") and os.path.exists(e["file"])]' \
		"$OUT/compile_commands.json" "$ROOT" | sort -u)
fi
TUS=$(python3 -c 'import json,os,sys; db={e["file"] for e in json.load(open(sys.argv[1]))}; [print(f) for f in sys.argv[2:] if f in db and os.path.exists(f)]' \
	"$OUT/compile_commands.json" $FILES)
CFILES=$(printf '%s\n' $TUS | grep '\.c$' || true)
CLANG=${CLANG:-$(python3 -c 'import json,shlex,sys; e=json.load(open(sys.argv[1]))[0]; print((e.get("arguments") or shlex.split(e["command"]))[0])' "$OUT/compile_commands.json")}
export CLANG
# Headers of the tree are analysed through the files that include them;
# flawfinder and coccinelle read them directly.
SRCFILES=$( (printf '%s\n' $FILES; [ $# -eq 0 ] && find "$ROOT/src" -name '*.h') | sort -u)

TOOLS_RUN=""; SKIPPED=""
skip() { SKIPPED="$SKIPPED $1=$(echo "$2" | tr ' ' '_')"; echo "== $1: SKIPPED ($2)"; }
# SA_SKIP="gcc-O0 gcc-O2" leaves tools out on purpose; they are reported as
# SKIPPED like a missing one
wanted() {
	case " ${SA_SKIP:-} " in *" $1 "*) skip "$1" "left out by SA_SKIP"; return 1 ;; esac
}
per_tu() {
	wanted "$1" || return 0
	echo "== $1"
	jobs=$JOBS
	case $1 in gcc-*) jobs=1 ;; esac
	printf '%s\n' $TUS | grep -v '\.c$' | xargs -r -P "$jobs" -I{} "$SELF" --tu "$1" {}
	TOOLS_RUN="$TOOLS_RUN $1"
}

if have "$CLANG"; then per_tu clang-analyzer; else skip clang-analyzer "$CLANG not found"; fi
if have "$CLANG_TIDY"; then per_tu clang-tidy; else skip clang-tidy "$CLANG_TIDY not installed"; fi
if have "$CPPCHECK"; then per_tu cppcheck; else skip cppcheck "$CPPCHECK not installed"; fi
if ! wanted flawfinder; then :
elif have "$FLAWFINDER"; then
	echo "== flawfinder"; mkdir -p "$OUT/raw/flawfinder"
	# shellcheck disable=SC2086
	"$FLAWFINDER" --minlevel=1 --falsepositive --columns --dataonly --quiet $SRCFILES >"$OUT/raw/flawfinder/all.log" 2>&1
	TOOLS_RUN="$TOOLS_RUN flawfinder"
else
	skip flawfinder "$FLAWFINDER not installed"
fi
if ! wanted coccinelle; then :
elif have "$SPATCH"; then
	echo "== coccinelle"; mkdir -p "$OUT/raw/coccinelle"
	for rule in "$HERE"/cocci/*.cocci; do
		# shellcheck disable=SC2086
		log="$OUT/raw/coccinelle/$(basename "$rule").log"
		# Plain --c++ stops at the first "auto"; WAM is C++20
		"$SPATCH" --c++=20 -D report --very-quiet --no-show-diff --no-includes -j "$JOBS" \
			--sp-file "$rule" $SRCFILES </dev/null >"$log" 2>&1
		status=$?
		if [ $status -ne 0 ] || grep -q -E '^exception|Aborting computation' "$log"; then
			echo "coccinelle	$(basename "$rule")	could not analyse: $(grep -m1 -E 'exception|rror' "$log" | cut -c1-200)" >"$log.err"
		else
			rm -f "$log.err"
		fi
	done
	TOOLS_RUN="$TOOLS_RUN coccinelle"
else
	skip coccinelle "$SPATCH not installed"
fi
# Last: by far the slowest, one file at a time
if have "$GXX" && "$GXX" --help=common 2>/dev/null | grep -q -- -fanalyzer; then
	per_tu gcc-O0; per_tu gcc-O2
else
	skip gcc-O0 "$GXX with -fanalyzer not installed"; skip gcc-O2 "$GXX with -fanalyzer not installed"
fi
if [ -z "$CFILES" ]; then
	skip sparse "no C files; sparse cannot parse C++"
elif have "$SPARSE"; then
	echo "== sparse"; mkdir -p "$OUT/raw/sparse"
	for f in $CFILES; do
		eval "set -- $(db_args "$OUT/compile_commands.json" "$f" gcc)"
		"$SPARSE" -Wsparse-all "$@" "$f" >>"$OUT/raw/sparse/all.log" 2>&1
	done
	TOOLS_RUN="$TOOLS_RUN sparse"
else
	skip sparse "$SPARSE not installed"
fi

cat "$OUT"/raw/*/*.err >"$OUT/errors.tsv" 2>/dev/null
normalise
echo
compare; status=$?
if [ -s "$OUT/errors.tsv" ]; then
	echo; echo "files a tool could not analyse (they fail the gate unless the baseline lists them):"
	sed "s|$ROOT/||; s/^/  /" "$OUT/errors.tsv"
fi
echo; echo "raw output and findings.tsv: $OUT"
[ $status -eq 0 ] && [ -n "$SKIPPED" ] && [ "${SA_ALLOW_SKIP:-0}" != 1 ] && status=2
exit $status
