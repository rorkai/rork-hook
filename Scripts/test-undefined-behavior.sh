#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRATCH_DIR="${TMPDIR:-/tmp}/rork-hook-undefined-behavior"

rm -rf "$SCRATCH_DIR"

UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
    swift test \
        --package-path "$ROOT_DIR" \
        --scratch-path "$SCRATCH_DIR" \
        --sanitize=undefined
