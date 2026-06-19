# Rork Hook

[![Swift](https://img.shields.io/badge/Swift-6.0-orange.svg)](Package.swift)
[![SwiftPM](https://img.shields.io/badge/SwiftPM-supported-brightgreen.svg)](Package.swift)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

Rork Hook is a lightweight Apple OS runtime hooking library with no
dependencies except libsystem.

The package is source-only and SwiftPM-first. Swift, Objective-C, Objective-C++,
C, and C++ clients all use the same exported C ABI:

```c
#include <RorkHook/RorkHook.h>
```

Swift clients import the Clang module directly:

```swift
import RorkHook

let version = String(cString: RorkHookVersion())
```

## Package Structure

- `Sources/RorkHook/include`: public C ABI headers exported by SwiftPM.
- `Sources/RorkHook/private`: implementation-only Mach-O and dyld-cache helpers.
- `Sources/RorkHook`: libsystem-only C implementation.
- `Tests/RorkHookTests`: host-runnable Swift tests for deterministic behavior
  and safe host fallbacks.
- `Tests/RorkHookTestSupport`: C/Objective-C helpers used by correctness tests.
- `Scripts/smoke-client-package.sh`: temporary downstream package smoke test for
  Swift and Objective-C/C import paths.

## Current Capabilities

- Change page protection with a direct arm64 iOS `mach_vm_protect` trap path,
  bypassing hooked libsystem stubs when installing further hooks.
- Open and close the arm64e TPRO thread write window when protected memory
  cannot be remapped normally.
- Write pointer-sized slots in `__DATA_CONST` and `__AUTH_CONST` memory while
  restoring the original page protection.
- Decode the small arm64 instruction subset needed by dyld/runtime stubs:
  `ADRP`, `ADD (immediate)`, `LDR`, `LDUR`, `MOVZ`, and leading branch veneers.
- Resolve private `N_SECT` symbols from loaded Mach-O images and file-mapped
  Mach-O images.
- Locate the active dyld shared cache and resolve local symbols from the cache's
  local-symbol metadata.
- Build absolute arm64 jump sequences and replace function prologues with a
  destructive non-reentrant detour.
- Rebind symbol-pointer sections in one image or globally across currently
  loaded and future images, including authenticated pointer slots on arm64e.

## SwiftPM Consumers

Use the tagged package:

```swift
.package(url: "https://github.com/rorkai/rork-hook.git", from: "0.1.0")
```

and depend on the `RorkHook` product:

```swift
.product(name: "RorkHook", package: "rork-hook")
```

Objective-C and C-family clients can include the umbrella header from targets
that depend on the product:

```objc
#import <RorkHook/RorkHook.h>
```

## Usage Examples

### Swift

Swift imports the `RorkHook` Clang module directly. The C ABI remains the source
of truth, so Swift code works with C pointers and Mach protection constants.

```swift
import RorkHook

public struct HookEnvironment {
    public let packageVersion: String
    public let abiVersion: UInt32
    public let supportsTPRO: Bool
    public let threadCanWriteTPRO: Bool

    public static func current() -> HookEnvironment {
        HookEnvironment(
            packageVersion: String(cString: RorkHookVersion()),
            abiVersion: RorkHookABIVersion(),
            supportsTPRO: RorkHookSupportsTPRO(),
            threadCanWriteTPRO: RorkHookThreadCanWriteTPRO()
        )
    }
}
```

Protected pointer slots can be written through the same C ABI. The helper
temporarily changes page protection when possible and falls back to a TPRO write
window on supported arm64e devices.

```swift
import RorkHook

func storeProtectedPointer(
    slot: UnsafeMutableRawPointer,
    value: UnsafeRawPointer?
) -> Bool {
    RorkHookStoreProtectedPointer(
        slot,
        value,
        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY
    )
}
```

### Objective-C

Objective-C clients can use either framework-style or direct umbrella imports:

```objc
#import <Foundation/Foundation.h>
#import <RorkHook/RorkHook.h>

static void LogHookEnvironment(void) {
    NSLog(@"RorkHook %s ABI %u", RorkHookVersion(), RorkHookABIVersion());
    NSLog(@"TPRO supported: %@", RorkHookSupportsTPRO() ? @"YES" : @"NO");
}
```

For process-wide rebinding, keep the replacement as a plain C/Objective-C
function pointer and retain the original address explicitly:

```objc
#include <stdio.h>

#import <RorkHook/RorkHook.h>

static int (*original_puts)(const char *);

static int replacement_puts(const char *string) {
    (void)string;
    return original_puts("intercepted puts");
}

static BOOL InstallPutsHook(void) {
    original_puts = puts;
    return RorkHookRebindSymbolGlobally(
        (void *)puts,
        (void *)replacement_puts,
        RORK_HOOK_NO_FILTER
    );
}
```

## Development

Run the host SwiftPM tests:

```bash
swift test
```

Smoke-test downstream package consumption:

```bash
Scripts/smoke-client-package.sh
```

The smoke test builds a temporary SwiftPM package that imports `RorkHook` from
Swift and includes `<RorkHook/RorkHook.h>` from Objective-C. When Xcode is
available, it also performs a generic iOS compile check.

## Safety

Rork Hook exposes low-level runtime patching primitives. It is intended for
controlled runtime and research use, not as a general application framework.
Several APIs intentionally depend on Apple runtime implementation details such
as Mach-O section layout, dyld shared-cache local-symbol metadata, pointer
authentication, and arm64e TPRO behavior.

Host tests verify deterministic helpers and safe fallback behavior. Live detours,
TPRO writes, dyld cache behavior, and runtime integration must be validated on
real iOS hardware.

See `Docs/Safety.md` for operational constraints.

## Documentation

- `Docs/Architecture.md`: package layering, ABI strategy, and non-goals.
- `Docs/Safety.md`: runtime safety constraints and validation expectations.
- `Docs/Release.md`: source-only release workflow.
- `NOTICE.md`: provenance and prior-art notes.

## License

rork-hook is licensed under the Apache License 2.0. See [LICENSE](LICENSE).
