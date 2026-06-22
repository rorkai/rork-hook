#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if command -v mise >/dev/null 2>&1; then
    tuist_command=(mise exec -- tuist)
elif command -v tuist >/dev/null 2>&1; then
    tuist_command=(tuist)
else
    cat >&2 <<'ERROR'
Tuist is required to generate the physical-device test harness.
Install the version pinned in .mise.toml with:
  mise install
ERROR
    exit 69
fi

"${tuist_command[@]}" generate \
    --path "$ROOT_DIR/DeviceTests" \
    --no-open
