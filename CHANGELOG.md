# Changelog

## 0.3.0 - 2026-06-22

- Add `RorkHookReplaceFunctionWithSize` so callers can validate the available
  prologue length before installing a destructive arm64 detour.
- Harden Mach-O, dyld shared-cache, symbol-table, and arm64 instruction parsing
  with bounded reads, overflow checks, and deterministic malformed-input
  failures.
- Make protected pointer writes preserve original page protections, account for
  process-level TPRO enforcement, and balance write-window state across failure
  paths.
- Harden image-local and global rebinding for authenticated arm64e slots,
  future image loads, and loader-lock-safe filtering.
- Add production C coverage thresholds, strict warning and sanitizer gates,
  downstream Swift 5 consumer builds, and a Tuist-generated physical-device
  harness for authenticated rebinding, shared-cache lookup, and TPRO behavior.

## 0.2.0 - 2026-06-19

- Require a Swift 6.0 or newer toolchain and compile the package's Swift
  targets in Swift 6 language mode without changing the exported C ABI,
  supported platforms, or deployment targets.
- Keep the downstream smoke client in Swift 5 language mode to verify that
  existing Swift consumers can continue importing the C module.
- Query the host VM page size through `getpagesize()` in memory tests instead
  of reading Darwin's mutable `vm_page_size` global.

## 0.1.4 - 2026-06-13

- Resolve symbol-pointer section addresses with `getsectiondata` instead of
  `section->addr + __TEXT slide`. In the dyld shared cache, `__TEXT` and the
  data segments are split into separate regions with different offsets, so the
  `__TEXT` slide does not apply to a data section's link-time address and the
  computed pointer landed in an unmapped cache hole — faulting (SIGBUS /
  KERN_PROTECTION_FAILURE) while rebinding shared-cache images. `getsectiondata`
  returns the correct mapped address and size for both cache and on-disk images.

## 0.1.3 - 2026-06-12

- Re-sign rewritten `__auth_got` slots with the IB (process-independent code)
  key instead of IA, matching the arm64e authenticated-GOT ABI and the
  reference litehook implementation. Signing with IA produced a pointer the
  call site could not authenticate, crashing when the rebound symbol was first
  called. Also restores the `ptrauth_auth_function` slot read (reverting the
  0.1.2 strip change), matching litehook.

## 0.1.2 - 2026-06-12

- Fix SIGBUS during global symbol rebinding on FPAC-capable arm64e devices
  (A17/A18 and later). `RorkHookRebindSection` authenticated every `__auth_got`
  slot with `ptrauth_auth_function`, which faults under FPAC when a foreign slot
  is signed under a scheme other than IA + address diversity. Strip the
  signature for the slot comparison instead; matching slots are still re-signed
  correctly when rewritten.

## 0.1.1 - 2026-06-12

- Fix intermittent SIGBUS in `RorkHookStoreProtectedPointer` on arm64e devices.
  The TPRO write window is now opened whenever the device supports TPRO, not
  only when `vm_protect` fails, so stores into TPRO-hardened shared-cache
  `__DATA_CONST`/`__AUTH_CONST` slots no longer fault when `vm_protect` reports
  success.

## 0.1.0 - 2026-06-01

- Add the source-only `RorkHook` SwiftPM product.
- Export the public C ABI used by Swift, Objective-C, Objective-C++, C, and C++
  clients.
- Add version probes through `RorkHookVersion()` and `RorkHookABIVersion()`.
- Add host-runnable Swift tests for deterministic helpers and safe fallbacks.
- Add correctness coverage for arm64 decoding, protected memory writes, loaded
  image symbol lookup, and image-local rebinding.
- Add downstream SwiftPM smoke tests and CI.
