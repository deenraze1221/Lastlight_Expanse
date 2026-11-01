#!/usr/bin/env bash
# ZODAR test gate (Linux/macOS shells; docs/guide/RUNNING.md "Test gate").
#
# Builds Debug and Release with warnings as errors, runs every ctest test exactly once (no
# retries: an intermittent failure fails the gate), optionally compares the Highlands
# regression screenshot hash, and exits 0 only when everything passed. Every failure is kept
# in $ZODAR_GATE_BUILD_ROOT/gate-failures.log (appended, with date and commit) so intermittent
# failures can be counted instead of hidden.
#
# Environment:
#   ZODAR_GATE_BUILD_ROOT        build trees (default: <repo>/../zodar-gate-build; outside the repo)
#   ZODAR_GATE_JOBS              ctest parallelism (default 4)
#   ZODAR_GATE_HIGHLANDS_SHA256  expected sha256 of the Highlands screenshot (640x360, 90 frames)
#                                on this machine's driver; unset: the hash is printed, not compared
#   ZODAR_GATE_GPU               1 (default): -DZODAR_GPU_TESTS=ON; 0: CPU tests only
set -u
repo=$(cd "$(dirname "$0")/.." && pwd)
root=${ZODAR_GATE_BUILD_ROOT:-$(dirname "$repo")/zodar-gate-build}
jobs=${ZODAR_GATE_JOBS:-4}
gpu=${ZODAR_GATE_GPU:-1}
gpuFlag=$([ "$gpu" = 1 ] && echo ON || echo OFF)
commit=$(git -C "$repo" rev-parse --short HEAD 2>/dev/null || echo unknown)
mkdir -p "$root"
failures="$root/gate-failures.log"
status=0
fail() {
    echo "GATE FAIL: $1"
    echo "$(date -u +%FT%TZ) $commit $1" >> "$failures"
    status=1
}
export SDL_VIDEO_DRIVER=${SDL_VIDEO_DRIVER:-offscreen}
for config in Debug Release; do
    dir="$root/$(echo "$config" | tr '[:upper:]' '[:lower:]')"
    if ! cmake -S "$repo" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=$config -DZODAR_GPU_TESTS=$gpuFlag \
            -DZODAR_WARNINGS_AS_ERRORS=ON -DFETCHCONTENT_BASE_DIR="$dir/_deps-src" > "$dir.configure.log" 2>&1; then
        fail "$config configure (see $dir.configure.log)"
        continue
    fi
    if ! cmake --build "$dir" > "$dir.build.log" 2>&1; then
        fail "$config build (see $dir.build.log)"
        grep -E "error:|warning:" "$dir.build.log" | head -20
        continue
    fi
    if ! ctest --test-dir "$dir" -j "$jobs" --output-on-failure > "$dir.ctest.log" 2>&1; then
        fail "$config ctest: $(grep -E '\(Failed\)|\(Timeout\)|\(SEGFAULT\)|Not Run' "$dir.ctest.log" | tr -s ' \t' ' ' | paste -sd ';' -)"
    fi
    grep -E "tests passed" "$dir.ctest.log" | sed "s/^/$config: /"
    if [ "$gpu" = 1 ]; then
        shot="$dir/highlands.png"
        "$dir/bin/LastlightExpanse" --headless --deterministic --frames 90 --width 640 --height 360 \
            --level highlands --screenshot "$shot" --report "$dir/highlands.json" > "$dir.highlands.log" 2>&1 ||
            fail "$config Highlands run (see $dir.highlands.log)"
        grep -q '"validation": {"errors": 0, "warnings": 0}' "$dir/highlands.json" 2>/dev/null ||
            fail "$config Highlands validation messages (see $dir/highlands.json)"
        hash=$(sha256sum "$shot" 2>/dev/null | cut -d' ' -f1)
        echo "$config: Highlands sha256 $hash"
        if [ -n "${ZODAR_GATE_HIGHLANDS_SHA256:-}" ] && [ "$hash" != "$ZODAR_GATE_HIGHLANDS_SHA256" ]; then
            fail "$config Highlands screenshot $hash != expected $ZODAR_GATE_HIGHLANDS_SHA256"
        fi
    fi
done
[ $status = 0 ] && echo "GATE OK ($commit)" || echo "GATE FAILED ($commit; history: $failures)"
exit $status
