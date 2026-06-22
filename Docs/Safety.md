# Safety

Rork Hook intentionally exposes low-level process mutation primitives. The
package should be used as a narrow toolkit inside controlled runtime code.

## Operational Constraints

- Treat public APIs as process-global mutation. Rebinding and detours can affect
  unrelated code loaded into the same process.
- Avoid Objective-C, Foundation, allocation-heavy code, logging frameworks, and
  locks inside callbacks that may run under dyld or loader-sensitive contexts.
- Global-rebind filters can run under dyld's loader lock. They must not call
  `dlopen`, trigger class or framework loading, block, or acquire locks that
  loader-sensitive code may hold.
- Keep replacement functions simple and reentrant unless the caller owns every
  path that can reach them.
- `RorkHookReplaceFunction` is destructive. It overwrites the target prologue
  and does not preserve a trampoline back to the original implementation. The
  caller must own enough complete instructions and prevent concurrent execution
  during the patch; prefer `RorkHookReplaceFunctionWithSize` when the prologue
  length is known.
- Always validate target addresses and symbol names before mutating memory.
- Keep TPRO write windows as short as possible and balanced on every path.

## Platform Constraints

- arm64e pointer authentication requires slot-specific signing for authenticated
  symbol-pointer sections.
- iOS 26 exposes process-level TPRO enablement through a public security
  configuration API. Opening and closing the thread write window still depends
  on arm64e iOS implementation details.
- Dyld shared-cache local-symbol metadata is an Apple implementation detail and
  may change across OS releases.
- Host and simulator builds intentionally fall back or report unsupported rather
  than attempting device-only comm-page behavior.

## Validation Expectations

Host tests cover deterministic helpers and safe fallbacks:

```bash
swift test
Scripts/test-coverage.sh
Scripts/test-undefined-behavior.sh
Scripts/test-sanitizers.sh
```

The opt-in harness covers physical arm64e behavior:

```bash
RORK_HOOK_DEVICE_UDID=<device-udid> \
DEVELOPMENT_TEAM=<team-id> \
Scripts/test-device.sh
```

It validates process-level TPRO reporting, the thread write-window contract,
authenticated import-slot mutation and invocation, and dyld shared-cache
local-symbol lookup on the connected OS version. Ordinary development signing
does not enable TPRO; the same test reaches the protected write-window path only
when the host process is signed by an environment authorized to enable it.

Runtime-specific startup hooks and global rebinding in loader-sensitive boot
paths still require integration validation in the consuming application.
