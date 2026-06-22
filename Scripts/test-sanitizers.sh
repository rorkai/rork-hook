#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRATCH_ROOT="${TMPDIR:-/tmp}/rork-hook-sanitizers"

for sanitizer in address thread; do
    scratch_dir="$SCRATCH_ROOT/$sanitizer"
    rm -rf "$scratch_dir"
    swift test \
        --package-path "$ROOT_DIR" \
        --scratch-path "$scratch_dir" \
        --sanitize="$sanitizer"
done
