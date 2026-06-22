#ifndef RORK_HOOK_SHARED_CACHE_H
#define RORK_HOOK_SHARED_CACHE_H

#include "RorkHookTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

RORK_HOOK_ASSUME_NONNULL_BEGIN

/// Returns the on-disk path of the active dyld shared cache for the running
/// architecture, or an empty string when it cannot be located.
///
/// The lookup honors a process-private cache (`DYLD_SHARED_REGION=private`
/// with `DYLD_SHARED_CACHE_DIR`), then the iOS 16+ Cryptex location, then the
/// pre-16 system location, and finally probes the architecture suffixes
/// (`_arm64e`, `_arm64`, ...). The result is computed once and cached, so the
/// returned pointer stays valid for the lifetime of the process and must not be
/// freed. Process-private environment overrides must therefore be configured
/// before the first call.
const char *RorkHookLocateSharedCache(void);

/// Resolves a private (local) symbol that lives in `imagePath` inside the dyld
/// shared cache.
///
/// The resolver reads a sibling `.symbols` sidecar when it can be mapped and
/// otherwise uses local-symbol metadata stored in the main cache. These symbols
/// are stripped from the in-memory images, so disk metadata is required to
/// recover addresses such as private `libdyld` or `libsystem` internals. The
/// returned pointer addresses the process's live shared-cache mapping, is
/// corrected for its slide, and on arm64e is signed with the function-pointer
/// key when it lands in executable memory. Returns `NULL` when the cache, image,
/// symbol, or bounded metadata is unavailable or invalid.
void *RORK_HOOK_NULLABLE RorkHookFindSharedCacheSymbol(const char *imagePath,
                                                       const char *symbolName);

RORK_HOOK_ASSUME_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* RORK_HOOK_SHARED_CACHE_H */
