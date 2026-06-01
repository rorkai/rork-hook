#ifndef RORK_HOOK_FUNCTION_H
#define RORK_HOOK_FUNCTION_H

#include "RorkHookTypes.h"

#include <mach/mach.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

RORK_HOOK_ASSUME_NONNULL_BEGIN

/// Number of 32-bit instructions in the absolute-jump sequence emitted by
/// ``RorkHookBuildAbsoluteJump`` on arm64.
#define RORK_HOOK_ABSOLUTE_JUMP_WORDS 5

/// Encodes an unconditional absolute jump to `destination` into `instructions`.
///
/// On arm64 this is four `MOVK x16, ...` instructions that materialise the 64-bit
/// destination followed by `BR x16`, which reaches any address without relying
/// on PC-relative range. `capacity` is the number of `uint32_t` slots
/// available; the function writes nothing and returns 0 when the buffer is too
/// small or the architecture is unsupported. Otherwise it returns the number of
/// instructions written (``RORK_HOOK_ABSOLUTE_JUMP_WORDS``).
///
/// This is the pure code-generation primitive behind ``RorkHookReplaceFunction``
/// and is exposed so callers can build their own trampolines and so the
/// encoding can be unit-tested without modifying live code.
size_t RorkHookBuildAbsoluteJump(const void *destination,
                                 uint32_t *instructions,
                                 size_t capacity);

/// Overwrites the prologue of `function` with an absolute jump to
/// `replacement`, so every call to `function` runs `replacement` instead.
///
/// This is a destructive, non-reentrant detour: the original instructions are
/// clobbered, so the replacement cannot call through to the original. Pointer
/// authentication bits are stripped from both arguments before patching.
/// Returns `KERN_SUCCESS` on success, or the failing `kern_return_t` from the
/// memory-protection step. Unsupported architectures return `KERN_NOT_SUPPORTED`.
kern_return_t RorkHookReplaceFunction(void *function, void *replacement);

RORK_HOOK_ASSUME_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* RORK_HOOK_FUNCTION_H */
