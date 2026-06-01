#include "RorkHookRebind.h"

#include "RorkHookInternal.h"
#include "RorkHookMemory.h"

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <os/lock.h>
#include <stdlib.h>
#include <string.h>

#if __has_feature(ptrauth_calls)
#include <ptrauth.h>
#endif

#pragma mark - Pointer rewriting

/// Strips any pointer authentication from `pointer`, yielding the raw virtual
/// address used for slot comparisons.
static void *RorkHookStripPointer(void *pointer) {
#if __has_feature(ptrauth_calls)
    return ptrauth_strip(ptrauth_auth_function(pointer, ptrauth_key_function_pointer, 0),
                         ptrauth_key_function_pointer);
#else
    return pointer;
#endif
}

/// Writes `value` into a symbol-pointer slot that may live in read-only and/or
/// TPRO-hardened memory, restoring the original protection afterwards.
static bool RorkHookStoreSlot(void **slot, void *value) {
    if (RorkHookStoreProtectedPointer(slot, value, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY)) {
        return true;
    }
    return RorkHookStoreProtectedPointer(slot, value, VM_PROT_READ | VM_PROT_WRITE);
}

/// Rewrites every slot in one symbol-pointer section that currently resolves to
/// `replaceeRaw` so it points at `replacement`, re-signing authenticated slots.
static void RorkHookRebindSection(const RorkHookSection *section,
                                  intptr_t slide,
                                  void *replaceeRaw,
                                  void *replacement) {
    void **slots = (void **)((uintptr_t)section->addr + (uintptr_t)slide);
    size_t slotCount = (size_t)section->size / sizeof(void *);
    if (slotCount == 0 || !RorkHookMemoryIsReadable(slots, (size_t)section->size)) {
        return;
    }

#if __has_feature(ptrauth_calls)
    bool authenticated = strncmp(section->sectname, "__auth_got", sizeof(section->sectname)) == 0;
#endif

    for (size_t index = 0; index < slotCount; index += 1) {
        void *current = slots[index];
        if (current == NULL) {
            continue;
        }

        void *resolved = current;
#if __has_feature(ptrauth_calls)
        if (authenticated) {
            resolved = ptrauth_strip(
                ptrauth_auth_function(current, ptrauth_key_function_pointer, &slots[index]),
                ptrauth_key_function_pointer);
        }
#endif
        if (resolved != replaceeRaw) {
            continue;
        }

        void *finalValue = replacement;
#if __has_feature(ptrauth_calls)
        if (authenticated) {
            // The slot expects an IA-signed pointer discriminated by its own
            // address; re-sign the plain replacement to match.
            finalValue = ptrauth_auth_and_resign(replacement, ptrauth_key_function_pointer, 0,
                                                 ptrauth_key_function_pointer, &slots[index]);
        } else {
            finalValue = ptrauth_strip(replacement, ptrauth_key_function_pointer);
        }
#endif
        RorkHookStoreSlot(&slots[index], finalValue);
    }
}

typedef struct {
    intptr_t slide;
    void *replaceeRaw;
    void *replacement;
} RorkHookRebindImageContext;

/// Visitor that scans writable pointer sections in one Mach-O image.
static bool RorkHookRebindImageLoadCommand(const struct load_command *command,
                                           uint32_t index,
                                           void *contextRaw) {
    (void)index;

    if (command->cmd != RORK_HOOK_LC_SEGMENT || command->cmdsize < sizeof(RorkHookSegmentCommand)) {
        return true;
    }

    const RorkHookSegmentCommand *segment = (const RorkHookSegmentCommand *)command;
    uint64_t sectionsEnd = (uint64_t)sizeof(*segment) + ((uint64_t)segment->nsects * sizeof(RorkHookSection));
    if (sectionsEnd > command->cmdsize) {
        return true;
    }

    bool dataSegment =
        strncmp(segment->segname, "__AUTH_CONST", sizeof(segment->segname)) == 0 ||
        strncmp(segment->segname, "__DATA_CONST", sizeof(segment->segname)) == 0 ||
        strncmp(segment->segname, SEG_DATA, sizeof(segment->segname)) == 0;
    if (!dataSegment) {
        return true;
    }

    RorkHookRebindImageContext *context = (RorkHookRebindImageContext *)contextRaw;
    const RorkHookSection *sections =
        (const RorkHookSection *)((uintptr_t)segment + sizeof(*segment));
    for (uint32_t sectionIndex = 0; sectionIndex < segment->nsects; sectionIndex += 1) {
        uint32_t type = sections[sectionIndex].flags & SECTION_TYPE;
        if (type == S_LAZY_SYMBOL_POINTERS || type == S_NON_LAZY_SYMBOL_POINTERS) {
            RorkHookRebindSection(&sections[sectionIndex],
                                  context->slide,
                                  context->replaceeRaw,
                                  context->replacement);
        }
    }
    return true;
}

/// Rebinds matching symbol-pointer slots in a single loaded image.
void RorkHookRebindSymbolInImage(const RorkHookMachHeader *header,
                                 void *replacee,
                                 void *replacement) {
    if (header == NULL || replacee == NULL || replacement == NULL) {
        return;
    }

    intptr_t slide = RorkHookImageSlide(header, NULL);
    void *replaceeRaw = RorkHookStripPointer(replacee);
    RorkHookRebindImageContext context = {
        .slide = slide,
        .replaceeRaw = replaceeRaw,
        .replacement = replacement,
    };
    RorkHookForEachLoadCommand(header, RorkHookRebindImageLoadCommand, &context);
}

#pragma mark - Global rebind registry

typedef struct {
    const RorkHookMachHeader *sourceHeader;
    void *replacee;
    void *replacement;
    RorkHookImageFilter filter;
} RorkHookGlobalRebind;

static os_unfair_lock gRebindLock = OS_UNFAIR_LOCK_INIT;
static RorkHookGlobalRebind *gRebinds = NULL;
static uint32_t gRebindCount = 0;
static bool gAddImageCallbackRegistered = false;

/// Applies a single registered rebind to one image, honouring the source-image
/// exclusion and the optional filter.
static void RorkHookApplyRebind(const RorkHookGlobalRebind *rebind, const RorkHookMachHeader *header) {
    if (header == rebind->sourceHeader) {
        return;
    }
    if (rebind->filter != NULL && !rebind->filter(header)) {
        return;
    }
    RorkHookRebindSymbolInImage(header, rebind->replacee, rebind->replacement);
}

/// dyld image-load callback: applies every registered rebind to a newly mapped
/// image. Also fires once per already-loaded image when first registered.
static void RorkHookHandleImageAdded(const struct mach_header *machHeader, intptr_t slide) {
    (void)slide;
    const RorkHookMachHeader *header = (const RorkHookMachHeader *)machHeader;

    // Snapshot the registry so the section rewriting runs without the lock held.
    os_unfair_lock_lock(&gRebindLock);
    uint32_t count = gRebindCount;
    RorkHookGlobalRebind *snapshot = NULL;
    if (count != 0) {
        snapshot = (RorkHookGlobalRebind *)malloc(sizeof(*snapshot) * count);
        if (snapshot != NULL) {
            memcpy(snapshot, gRebinds, sizeof(*snapshot) * count);
        }
    }
    os_unfair_lock_unlock(&gRebindLock);

    if (snapshot == NULL) {
        return;
    }
    for (uint32_t index = 0; index < count; index += 1) {
        RorkHookApplyRebind(&snapshot[index], header);
    }
    free(snapshot);
}

/// Registers a process-wide rebind and applies it to current and future images.
bool RorkHookRebindSymbolGlobally(void *replacee, void *replacement, RorkHookImageFilter filter) {
    if (replacee == NULL || replacement == NULL) {
        return false;
    }

    // Identify the image that defines the replacement so it keeps calling the
    // original implementation and is never rebound onto itself.
    Dl_info replacementInfo;
    if (dladdr(replacement, &replacementInfo) == 0 || replacementInfo.dli_fbase == NULL) {
        return false;
    }
    const RorkHookMachHeader *sourceHeader = (const RorkHookMachHeader *)replacementInfo.dli_fbase;
    uint32_t imageCount = _dyld_image_count();

    os_unfair_lock_lock(&gRebindLock);
    RorkHookGlobalRebind *resized =
        (RorkHookGlobalRebind *)realloc(gRebinds, sizeof(*resized) * (gRebindCount + 1));
    if (resized == NULL) {
        os_unfair_lock_unlock(&gRebindLock);
        return false;
    }
    gRebinds = resized;
    gRebinds[gRebindCount] = (RorkHookGlobalRebind){sourceHeader, replacee, replacement, filter};
    gRebindCount += 1;
    bool mustRegister = !gAddImageCallbackRegistered;
    gAddImageCallbackRegistered = true;
    RorkHookGlobalRebind entry = gRebinds[gRebindCount - 1];
    os_unfair_lock_unlock(&gRebindLock);

    if (mustRegister) {
        // Registering fires the callback synchronously for every already-loaded
        // image, applying this first entry; it also covers future images.
        _dyld_register_func_for_add_image(&RorkHookHandleImageAdded);
    } else {
        // The callback only fires for images loaded after registration, so apply
        // this new entry to the images that are already present.
        for (uint32_t index = 0; index < imageCount; index += 1) {
            RorkHookApplyRebind(&entry, (const RorkHookMachHeader *)_dyld_get_image_header(index));
        }
    }
    return true;
}
