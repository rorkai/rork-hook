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
/// executable memory, so it can be called directly. It remains valid only while
/// the image stays loaded. Returns `NULL` when the symbol is absent or the
/// header is malformed.
void *RORK_HOOK_NULLABLE RorkHookFindSymbol(const RorkHookMachHeader *header,
                                            const char *symbolName);

/// Resolves `symbolName` inside a Mach-O whose file layout is mapped verbatim
/// (for example an `mmap` of an on-disk binary that dyld has not loaded), where
/// symbol and string tables are reached through file offsets rather than the
/// `__LINKEDIT` slide.
///
/// The returned pointer addresses the symbol's location within the mapped file
/// image and remains valid only while that mapping exists. Because this
/// compatibility form infers its bound from the readable VM region containing
/// `header`, callers that know the mapping length should prefer
/// ``RorkHookFindSymbolInFileImageWithSize``. Returns `NULL` when the symbol is
/// absent, the inferred region ends too early, or the header is malformed.
void *RORK_HOOK_NULLABLE RorkHookFindSymbolInFileImage(const RorkHookMachHeader *header,
                                                       const char *symbolName);

/// Resolves `symbolName` inside the first `mappedSize` bytes of a verbatim
/// file-mapped Mach-O image.
///
/// This is the bounds-safe form of ``RorkHookFindSymbolInFileImage`` and should
/// be used whenever the mapped file's length is available. Every load command,
/// symbol-table entry, string, segment translation, and returned address is
/// validated against the supplied mapping. Returns `NULL` for malformed,
/// truncated, non-file-backed, or missing symbols. A successful result borrows
/// the mapping's lifetime and becomes invalid when the mapping is released.
void *RORK_HOOK_NULLABLE RorkHookFindSymbolInFileImageWithSize(
    const RorkHookMachHeader *header,
    size_t mappedSize,
    const char *symbolName);

RORK_HOOK_ASSUME_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* RORK_HOOK_SYMBOLS_H */
