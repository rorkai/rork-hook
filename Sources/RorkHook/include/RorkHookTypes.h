#ifndef RORK_HOOK_TYPES_H
#define RORK_HOOK_TYPES_H

#include <mach-o/loader.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// Nullability helpers used by the public C headers. They keep Swift and
/// Objective-C imports precise while compiling away for non-Clang C compilers.
#if defined(__clang__)
#define RORK_HOOK_ASSUME_NONNULL_BEGIN _Pragma("clang assume_nonnull begin")
#define RORK_HOOK_ASSUME_NONNULL_END _Pragma("clang assume_nonnull end")
#define RORK_HOOK_NULLABLE _Nullable
#else
#define RORK_HOOK_ASSUME_NONNULL_BEGIN
#define RORK_HOOK_ASSUME_NONNULL_END
#define RORK_HOOK_NULLABLE
#endif

/// Native Mach-O header type for the building architecture.
///
/// RorkHook is a 64-bit-first library, but the alias keeps the public API
/// arch-agnostic so callers do not have to special-case `mach_header` versus
/// `mach_header_64` at every call site.
#if defined(__LP64__)
typedef struct mach_header_64 RorkHookMachHeader;
#else
typedef struct mach_header RorkHookMachHeader;
#endif

/// Sentinel passed to image-filtered APIs to mean "no filter; apply to every
/// eligible image".
#define RORK_HOOK_NO_FILTER ((RorkHookImageFilter)NULL)

RORK_HOOK_ASSUME_NONNULL_BEGIN

#ifdef __cplusplus
extern "C" {
#endif

/// Predicate used to include or exclude a Mach-O image from a global rebind.
///
/// Return `true` to apply the rebind to `header`, `false` to skip it. The image
/// that defines the replacement function is always excluded automatically, so a
/// filter never has to guard against rebinding the replacement onto itself.
///
/// Dyld may invoke the predicate while holding its loader lock. The callback
/// must therefore remain non-blocking and must not load images, acquire locks
/// that loader-sensitive code may hold, or call allocation-heavy frameworks.
typedef bool (*RorkHookImageFilter)(const RorkHookMachHeader *header);

#ifdef __cplusplus
}
#endif

RORK_HOOK_ASSUME_NONNULL_END

#endif /* RORK_HOOK_TYPES_H */
