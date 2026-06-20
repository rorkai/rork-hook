#ifndef RORK_HOOK_INTERNAL_H
#define RORK_HOOK_INTERNAL_H

#include "RorkHookTypes.h"

#include <mach-o/loader.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// Internal helpers shared between RorkHook translation units.
///
/// These declarations are implementation contracts rather than exported API
/// and intentionally remain outside the public `include/` directory.

/// Architecture-native aliases for the Mach-O records parsed by the library.
#if defined(__LP64__)
typedef struct segment_command_64 RorkHookSegmentCommand;
typedef struct section_64 RorkHookSection;
typedef struct nlist_64 RorkHookNList;
#define RORK_HOOK_LC_SEGMENT LC_SEGMENT_64
#define RORK_HOOK_MH_MAGIC MH_MAGIC_64
#else
typedef struct segment_command RorkHookSegmentCommand;
typedef struct section RorkHookSection;
typedef struct nlist RorkHookNList;
#define RORK_HOOK_LC_SEGMENT LC_SEGMENT
#define RORK_HOOK_MH_MAGIC MH_MAGIC
#endif

/// Returns the VM slide of a loaded image by comparing the header's address
/// with the unslid `vmaddr` of the segment that maps the header (`__TEXT`).
///
/// This is the general form of the slide calculation: it is correct for the
/// main executable (`__TEXT` at a non-zero base) and for dylibs (`__TEXT` based
/// at zero) alike. The implementation accepts whichever file-backed segment
/// actually maps the header rather than relying on its name. When `resolved` is
/// non-NULL, it is always initialized and reports whether that segment was
/// found. A return value of zero is therefore distinguishable from an
/// unresolved image.
intptr_t RorkHookImageSlide(const RorkHookMachHeader *header, bool *resolved);

/// Visitor callback used by ``RorkHookForEachLoadCommand``. Return `true` to
/// continue scanning or `false` to stop after the current command.
typedef bool (*RorkHookLoadCommandVisitor)(const struct load_command *command,
                                           uint32_t index,
                                           void *context);

/// Validates and iterates a live image's complete load-command table.
///
/// The full table is checked before the first callback runs. This prevents a
/// visitor that stops early from accepting an image with malformed trailing
/// commands, and prevents mutating visitors from making partial changes before
/// malformed metadata is discovered. Returns `false` without invoking
/// `visitor` when the table is invalid.
bool RorkHookForEachLoadCommand(const RorkHookMachHeader *header,
                                RorkHookLoadCommandVisitor visitor,
                                void *context);

/// Validates and iterates a file-mapped image's complete load-command table.
///
/// Every header and command read is confined to `mappedSize`. The full table is
/// validated before callbacks begin; a callback may then return `false` to stop
/// normal iteration without changing the successful return value.
bool RorkHookForEachLoadCommandWithSize(const RorkHookMachHeader *header,
                                        size_t mappedSize,
                                        RorkHookLoadCommandVisitor visitor,
                                        void *context);

/// Returns the number of readable bytes remaining in the single VM region
/// containing `address`, or 0 when the address is unmapped or unreadable.
size_t RorkHookReadableMemoryLength(const void *address);

/// Returns the current VM protection for the region containing `address`.
///
/// `protectionOut` is written only when the address belongs to a mapped region.
bool RorkHookMemoryProtection(const void *address, vm_prot_t *protectionOut);

/// Returns whether a protected pointer write can proceed after attempting both
/// VM reprotection and the current thread's TPRO write window.
bool RorkHookProtectedPointerWriteIsAvailable(kern_return_t protectionResult,
                                              bool supportsTPRO,
                                              bool tproWindowOpened);

/// Returns whether TPRO is enabled for the current process. A known process
/// security configuration takes precedence over the legacy write-window probe;
/// hardware capability is required in either case.
bool RorkHookProcessUsesTPRO(bool hardwareSupportsTPRO,
                             bool processConfigurationKnown,
                             bool processConfigurationEnablesTPRO,
                             bool writeWindowProbeSucceeded);

/// Returns `true` when the nonempty range
/// `[address, address + length)` lies entirely inside one readable VM region.
///
/// This helper guards dereferences derived from Mach-O or cache metadata so a
/// malformed image is rejected instead of crossing into an unreadable mapping.
bool RorkHookMemoryIsReadable(const void *address, size_t length);

/// On arm64e, signs `pointer` with the function-pointer key when it points into
/// executable memory so the result is directly callable.
///
/// A pointer outside executable memory is returned bit-for-bit unchanged. The
/// function is also a no-op on targets without pointer authentication and for
/// `NULL`.
void *RorkHookSignPointerIfExecutable(void *pointer);

/// Returns the active dyld shared cache slide, or 0 when it cannot be read.
uintptr_t RorkHookSharedCacheSlide(void);

/// Resolves a shared-cache local symbol from an explicitly selected cache path.
///
/// A nonempty regular `.symbols` sidecar is preferred when it can be mapped;
/// otherwise local-symbol metadata is parsed from the main file. Both mappings
/// are released before return. A successful pointer is computed from the
/// caller-supplied slide and never borrows either temporary file mapping.
void *RorkHookFindSharedCacheSymbolAtPath(const char *mainPath,
                                          uintptr_t slide,
                                          const char *imagePath,
                                          const char *symbolName);

/// Parses shared-cache image and local-symbol metadata from bounded byte ranges.
///
/// The ranges are borrowed and may alias when symbols are stored inline in the
/// main cache. Every record and string is validated before it is read. Returns
/// the symbol value plus the caller-supplied slide on success, signing the
/// result when it addresses executable memory in the current process. Returns
/// `NULL` for malformed metadata, an absent image, or an absent symbol.
void *RorkHookFindSharedCacheSymbolInData(const void *mainData,
                                          size_t mainSize,
                                          const void *symbolsData,
                                          size_t symbolsSize,
                                          uintptr_t slide,
                                          const char *imagePath,
                                          const char *symbolName);

#endif /* RORK_HOOK_INTERNAL_H */
