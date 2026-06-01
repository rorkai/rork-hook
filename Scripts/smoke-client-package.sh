#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGE_ID="$(basename "$ROOT_DIR")"
PACKAGE_ID="${PACKAGE_ID%.git}"
PACKAGE_ID="$(printf '%s' "$PACKAGE_ID" | tr '[:upper:]' '[:lower:]')"
SMOKE_DIR="${TMPDIR:-/tmp}/rork-hook-client-smoke"
IOS_DERIVED_DATA="${TMPDIR:-/tmp}/rork-hook-client-smoke-ios-dd"

rm -rf "$SMOKE_DIR" "$IOS_DERIVED_DATA"
mkdir -p "$SMOKE_DIR/Sources/ObjCSmoke/include" "$SMOKE_DIR/Sources/ClientSmoke"

cat > "$SMOKE_DIR/Package.swift" <<SWIFT
// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "ClientSmoke",
    platforms: [
        .macOS(.v13),
        .iOS(.v15),
    ],
    products: [
        .library(name: "ClientSmoke", targets: ["ClientSmoke"]),
    ],
    dependencies: [
        .package(path: "$ROOT_DIR"),
    ],
    targets: [
        .target(
            name: "ObjCSmoke",
            dependencies: [
                .product(name: "RorkHook", package: "$PACKAGE_ID"),
            ],
            publicHeadersPath: "include"
        ),
        .target(
            name: "ClientSmoke",
            dependencies: [
                "ObjCSmoke",
                .product(name: "RorkHook", package: "$PACKAGE_ID"),
            ]
        ),
    ]
)
SWIFT

cat > "$SMOKE_DIR/Sources/ObjCSmoke/include/ObjCSmoke.h" <<'OBJC'
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

const char *RorkHookObjCSmokeVersion(void);

#ifdef __cplusplus
}
#endif
OBJC

cat > "$SMOKE_DIR/Sources/ObjCSmoke/ObjCSmoke.m" <<'OBJC'
#import "ObjCSmoke.h"

#import "RorkHook.h"
#import <RorkHook/RorkHook.h>

const char *RorkHookObjCSmokeVersion(void) {
    return RorkHookVersion();
}
OBJC

cat > "$SMOKE_DIR/Sources/ClientSmoke/Smoke.swift" <<'SWIFT'
import ObjCSmoke
import RorkHook

public enum Smoke {
    public static var version: String {
        String(cString: RorkHookVersion())
    }

    public static var objcVersion: String {
        String(cString: RorkHookObjCSmokeVersion())
    }

    public static var abiVersion: UInt32 {
        RorkHookABIVersion()
    }
}
SWIFT

swift build --package-path "$SMOKE_DIR"

if command -v xcodebuild >/dev/null 2>&1; then
    (
        cd "$SMOKE_DIR"
        xcodebuild \
            -quiet \
            -scheme ClientSmoke \
            -destination 'generic/platform=iOS' \
            -derivedDataPath "$IOS_DERIVED_DATA" \
            build
    )
fi
