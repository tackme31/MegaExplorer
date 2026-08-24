#!/usr/bin/env bash
# Single machine-verification entry point for the /evolve loop: close our exes,
# (re)configure if needed, build every target in the preset, fail on any /W4
# warning of ours, then run ctest. Terse on success -- the loop pays for every
# line of this output in context, so a clean run is a handful of lines and only
# failures print detail.
#
# Qt Creator and the qtcreator MCP are intentionally not used: the loop has to
# run with no IDE resident (CLAUDE.md, "Loop engineering").
#
# Usage: loop_verify.sh [--full] [--reconfigure] [--no-tests] [--keep-app]
#   --full         wipe our targets' object dirs first, so moc/qmlcachegen/type
#                  registration are recompiled -- their warnings never appear in
#                  an incremental build
#   --reconfigure  force the configure step (also implied by a missing cache or
#                  by an uncommitted QML_FILES change)
#   --no-tests     build and warning-gate only
#   --keep-app     don't kill a running MegaExplorer.exe / megatool.exe (will
#                  likely fail at link time with LNK1104; only useful when
#                  debugging by hand)
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1

# The cmake on PATH is Strawberry Perl's 3.29: too old for this MSVC, and it
# overwrites CMakeCache.txt before failing. Always the full path.
CMAKE=${LOOP_VERIFY_CMAKE:-C:/Qt/Tools/CMake_64/bin/cmake.exe}
CTEST=${LOOP_VERIFY_CTEST:-C:/Qt/Tools/CMake_64/bin/ctest.exe}
BUILD_DIR=build/msvc-debug
LOG=$BUILD_DIR/loop_verify.log

full=0
reconfigure=0
run_tests=1
keep_app=0
for arg in "$@"; do
    case "$arg" in
        --full) full=1; reconfigure=1 ;;
        --reconfigure) reconfigure=1 ;;
        --no-tests) run_tests=0 ;;
        --keep-app) keep_app=1 ;;
        *) echo "loop_verify: unknown option $arg" >&2; exit 2 ;;
    esac
done

fail() { echo "[FAIL] $*"; exit 1; }
step() { printf '[ok] %-12s %ss\n' "$1" "$2"; }

# ------------------------------------------------------------- 1. close ours
# A running MegaExplorer.exe or megatool.exe holds its own .exe open and the
# link dies with LNK1104, which would stall an unattended cycle.
# No `tasklist | grep`: under `set -o pipefail` a SIGPIPE'd tasklist makes the
# pipeline non-zero even when grep matched, which would read as "not running".
# MSYS grep also aborts on a here-string, so the match is done in bash itself.
running() {
    local out
    out=$(tasklist //FI "IMAGENAME eq $1" 2>/dev/null) || return 1
    [[ ${out,,} == *"${1,,}"* ]]
}

close_exe() {
    local name=$1
    running "$name" || return 0
    taskkill //IM "$name" //F >/dev/null 2>&1
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        running "$name" || break
        sleep 1
    done
    if running "$name"; then
        fail "$name is still running and could not be killed; the link would hit LNK1104. Report this and stop the cycle."
    fi
    echo "[ok] closed running $name"
}

if [ "$keep_app" -eq 0 ]; then
    close_exe MegaExplorer.exe
    close_exe megatool.exe
fi

# --------------------------------------------------------------- 2. full reset
# Only our own targets: wiping the whole tree drags the SDK in and costs tens of
# minutes. Removing the autogen dirs is the point -- that is what forces moc and
# the QML type registration to be regenerated and re-warned about.
if [ "$full" -eq 1 ]; then
    rm -rf \
        "$BUILD_DIR"/MegaExplorerCore.dir \
        "$BUILD_DIR"/MegaExplorerQml*.dir \
        "$BUILD_DIR"/MegaExplorer.dir \
        "$BUILD_DIR"/megatool.dir \
        "$BUILD_DIR"/tests/*.dir \
        "$BUILD_DIR"/CMakeFiles/*MegaExplorer*_autogen.dir
    echo "[ok] wiped our object dirs (--full)"
fi

# -------------------------------------------------------------- 3. (re)configure
# An incremental build leaves the AOT qmlcache_loader.cpp aggregator stale when
# QML_FILES changed, and the link then fails on QmlCacheGeneratedCode symbols.
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    reconfigure=1
    echo "[..] no CMakeCache.txt -- configuring"
elif [ "$reconfigure" -eq 0 ] &&
     git diff HEAD -- CMakeLists.txt 2>/dev/null | grep -qE '^[+-].*\.qml'; then
    reconfigure=1
    echo "[..] uncommitted QML_FILES change -- configuring"
fi

if [ "$reconfigure" -eq 1 ]; then
    SECONDS=0
    if ! "$CMAKE" --preset msvc-debug >"$LOG" 2>&1; then
        echo "--- configure output (tail) ---"
        tail -n 30 "$LOG"
        fail "configure"
    fi
    step configure "$SECONDS"
fi

# -------------------------------------------------------------------- 4. build
SECONDS=0
"$CMAKE" --build --preset msvc-debug >"$LOG" 2>&1
build_status=$?
build_time=$SECONDS

# Ours only: third_party is not built at /W4 and its warnings are not actionable
# here. Sorted unique because MSBuild repeats each warning in its end summary.
warnings=$(grep -E 'warning [A-Z]+[0-9]+' "$LOG" | grep -v 'third_party' | sed 's/^ *//' | sort -u)
linker=$(grep -oE 'LNK[0-9]{4}[^]]*' "$LOG" | sort -u)

if [ "$build_status" -ne 0 ]; then
    echo "--- build errors ---"
    grep -E 'error [A-Z]+[0-9]+|LNK[0-9]{4}' "$LOG" | grep -v 'third_party' | sed 's/^ *//' | sort -u | head -n 40
    if printf '%s' "$linker" | grep -q 'LNK1104'; then
        echo "--- LNK1104: the output file was locked. Is MegaExplorer.exe or megatool.exe running?"
        echo "    (loop_verify closes them automatically unless --keep-app was passed)"
    fi
    fail "build (${build_time}s)"
fi
step build "$build_time"

# The hard gate (docs: /W4 was measured at zero warnings on a forced full
# recompile, so any warning here is one this cycle introduced).
if [ -n "$warnings" ]; then
    echo "--- new /W4 warnings (must be zero) ---"
    printf '%s\n' "$warnings" | head -n 40
    fail "warning gate: $(printf '%s\n' "$warnings" | wc -l) warning(s)"
fi
echo "[ok] warnings     0"

# -------------------------------------------------------------------- 5. tests
if [ "$run_tests" -eq 1 ]; then
    SECONDS=0
    "$CTEST" --preset msvc-debug >"$LOG" 2>&1
    test_status=$?
    if [ "$test_status" -ne 0 ]; then
        echo "--- failing tests ---"
        grep -E '^\s*[0-9]+ - .*(Failed|Timeout)|tests failed out of|FAIL!' "$LOG" | head -n 40
        fail "ctest ($SECONDS s)"
    fi
    step ctest "$SECONDS"
    grep -E 'tests passed' "$LOG" | tail -n 1
fi

echo "[PASS] loop_verify"
