#!/usr/bin/env bash
# Builds the bot and runs the offline unit tests. Usage:
#   ./build.sh                                  (uses ../cppgram as the library source)
#   CPPGRAM_SOURCE_DIR=/path/to/cppgram ./build.sh
#   CPPGRAM_PREBUILT_DIR=/path/with/libcppgram.a CPPGRAM_PREBUILT_INCLUDE_DIR=/path/include ./build.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="${BUILD_DIR:-$HERE/build}"
ARGS=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="${CXX:-g++}" -DCMAKE_C_COMPILER="${CC:-gcc}")
[[ -n "${CPPGRAM_SOURCE_DIR:-}" ]] && ARGS+=(-DCPPGRAM_SOURCE_DIR="$CPPGRAM_SOURCE_DIR")
[[ -n "${CPPGRAM_PREBUILT_DIR:-}" ]] && ARGS+=(-DCPPGRAM_PREBUILT_DIR="$CPPGRAM_PREBUILT_DIR")
[[ -n "${CPPGRAM_PREBUILT_INCLUDE_DIR:-}" ]] && ARGS+=(-DCPPGRAM_PREBUILT_INCLUDE_DIR="$CPPGRAM_PREBUILT_INCLUDE_DIR")
cmake -S "$HERE" -B "$BUILD" "${ARGS[@]}"
cmake --build "$BUILD" --parallel "$(nproc)"
ctest --test-dir "$BUILD" --output-on-failure
echo "built: $BUILD/cppgram_live_test_bot"
