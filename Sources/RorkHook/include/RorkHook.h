#ifndef RORK_HOOK_H
#define RORK_HOOK_H

/// # RorkHook
///
/// A small, dependency-free runtime patching toolkit for Apple platforms. It is
/// a first-party, clean-room reimplementation of the litehook library that Rork
/// owns outright; see the repository `NOTICE.md` for provenance.
///
/// The library depends only on libsystem and groups its surface into five
/// concerns:
///
/// - **Memory** (`RorkHookMemory.h`): page-protection changes issued through the
///   `mach_vm_protect` trap, plus the arm64e TPRO write-window helpers.
/// - **Symbols** (`RorkHookSymbols.h`): resolve private `N_SECT` symbols inside
///   loaded or file-mapped Mach-O images that `dlsym` cannot see.
/// - **Shared cache** (`RorkHookSharedCache.h`): locate the dyld shared cache
///   and read local symbols stripped from the in-memory images.
/// - **arm64 decode** (`RorkHookArm64.h`): decode the small instruction subset
///   needed to find runtime-owned global slots in system stubs.
/// - **Detours** (`RorkHookFunction.h`, `RorkHookRebind.h`): replace a function
///   in place with an absolute jump, or rebind a symbol in an image's import
///   table without touching the target function.
///
/// Everything is built for arm64/arm64e first; on the simulator and other
/// architectures the calls degrade to portable fallbacks (or report
/// unsupported) rather than misbehaving.
///
/// Import this umbrella header to pull in the whole API, or include a single
/// concern header when you only need one area.

#include "RorkHookTypes.h"

#include "RorkHookVersion.h"
#include "RorkHookMemory.h"
#include "RorkHookArm64.h"
#include "RorkHookSymbols.h"
#include "RorkHookSharedCache.h"
#include "RorkHookFunction.h"
#include "RorkHookRebind.h"

#endif /* RORK_HOOK_H */
