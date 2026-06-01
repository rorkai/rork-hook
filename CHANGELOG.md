# Changelog

## 0.1.0 - Unreleased

- Add the source-only `RorkHook` SwiftPM product.
- Export the public C ABI used by Swift, Objective-C, Objective-C++, C, and C++
  clients.
- Add version probes through `RorkHookVersion()` and `RorkHookABIVersion()`.
- Add host-runnable Swift tests for deterministic helpers and safe fallbacks.
- Add correctness coverage for arm64 decoding, protected memory writes, loaded
  image symbol lookup, and image-local rebinding.
- Add downstream SwiftPM smoke tests and CI.
