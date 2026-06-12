# Changelog

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
