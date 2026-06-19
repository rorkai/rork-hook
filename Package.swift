// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "rork-hook",
    platforms: [
        .macOS(.v13),
        .iOS(.v15),
    ],
    products: [
        .library(
            name: "RorkHook",
            type: .static,
            targets: ["RorkHook"]
        ),
    ],
    targets: [
        .target(
            name: "RorkHook",
            path: "Sources/RorkHook",
            sources: [
                "RorkHookVersion.c",
                "RorkHookInternal.c",
                "RorkHookArm64.c",
                "RorkHookMemory.c",
                "RorkHookSymbols.c",
                "RorkHookSharedCache.c",
                "RorkHookFunction.c",
                "RorkHookRebind.c",
            ],
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("include"),
                .headerSearchPath("private"),
            ]
        ),
        .target(
            name: "RorkHookTestSupport",
            dependencies: ["RorkHook"],
            path: "Tests/RorkHookTestSupport",
            publicHeadersPath: "include"
        ),
        .testTarget(
            name: "RorkHookTests",
            dependencies: ["RorkHook", "RorkHookTestSupport"],
            path: "Tests/RorkHookTests"
        ),
    ],
    swiftLanguageModes: [.v6],
    cLanguageStandard: .gnu11,
    cxxLanguageStandard: .gnucxx17
)
