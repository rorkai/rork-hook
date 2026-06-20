#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_PATH="$ROOT_DIR/DeviceTests/RorkHookDeviceTests.xcodeproj"
SCHEME="RorkHookDeviceTests"
DEVICE_UDID="${RORK_HOOK_DEVICE_UDID:-}"
DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM:-}"
TEMPORARY_DIRECTORY="${TMPDIR:-/tmp}"
TEMPORARY_DIRECTORY="${TEMPORARY_DIRECTORY%/}"
SCRATCH_DIR="${RORK_HOOK_DEVICE_TESTS_SCRATCH_DIR:-$TEMPORARY_DIRECTORY/rork-hook-device-tests}"

if [[ -z "$DEVICE_UDID" || -z "$DEVELOPMENT_TEAM" ]]; then
    cat >&2 <<'USAGE'
Usage:
  RORK_HOOK_DEVICE_UDID=<device-udid> \
  DEVELOPMENT_TEAM=<team-id> \
  Scripts/test-device.sh
USAGE
    exit 64
fi

team_component="$(
    printf '%s' "$DEVELOPMENT_TEAM" |
        tr '[:upper:]' '[:lower:]' |
        tr -cd '[:alnum:]'
)"
bundle_id_prefix="${RORK_HOOK_DEVICE_BUNDLE_ID_PREFIX:-dev.rorkhook.device-tests.team${team_component}}"
derived_data="$SCRATCH_DIR/DerivedData"
result_bundle="$SCRATCH_DIR/Result.xcresult"

rm -rf "$derived_data" "$result_bundle"
mkdir -p "$SCRATCH_DIR"

xcodebuild test \
    -project "$PROJECT_PATH" \
    -scheme "$SCHEME" \
    -destination "id=$DEVICE_UDID" \
    -destination-timeout 60 \
    -derivedDataPath "$derived_data" \
    -resultBundlePath "$result_bundle" \
    -allowProvisioningUpdates \
    ARCHS=arm64e \
    CODE_SIGN_STYLE=Automatic \
    DEVELOPMENT_TEAM="$DEVELOPMENT_TEAM" \
    ONLY_ACTIVE_ARCH=YES \
    RORK_HOOK_DEVICE_BUNDLE_ID_PREFIX="$bundle_id_prefix"

products_directory="$derived_data/Build/Products/Debug-iphoneos"
test_bundle="$products_directory/RorkHookDeviceHost.app/PlugIns/RorkHookDeviceTests.xctest"
test_binary="$test_bundle/RorkHookDeviceTests"
fixups="$(xcrun dyld_info -fixups "$test_binary")"
if ! grep -Eq \
    '__auth_got[[:space:]].*auth-bind[[:space:]].*/_getpid([[:space:]]|$)' \
    <<<"$fixups"; then
    echo "The device test fixture no longer imports getpid through an authenticated slot." >&2
    exit 1
fi

printf 'Physical-device result bundle: %s\n' "$result_bundle"
