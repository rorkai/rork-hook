#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRATCH_DIR="${TMPDIR:-/tmp}/rork-hook-code-coverage"
MINIMUM_TOTAL_LINES=80
MINIMUM_FILE_LINES=65

rm -rf "$SCRATCH_DIR"

swift test \
    --package-path "$ROOT_DIR" \
    --enable-code-coverage \
    --scratch-path "$SCRATCH_DIR" \
    -Xcc -fprofile-instr-generate \
    -Xcc -fcoverage-mapping

TEST_BINARY="$(find "$SCRATCH_DIR" -type f -path '*.xctest/Contents/MacOS/*' -perm -111 -print -quit)"
PROFILE_DATA="$(find "$SCRATCH_DIR" -type f -name default.profdata -print -quit)"
if [[ -z "$TEST_BINARY" || -z "$PROFILE_DATA" ]]; then
    echo "Unable to locate the instrumented test binary or profile data." >&2
    exit 1
fi

COVERAGE_REPORT="$(
    xcrun llvm-cov report \
        "$TEST_BINARY" \
        -instr-profile="$PROFILE_DATA" \
        "$ROOT_DIR"/Sources/RorkHook/*.c
)"
printf '%s\n' "$COVERAGE_REPORT"

TOTAL_LINES="$(
    awk '/^TOTAL/ { value = $10; sub(/%$/, "", value); print value }' \
        <<<"$COVERAGE_REPORT"
)"
if ! awk -v actual="$TOTAL_LINES" -v minimum="$MINIMUM_TOTAL_LINES" \
    'BEGIN { exit(actual + 0 >= minimum + 0 ? 0 : 1) }'; then
    echo "Production C line coverage ${TOTAL_LINES}% is below ${MINIMUM_TOTAL_LINES}%." >&2
    exit 1
fi

LOW_COVERAGE_FILES="$(
    awk -v minimum="$MINIMUM_FILE_LINES" '
        /\.c$/ {
            value = $10
            sub(/%$/, "", value)
            if (value + 0 < minimum + 0) {
                print $1 " (" value "%)"
            }
        }
    ' <<<"$COVERAGE_REPORT"
)"
if [[ -n "$LOW_COVERAGE_FILES" ]]; then
    echo "Production C files below ${MINIMUM_FILE_LINES}% line coverage:" >&2
    printf '%s\n' "$LOW_COVERAGE_FILES" >&2
    exit 1
fi
