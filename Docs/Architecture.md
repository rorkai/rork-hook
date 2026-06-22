# Architecture

Rork Hook is SPM-first with a small C ABI as the package boundary.

## Goals

- Keep runtime patching primitives dependency-free and owned by Rork.
- Give SwiftPM consumers a normal source package with no custom build phases.
- Expose one ABI that works for Swift, Objective-C, Objective-C++, C, and C++.
- Keep private Mach-O and dyld-cache parsing helpers out of the public header
  surface.
- Preserve host buildability while making device-only behavior explicit.

## Layers

- `Sources/RorkHook/include`: public ABI. These headers are the contract for all
  supported languages and are covered with doc comments.
- `Sources/RorkHook/private`: implementation-only types and helpers shared by
  the C translation units.
- `Sources/RorkHook/*.c`: libsystem-only implementation. Device-only assembly
  and comm-page paths are gated by platform and architecture checks.
- `Tests/RorkHookTests`: Swift tests that import the same Clang module clients
  use.

## Binding Strategy

The public C ABI is the source of truth. Swift imports the SwiftPM Clang module
directly, and Objective-C/C-family clients include:

```objc
#import <RorkHook/RorkHook.h>
```

This keeps the initial package small and avoids a parallel Swift wrapper that
would need to duplicate low-level pointer and Mach-O concepts. A Swift overlay
can be added later if real call sites need safer typed wrappers, but it should
remain additive and should not replace the C ABI.

## Distribution

Rork Hook ships as source through SwiftPM:

```swift
.package(url: "https://github.com/rorkai/rork-hook.git", from: "0.3.0")
```

and:

```swift
.product(name: "RorkHook", package: "rork-hook")
```

There is no binary artifact or generated project. Consumers only need SwiftPM
and an Apple toolchain.

## Initial Scope

The `0.1.0` API covers:

- VM protection and arm64e TPRO write-window helpers.
- Protected pointer-slot writes.
- Private symbol lookup in loaded and file-mapped Mach-O images.
- Dyld shared-cache path discovery and local-symbol lookup.
- Arm64 instruction decoding helpers used by dyld/runtime stubs.
- Absolute jump generation and destructive function replacement.
- Fishhook-style rebinding for one image or all images.
- Package and ABI version probes.

## Non-Goals

- Rork Hook does not provide a full dynamic instrumentation framework.
- Rork Hook does not provide original-function trampolines for destructive
  detours.
- Rork Hook does not promise App Store suitability.
- Rork Hook does not own higher-level runtime policy. Client packages should
  decide which hooks to install, when to install them, and how to validate them.
