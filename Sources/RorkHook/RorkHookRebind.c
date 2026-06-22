#include "RorkHookRebind.h"

#include "RorkHookInternal.h"
#include "RorkHookMemory.h"

#include <dlfcn.h>
#include <dispatch/dispatch.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <stdatomic.h>
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
    return ptrauth_strip(pointer, ptrauth_key_function_pointer);
#else
    return pointer;
#endif
}

/// Writes `value` into a symbol-pointer slot that may live in read-only and/or
/// TPRO-hardened memory, restoring the original protection afterwards.
///
/// `VM_PROT_COPY` also permits ordinary writable mappings and avoids a second
/// attempt after an ambiguous failure: the underlying API can return `false`
/// when the store completed but protection restoration did not.
static bool RorkHookStoreSlot(void **slot, void *value) {
    return RorkHookStoreProtectedPointer(
        slot,
        value,
        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
}

/// State threaded through one image's section scan: the image being rebound,
/// the raw replacee to match, and the replacement to install.
typedef struct RorkHookRebindImageContext {
    const RorkHookMachHeader *header;
    void *replaceeRaw;
    void *replacement;
} RorkHookRebindImageContext;

/// Reads a 16-byte Mach-O segment/section name into a NUL-terminated buffer.
static void RorkHookCopyName(char out[17], const char name[16]) {
    memcpy(out, name, 16);
    out[16] = '\0';
}

/// Rewrites every slot in one symbol-pointer section that currently resolves to
/// the context's replacee so it points at the replacement, re-signing
/// authenticated slots.
static void RorkHookRebindSection(const RorkHookRebindImageContext *context,
                                  const RorkHookSection *section) {
    if (context == NULL || section == NULL) {
        return;
    }

    // Resolve the section's mapped address with getsectiondata rather than
    // `section->addr + slide`. In the dyld shared cache, __TEXT and the data
    // segments are split into separate regions with different offsets, so the
    // __TEXT slide does not apply to a data section's link-time address and the
    // computed pointer lands in an unmapped cache hole. getsectiondata resolves
    // the real runtime address (and size) for both cache and on-disk images.
    char segname[17];
    char sectname[17];
    RorkHookCopyName(segname, section->segname);
    RorkHookCopyName(sectname, section->sectname);

    unsigned long sectionSize = 0;
    void **slots = (void **)getsectiondata(
        (const struct mach_header_64 *)context->header,
        segname,
        sectname,
        &sectionSize);
    size_t slotCount = (size_t)sectionSize / sizeof(void *);
    if (slots == NULL ||
        slotCount == 0 ||
        sectionSize % sizeof(void *) != 0 ||
        !RorkHookMemoryIsReadable(slots, (size_t)sectionSize)) {
        return;
    }

#if __has_feature(ptrauth_calls)
    bool authenticated = strcmp(sectname, "__auth_got") == 0;
#endif

    for (size_t index = 0; index < slotCount; index += 1) {
        void *current = slots[index];
        if (current == NULL) {
            continue;
        }

        // A slot may carry pointer-authentication decoration even when it does
        // not live in `__auth_got`. Stripping does not authenticate the pointer,
        // so it is safe for foreign PAC schemas and sufficient for comparing
        // the underlying virtual address.
        void *resolved = RorkHookStripPointer(current);
        if (resolved != context->replaceeRaw) {
            continue;
        }

        void *finalValue = context->replacement;
#if __has_feature(ptrauth_calls)
        if (authenticated) {
            // Standard authenticated function-pointer slots use the IA key with
            // address diversity. Sign the raw replacement for this exact slot
            // without first authenticating a type-erased caller pointer.
            void *rawReplacement =
                ptrauth_strip(context->replacement, ptrauth_key_function_pointer);
            finalValue = ptrauth_sign_unauthenticated(
                rawReplacement,
                ptrauth_key_function_pointer,
                &slots[index]);
        } else {
            finalValue = RorkHookStripPointer(context->replacement);
        }
#endif
        // This API is intentionally best-effort. A slot that cannot be made
        // writable remains unchanged while scanning continues.
        (void)RorkHookStoreSlot(&slots[index], finalValue);
    }
}

/// Visitor that scans writable pointer sections in one Mach-O image.
static bool RorkHookRebindImageLoadCommand(const struct load_command *command,
                                           uint32_t index,
                                           void *contextRaw) {
    (void)index;

    if (command == NULL ||
        contextRaw == NULL ||
        command->cmd != RORK_HOOK_LC_SEGMENT ||
        command->cmdsize < sizeof(RorkHookSegmentCommand)) {
        return true;
    }

    const RorkHookSegmentCommand *segment =
        (const RorkHookSegmentCommand *)(const void *)command;
    size_t sectionsSize = 0;
    size_t sectionsEnd = 0;
    if (__builtin_mul_overflow(
            (size_t)segment->nsects,
            sizeof(RorkHookSection),
            &sectionsSize) ||
        __builtin_add_overflow(
            sizeof(*segment),
            sectionsSize,
            &sectionsEnd) ||
        sectionsEnd > command->cmdsize) {
        return true;
    }

    RorkHookRebindImageContext *context =
        (RorkHookRebindImageContext *)contextRaw;
    const RorkHookSection *sections =
        (const RorkHookSection *)(const void *)(
            (const uint8_t *)segment + sizeof(*segment));
    for (uint32_t sectionIndex = 0;
         sectionIndex < segment->nsects;
         sectionIndex += 1) {
        uint32_t type = sections[sectionIndex].flags & SECTION_TYPE;
        if (type == S_LAZY_SYMBOL_POINTERS ||
            type == S_NON_LAZY_SYMBOL_POINTERS) {
            RorkHookRebindSection(context, &sections[sectionIndex]);
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

    void *replaceeRaw = RorkHookStripPointer(replacee);
    RorkHookRebindImageContext context = {
        .header = header,
        .replaceeRaw = replaceeRaw,
        .replacement = replacement,
    };
    (void)RorkHookForEachLoadCommand(
        header,
        RorkHookRebindImageLoadCommand,
        &context);
}

#pragma mark - Global rebind registry

/// Immutable process-lifetime registration published through the lock-free
/// global list.
///
/// Entries are never removed, so readers running under dyld's loader lock can
/// traverse the list without reclamation or external synchronization.
typedef struct RorkHookGlobalRebind {
    const RorkHookMachHeader *sourceHeader;
    void *replacee;
    void *replacement;
    RorkHookImageFilter filter;
    struct RorkHookGlobalRebind *next;
} RorkHookGlobalRebind;

/// Head of the immutable process-lifetime registration list.
static _Atomic(RorkHookGlobalRebind *) gRebinds = NULL;

/// Ensures dyld receives exactly one process-wide image-load callback.
static dispatch_once_t gAddImageCallbackOnce;

/// Applies a single registered rebind to one image, honoring the source-image
/// exclusion and the optional filter.
static void RorkHookApplyRebind(const RorkHookGlobalRebind *rebind,
                                const RorkHookMachHeader *header) {
    if (rebind == NULL || header == NULL) {
        return;
    }
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
static void RorkHookHandleImageAdded(const struct mach_header *machHeader,
                                     intptr_t slide) {
    (void)slide;
    const RorkHookMachHeader *header = (const RorkHookMachHeader *)machHeader;
    RorkHookGlobalRebind *rebind =
        atomic_load_explicit(&gRebinds, memory_order_acquire);
    while (rebind != NULL) {
        RorkHookApplyRebind(rebind, header);
        rebind = rebind->next;
    }
}

/// Publishes a process-lifetime rebind before registering the dyld callback.
///
/// The release/acquire list ordering makes each immutable entry visible to
/// concurrent image-load callbacks. Registering the callback also visits all
/// images already loaded by dyld; the explicit enumeration closes the remaining
/// interleavings between concurrent registrations.
bool RorkHookRebindSymbolGlobally(void *replacee,
                                  void *replacement,
                                  RorkHookImageFilter filter) {
    if (replacee == NULL || replacement == NULL) {
        return false;
    }

    // Identify the image that defines the replacement so it keeps calling the
    // original implementation and is never rebound onto itself.
    Dl_info replacementInfo;
    void *replacementRaw = RorkHookStripPointer(replacement);
    if (dladdr(replacementRaw, &replacementInfo) == 0 ||
        replacementInfo.dli_fbase == NULL) {
        return false;
    }
    const RorkHookMachHeader *sourceHeader =
        (const RorkHookMachHeader *)replacementInfo.dli_fbase;
    RorkHookGlobalRebind *entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        return false;
    }
    *entry = (RorkHookGlobalRebind){
        .sourceHeader = sourceHeader,
        .replacee = replacee,
        .replacement = replacement,
        .filter = filter,
        .next = NULL,
    };

    RorkHookGlobalRebind *head =
        atomic_load_explicit(&gRebinds, memory_order_relaxed);
    do {
        entry->next = head;
    } while (!atomic_compare_exchange_weak_explicit(
        &gRebinds,
        &head,
        entry,
        memory_order_release,
        memory_order_relaxed));

    dispatch_once(&gAddImageCallbackOnce, ^{
        _dyld_register_func_for_add_image(&RorkHookHandleImageAdded);
    });

    // Publishing the entry before taking this count closes the registration
    // gap: an image loaded earlier appears in this enumeration, while one loaded
    // later is handled by the already-registered callback.
    uint32_t imageCount = _dyld_image_count();
    for (uint32_t index = 0; index < imageCount; index += 1) {
        const RorkHookMachHeader *header =
            (const RorkHookMachHeader *)_dyld_get_image_header(index);
        if (header != NULL) {
            RorkHookApplyRebind(entry, header);
        }
    }
    return true;
}
