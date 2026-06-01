# Safety

Rork Hook intentionally exposes low-level process mutation primitives. The
package should be used as a narrow toolkit inside controlled runtime code.

## Operational Constraints

- Treat public APIs as process-global mutation. Rebinding and detours can affect
  unrelated code loaded into the same process.
- Avoid Objective-C, Foundation, allocation-heavy code, logging frameworks, and
  locks inside callbacks that may run under dyld or loader-sensitive contexts.
- Keep replacement functions simple and reentrant unless the caller owns every
  path that can reach them.
- `RorkHookReplaceFunction` is destructive. It overwrites the target prologue
  and does not preserve a trampoline back to the original implementation.
- Always validate target addresses and symbol names before mutating memory.
- Keep TPRO write windows as short as possible and balanced on every path.

## Platform Constraints

- arm64e pointer authentication requires slot-specific signing for authenticated
  symbol-pointer sections.
- TPRO write-window behavior is device-only and depends on private arm64e iOS
  implementation details.
- Dyld shared-cache local-symbol metadata is an Apple implementation detail and
  may change across OS releases.
- Host and simulator builds intentionally fall back or report unsupported rather
  than attempting device-only comm-page behavior.

## Validation Expectations

Host tests cover deterministic helpers and safe fallbacks:

```bash
swift test
```

The following behavior must be validated on real iOS hardware before relying on
it in a runtime:

- TPRO protected writes.
- `__AUTH_CONST` and `__DATA_CONST` slot mutation.
- Global rebinding across images loaded before and after registration.
- Dyld shared-cache local-symbol lookup on the target OS version.
- Any hook that runs during runtime startup or dyld-sensitive boot paths.
