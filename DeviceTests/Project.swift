import ProjectDescription

let hostTargetName = "RorkHookDeviceHost"
let testTargetName = "RorkHookDeviceTests"

let project = Project(
    name: "RorkHookDeviceTests",
    organizationName: "Rork Hook",
    packages: [
        .package(path: ".."),
    ],
    settings: .settings(
        base: [
            "ARCHS": "arm64e",
            "CODE_SIGN_STYLE": "Automatic",
            "IPHONEOS_DEPLOYMENT_TARGET": "15.0",
            "ONLY_ACTIVE_ARCH": "YES",
            "RORK_HOOK_DEVICE_BUNDLE_ID_PREFIX": "dev.rorkhook.device-tests",
        ]
    ),
    targets: [
        .target(
            name: hostTargetName,
            destinations: [.iPhone],
            product: .app,
            bundleId: "$(RORK_HOOK_DEVICE_BUNDLE_ID_PREFIX).host",
            deploymentTargets: .iOS("15.0"),
            infoPlist: .default,
            sources: [
                "Host/**/*.swift",
            ],
            settings: .settings(
                base: [
                    "INFOPLIST_KEY_UILaunchScreen_Generation": "YES",
                    "SWIFT_VERSION": "6.0",
                    "TARGETED_DEVICE_FAMILY": "1",
                ]
            )
        ),
        .target(
            name: testTargetName,
            destinations: [.iPhone],
            product: .unitTests,
            bundleId: "$(RORK_HOOK_DEVICE_BUNDLE_ID_PREFIX).tests",
            deploymentTargets: .iOS("15.0"),
            infoPlist: .default,
            sources: [
                "Tests/**/*.m",
            ],
            dependencies: [
                .target(name: hostTargetName),
                .package(product: "RorkHook"),
            ],
            settings: .settings(
                base: [
                    "BUNDLE_LOADER": "$(TEST_HOST)",
                    "CLANG_ENABLE_MODULES": "YES",
                    "OTHER_CFLAGS": "$(inherited) -fno-builtin-getpid",
                    "TARGETED_DEVICE_FAMILY": "1",
                    "TEST_HOST": "$(BUILT_PRODUCTS_DIR)/\(hostTargetName).app/$(BUNDLE_EXECUTABLE_FOLDER_PATH)/\(hostTargetName)",
                ]
            )
        ),
    ],
    schemes: [
        .scheme(
            name: testTargetName,
            shared: true,
            buildAction: .buildAction(
                targets: [
                    .target(hostTargetName),
                    .target(testTargetName),
                ]
            ),
            testAction: .targets(
                [
                    .testableTarget(
                        target: .target(testTargetName),
                        parallelization: .disabled
                    ),
                ],
                expandVariableFromTarget: .target(hostTargetName)
            ),
            runAction: .runAction(
                executable: .target(hostTargetName)
            ),
            archiveAction: .archiveAction(
                configuration: .release
            ),
            profileAction: .profileAction(
                executable: .target(hostTargetName)
            ),
            analyzeAction: .analyzeAction(
                configuration: .debug
            )
        ),
    ]
)
