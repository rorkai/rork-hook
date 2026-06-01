#ifndef RORK_HOOK_REBIND_H
#define RORK_HOOK_REBIND_H

#include "RorkHookTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

RORK_HOOK_ASSUME_NONNULL_BEGIN

/// Redirects every bound reference to `replacee` to point at `replacement`
/// within a single Mach-O image, in the style of fishhook and
/// `dyld_dynamic_interpose`.
///
/// The function pointer slots in the image's `__got`/`__auth_got` and lazy and
/// non-lazy symbol-pointer sections are scanned; any slot already resolved to
/// `replacee` is rewritten. Unlike ``RorkHookReplaceFunction`` this leaves the
/// target function untouched and only edits this image's import table, so calls
/// made through other images are unaffected. Pointer-authenticated slots are
/// re-signed for their slot address.
void RorkHookRebindSymbolInImage(const RorkHookMachHeader *header,
                                 void *replacee,
                                 void *replacement);

/// Applies ``RorkHookRebindSymbolInImage`` to every currently loaded image and
/// registers an image-load callback so images mapped later are rebound too.
///
/// `filter` (may be ``RORK_HOOK_NO_FILTER``) decides which images participate;
/// the image that defines `replacement` is always excluded so it keeps calling
/// the original. Returns `true` once the global rebind is registered, or
/// `false` if the arguments are invalid or the defining image of `replacement`
/// cannot be determined.
bool RorkHookRebindSymbolGlobally(void *replacee,
                                  void *replacement,
                                  RorkHookImageFilter RORK_HOOK_NULLABLE filter);

RORK_HOOK_ASSUME_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* RORK_HOOK_REBIND_H */
