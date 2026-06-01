#ifndef RORK_HOOK_SYMBOLS_H
#define RORK_HOOK_SYMBOLS_H

#include "RorkHookTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

RORK_HOOK_ASSUME_NONNULL_BEGIN

/// Resolves `symbolName` to its run-time address inside an image that dyld has
/// already mapped, including private (`N_SECT`) symbols that `dlsym` cannot see.
///
/// `header` must be a live, slid Mach-O header (for example a value returned by
/// `_dyld_get_image_header`). The returned pointer is corrected for the image
/// slide; on arm64e it is signed with the function-pointer key when it lands in
/// executable memory, so it can be called directly. Returns `NULL` when the
/// symbol is absent or the header is malformed.
void *RORK_HOOK_NULLABLE RorkHookFindSymbol(const RorkHookMachHeader *header,
                                            const char *symbolName);

/// Resolves `symbolName` inside a Mach-O whose file layout is mapped verbatim
/// (for example an `mmap` of an on-disk binary that dyld has not loaded), where
/// symbol and string tables are reached through file offsets rather than the
/// `__LINKEDIT` slide.
///
/// The returned pointer addresses the symbol's location within the mapped file
/// image. Returns `NULL` when the symbol is absent or the header is malformed.
void *RORK_HOOK_NULLABLE RorkHookFindSymbolInFileImage(const RorkHookMachHeader *header,
                                                       const char *symbolName);

RORK_HOOK_ASSUME_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* RORK_HOOK_SYMBOLS_H */
