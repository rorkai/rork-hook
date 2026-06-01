#ifndef RORK_HOOK_INTERNAL_H
#define RORK_HOOK_INTERNAL_H

#include "RorkHookTypes.h"

#include <mach-o/loader.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// Internal helpers shared between the RorkHook translation units. These are not
/// part of the public API and are intentionally kept out of the `include/`
/// directory.

#if defined(__LP64__)
typedef struct segment_command_64 RorkHookSegmentCommand;
typedef struct section_64 RorkHookSection;
typedef struct nlist_64 RorkHookNList;
#define RORK_HOOK_LC_SEGMENT LC_SEGMENT_64
#else
typedef struct segment_command RorkHookSegmentCommand;
typedef struct section RorkHookSection;
typedef struct nlist RorkHookNList;
#define RORK_HOOK_LC_SEGMENT LC_SEGMENT
#endif

/// Returns the VM slide of a loaded image by comparing the header's address with
/// the unslid `vmaddr` of the segment that maps the header (`__TEXT`).
///
/// This is the general form of the slide calculation: it is correct for the
/// main executable (`__TEXT` at a non-zero base) and for dylibs (`__TEXT` based
/// at zero) alike. Writes `true`/`false` to `resolved` when non-NULL.
intptr_t RorkHookImageSlide(const RorkHookMachHeader *header, bool *resolved);

/// Visitor callback used by ``RorkHookForEachLoadCommand``. Return `true` to
/// continue scanning or `false` to stop after the current command.
typedef bool (*RorkHookLoadCommandVisitor)(const struct load_command *command,
                                           uint32_t index,
                                           void *context);

/// Iterates the header's load-command table with strict `sizeofcmds` bounds.
/// Malformed command counts or sizes stop iteration before any out-of-bounds
/// command or segment data is read.
bool RorkHookForEachLoadCommand(const RorkHookMachHeader *header,
                                RorkHookLoadCommandVisitor visitor,
                                void *context);

/// Returns `true` when `[address, address + length)` lies entirely inside a
/// single readable VM region. Guards every dereference of attacker- or
/// cache-controlled offsets so a malformed image yields `NULL` instead of a
/// crash.
bool RorkHookMemoryIsReadable(const void *address, size_t length);

/// On arm64e, signs `pointer` with the function-pointer key when it points into
/// executable memory so the result is directly callable. A no-op everywhere
/// else and for `NULL`.
void *RorkHookSignPointerIfExecutable(void *pointer);

/// Returns the active dyld shared cache slide, or 0 when it cannot be read.
uintptr_t RorkHookSharedCacheSlide(void);

#endif /* RORK_HOOK_INTERNAL_H */
