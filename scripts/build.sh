#!/usr/bin/env bash
# Guarded local CMake entrypoint. Direct cmake/IDE invocations remain caller-owned.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd -P)"
mode="${1:-build}"
case "$mode" in build|test) ;; *) echo 'Usage: scripts/build.sh [build|test]' >&2; exit 2 ;; esac
(( $# <= 1 )) || { echo 'Raw CMake overrides are outside wrapper guarantees.' >&2; exit 2; }
exports="$(python3 -B "$repo/scripts/build_output_paths.py" --task "${FLOATINGBALL_BUILD_TASK:-dev}" --resolve-only)"
eval "$exports"
binary="${FLOATINGBALL_BINARY_DIR:-$MYGIT_WORK_DIR/cmake.noindex}"
exports="$(python3 -B "$repo/scripts/build_output_paths.py" --task "${FLOATINGBALL_BUILD_TASK:-dev}" --app-output "$binary" --validate "$MYGIT_TESTS_DIR/results.xml" --validate "$MYGIT_TESTS_DIR/ctest.log")"
eval "$exports"
mkdir -p "$MYGIT_TESTS_DIR"
cmake -S "$repo" -B "$binary" -DFLOATINGBALL_BUILD_TESTS=ON
cmake --build "$binary"
case "$mode" in
  test) ctest --test-dir "$binary" --output-on-failure --output-junit "$MYGIT_TESTS_DIR/results.xml" --output-log "$MYGIT_TESTS_DIR/ctest.log" ;;
esac
