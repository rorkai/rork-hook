#ifndef RORK_HOOK_VERSION_H
#define RORK_HOOK_VERSION_H

#include "RorkHookTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

RORK_HOOK_ASSUME_NONNULL_BEGIN

/// Major version for the compiled RorkHook package.
#define RORK_HOOK_VERSION_MAJOR 0

/// Minor version for the compiled RorkHook package.
#define RORK_HOOK_VERSION_MINOR 1

/// Patch version for the compiled RorkHook package.
#define RORK_HOOK_VERSION_PATCH 4

/// Human-readable semantic version for the compiled RorkHook package.
#define RORK_HOOK_VERSION_STRING "0.1.4"

/// Version of the exported C ABI. This changes only when the ABI contract
/// itself changes, not for every package release.
#define RORK_HOOK_ABI_VERSION 1

/// Returns ``RORK_HOOK_VERSION_STRING`` for clients that prefer a run-time
/// version probe over preprocessor macros.
const char *RorkHookVersion(void);

/// Returns ``RORK_HOOK_ABI_VERSION`` for clients that gate behavior on the
/// low-level C ABI contract.
uint32_t RorkHookABIVersion(void);

RORK_HOOK_ASSUME_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* RORK_HOOK_VERSION_H */
