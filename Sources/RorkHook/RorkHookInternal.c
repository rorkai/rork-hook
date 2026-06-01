#include "RorkHookInternal.h"

#include <mach/mach.h>
#include <mach-o/dyld_images.h>
#include <string.h>

#if __has_feature(ptrauth_calls)
#include <ptrauth.h>
#endif

/// Walks a Mach-O header's load commands without trusting malformed sizes.
bool RorkHookForEachLoadCommand(const RorkHookMachHeader *header,
                                RorkHookLoadCommandVisitor visitor,
                                void *context) {
    if (header == NULL || visitor == NULL) {
        return false;
    }

    uintptr_t commandAddress = (uintptr_t)header + sizeof(*header);
    uint32_t consumed = 0;
    for (uint32_t index = 0; index < header->ncmds; index += 1) {
        if (consumed > header->sizeofcmds) {
            return false;
        }

        uint32_t remaining = header->sizeofcmds - consumed;
        if (remaining < sizeof(struct load_command)) {
            return false;
        }

        const struct load_command *command = (const struct load_command *)(commandAddress + consumed);
        if (command->cmdsize < sizeof(struct load_command) || command->cmdsize > remaining) {
            return false;
        }

        if (!visitor(command, index, context)) {
            return true;
        }

        consumed += command->cmdsize;
    }
    return true;
}

typedef struct {
    const RorkHookMachHeader *header;
    intptr_t slide;
    bool found;
} RorkHookImageSlideContext;

/// Visitor that records the segment mapping the Mach-O header.
static bool RorkHookImageSlideLoadCommand(const struct load_command *command,
                                          uint32_t index,
                                          void *contextRaw) {
    (void)index;

    RorkHookImageSlideContext *context = (RorkHookImageSlideContext *)contextRaw;
    if (command->cmd != RORK_HOOK_LC_SEGMENT || command->cmdsize < sizeof(RorkHookSegmentCommand)) {
        return true;
    }

    const RorkHookSegmentCommand *segment = (const RorkHookSegmentCommand *)command;
    // __TEXT is the segment that maps the header itself: file offset 0 with a
    // non-zero file size. __PAGEZERO has a zero file size and is skipped, which
    // is why this is correct for executables too.
    if (segment->fileoff == 0 && segment->filesize != 0) {
        context->slide = (intptr_t)((uintptr_t)context->header - (uintptr_t)segment->vmaddr);
        context->found = true;
        return false;
    }
    return true;
}

/// Resolves the VM slide for both executables and dylibs.
intptr_t RorkHookImageSlide(const RorkHookMachHeader *header, bool *resolved) {
    if (resolved) {
        *resolved = false;
    }
    if (header == NULL) {
        return 0;
    }

    RorkHookImageSlideContext context = {
        .header = header,
        .slide = 0,
        .found = false,
    };
    RorkHookForEachLoadCommand(header, RorkHookImageSlideLoadCommand, &context);
    if (context.found) {
        if (resolved) {
            *resolved = true;
        }
        return context.slide;
    }

    return 0;
}

/// Queries the VM region containing `address` and optionally returns its bounds.
static bool RorkHookQueryRegion(const void *address,
                                vm_address_t *regionAddressOut,
                                vm_size_t *regionSizeOut,
                                vm_prot_t *protectionOut) {
    if (address == NULL) {
        return false;
    }

    vm_address_t regionAddress = (vm_address_t)(uintptr_t)address;
    vm_size_t regionSize = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t objectName = MACH_PORT_NULL;
    kern_return_t result = vm_region_64(mach_task_self(),
                                        &regionAddress,
                                        &regionSize,
                                        VM_REGION_BASIC_INFO_64,
                                        (vm_region_info_t)&info,
                                        &count,
                                        &objectName);
    if (objectName != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), objectName);
    }
    if (result != KERN_SUCCESS) {
        return false;
    }

    if (regionAddressOut != NULL) {
        *regionAddressOut = regionAddress;
    }
    if (regionSizeOut != NULL) {
        *regionSizeOut = regionSize;
    }
    if (protectionOut != NULL) {
        *protectionOut = info.protection;
    }
    return true;
}

/// Verifies that a byte range is fully contained in one readable VM region.
bool RorkHookMemoryIsReadable(const void *address, size_t length) {
    if (address == NULL || length == 0) {
        return false;
    }

    vm_address_t regionAddress = 0;
    vm_size_t regionSize = 0;
    vm_prot_t protection = 0;
    if (!RorkHookQueryRegion(address, &regionAddress, &regionSize, &protection) ||
        !(protection & VM_PROT_READ)) {
        return false;
    }

    uintptr_t start = (uintptr_t)address;
    uintptr_t end = start + length;
    uintptr_t regionStart = (uintptr_t)regionAddress;
    uintptr_t regionEnd = regionStart + (uintptr_t)regionSize;
    return end >= start && start >= regionStart && end <= regionEnd;
}

/// Re-signs executable addresses for direct calls on pointer-authenticated targets.
void *RorkHookSignPointerIfExecutable(void *pointer) {
    if (pointer == NULL) {
        return NULL;
    }

#if __has_feature(ptrauth_calls)
    vm_prot_t protection = 0;
    if (RorkHookQueryRegion(pointer, NULL, NULL, &protection) &&
        (protection & VM_PROT_EXECUTE)) {
        return ptrauth_sign_unauthenticated(pointer, ptrauth_key_function_pointer, 0);
    }
#endif
    return pointer;
}

/// Reads dyld's published shared-cache slide from the current task metadata.
uintptr_t RorkHookSharedCacheSlide(void) {
    task_dyld_info_data_t dyldInfo;
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    kern_return_t result = task_info(mach_task_self(),
                                     TASK_DYLD_INFO,
                                     (task_info_t)&dyldInfo,
                                     &count);
    if (result != KERN_SUCCESS || dyldInfo.all_image_info_addr == 0) {
        return 0;
    }

    struct dyld_all_image_infos *imageInfos =
        (struct dyld_all_image_infos *)(uintptr_t)dyldInfo.all_image_info_addr;
    if (!RorkHookMemoryIsReadable(imageInfos, sizeof(*imageInfos)) || imageInfos->version < 12) {
        return 0;
    }
    return (uintptr_t)imageInfos->sharedCacheSlide;
}
