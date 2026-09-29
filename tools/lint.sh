#!/usr/bin/env bash
# Lint: clang-format check + clang-tidy over our sources.
#
#   tools/lint.sh [build-dir] [--fix-format]
#
# build-dir must contain compile_commands.json (configure with CMake first;
# the default is build/engine, which does not need JUCE). Plugin sources are
# only tidied when the build dir includes the plugin.
set -euo pipefail

cd "$(dirname "$0")/.."
BUILD_DIR="${1:-build/engine}"
CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
CLANG_TIDY="${CLANG_TIDY:-clang-tidy}"

FORMAT_FILES=(dsp/include/lili/*.h dsp/src/*.cpp plugin/*.h plugin/*.cpp tests/*.cpp)
TIDY_FILES=(dsp/src/Engine.cpp tests/test_engine.cpp tests/render.cpp)
if grep -q 'PluginProcessor.cpp' "$BUILD_DIR/compile_commands.json"; then
    TIDY_FILES+=(plugin/PluginProcessor.cpp)
fi

if [[ "${2:-}" == "--fix-format" ]]; then
    "$CLANG_FORMAT" -i "${FORMAT_FILES[@]}"
else
    echo "== clang-format"
    "$CLANG_FORMAT" --dry-run --Werror "${FORMAT_FILES[@]}"
fi

echo "== clang-tidy"
EXTRA=()
if [[ "$(uname)" == "Darwin" ]]; then
    EXTRA+=("--extra-arg=-isysroot$(xcrun --show-sdk-path)")
fi
"$CLANG_TIDY" -p "$BUILD_DIR" --quiet "${EXTRA[@]}" "${TIDY_FILES[@]}"
echo "lint ok"
