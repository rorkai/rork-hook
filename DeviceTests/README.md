# Physical Device Tests

The device harness validates behavior that a macOS process and the iOS
Simulator cannot reproduce:

- process-level TPRO detection and per-thread write-window state;
- live dyld shared-cache discovery and local-symbol lookup;
- rebinding and invoking an authenticated arm64e import slot.

The tests run inside a minimal iOS host application because SwiftPM's
tool-hosted test bundle cannot execute directly on a physical iPhone. The
checked-in Xcode project references the package at the repository root and
contains no development team, device identifier, provisioning profile, or
private entitlement.

Run the harness with a connected, trusted iPhone:

```bash
RORK_HOOK_DEVICE_UDID=00000000-0000000000000000 \
DEVELOPMENT_TEAM=ABCDE12345 \
Scripts/test-device.sh
```

`RORK_HOOK_DEVICE_BUNDLE_ID_PREFIX` may be supplied when the generated default
is unsuitable for the signing team. Build products and the result bundle are
written beneath `${TMPDIR:-/tmp}/rork-hook-device-tests`.

Ordinary development signing exercises the non-TPRO process path. When the host
is signed by an environment authorized to enable TPRO, the same test verifies
that the write window opens and closes. The repository intentionally does not
contain private entitlements or signing material.
