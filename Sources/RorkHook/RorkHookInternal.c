#include "RorkHookInternal.h"

#include <mach/mach.h>
#include <mach-o/dyld_images.h>
#include <string.h>

#if __has_feature(ptrauth_calls)
#include <ptrauth.h>
#endif

#if defined(__LP64__)
#define RORK_HOOK_LOAD_COMMAND_ALIGNMENT sizeof(uint64_t)
#else
#define RORK_HOOK_LOAD_COMMAND_ALIGNMENT sizeof(uint32_t)
#endif

/// Validates a bounded Mach-O load-command table and returns its first byte.
///
/// Validation is deliberately separate from callback delivery so a visitor
/// with side effects cannot partially process an image whose trailing commands
/// are malformed.
static bool RorkHookValidateLoadCommands(
    const RorkHookMachHeader *header,
    size_t mappedSize,
    const uint8_t **commandBytesOut) {
    if (header == NULL ||
        commandBytesOut == NULL ||
        mappedSize < sizeof(*header)) {
        return false;
    }

    if (header->magic != RORK_HOOK_MH_MAGIC ||
        header->sizeofcmds > mappedSize - sizeof(*header) ||
        header->ncmds > header->sizeofcmds / sizeof(struct load_command)) {
        return false;
    }

    const uint8_t *commandBytes = (const uint8_t *)header + sizeof(*header);
    uint32_t consumed = 0;
    for (uint32_t index = 0; index < header->ncmds; index += 1) {
        if (consumed > header->sizeofcmds) {
            return false;
        }

        uint32_t remaining = header->sizeofcmds - consumed;
        if (remaining < sizeof(struct load_command)) {
            return false;
        }

        struct load_command command;
        memcpy(&command, commandBytes + consumed, sizeof(command));
        if (command.cmdsize < sizeof(struct load_command) ||
            command.cmdsize % RORK_HOOK_LOAD_COMMAND_ALIGNMENT != 0 ||
            command.cmdsize > remaining) {
            return false;
        }
        consumed += command.cmdsize;
    }
    if (consumed != header->sizeofcmds) {
        return false;
    }

    *commandBytesOut = commandBytes;
    return true;
}

/// Delivers a previously validated load-command table to the caller's visitor.
bool RorkHookForEachLoadCommandWithSize(const RorkHookMachHeader *header,
                                        size_t mappedSize,
                                        RorkHookLoadCommandVisitor visitor,
                                        void *context) {
    if (visitor == NULL) {
        return false;
    }

    const uint8_t *commandBytes = NULL;
    if (!RorkHookValidateLoadCommands(header, mappedSize, &commandBytes)) {
        return false;
    }

    uint32_t consumed = 0;
    for (uint32_t index = 0; index < header->ncmds; index += 1) {
        const struct load_command *command =
            (const struct load_command *)(const void *)(commandBytes + consumed);
        if (!visitor(command, index, context)) {
            break;
        }
        consumed += command->cmdsize;
    }
    return true;
}

/// Walks a live image's load commands within the readable header VM region.
bool RorkHookForEachLoadCommand(const RorkHookMachHeader *header,
                                RorkHookLoadCommandVisitor visitor,
                                void *context) {
    return RorkHookForEachLoadCommandWithSize(
        header,
        RorkHookReadableMemoryLength(header),
        visitor,
        context);
}

/// State accumulated while locating the segment that maps a live image header.
typedef struct RorkHookImageSlideContext {
    const RorkHookMachHeader *header;
    intptr_t slide;
    bool found;
} RorkHookImageSlideContext;

/// Computes `left - right` when the result fits in `intptr_t`.
static bool RorkHookAddressDifference(uintptr_t left,
                                      uintptr_t right,
                                      intptr_t *differenceOut) {
    if (differenceOut == NULL) {
        return false;
    }

    if (left >= right) {
        uintptr_t difference = left - right;
        if (difference > INTPTR_MAX) {
            return false;
        }
        *differenceOut = (intptr_t)difference;
        return true;
    }

    uintptr_t magnitude = right - left;
    uintptr_t minimumMagnitude = (uintptr_t)INTPTR_MAX + 1;
    if (magnitude > minimumMagnitude) {
        return false;
    }
    *differenceOut = magnitude == minimumMagnitude
        ? INTPTR_MIN
        : -(intptr_t)magnitude;
    return true;
}

/// Records the slide implied by the segment whose file bytes contain the header.
static bool RorkHookImageSlideLoadCommand(const struct load_command *command,
                                          uint32_t index,
                                          void *contextRaw) {
    (void)index;

    if (command == NULL || contextRaw == NULL) {
        return false;
    }

    RorkHookImageSlideContext *context =
        (RorkHookImageSlideContext *)contextRaw;
    if (command->cmd != RORK_HOOK_LC_SEGMENT ||
        command->cmdsize < sizeof(RorkHookSegmentCommand)) {
        return true;
    }

    const RorkHookSegmentCommand *segment =
        (const RorkHookSegmentCommand *)(const void *)command;
    // The segment mapping the header has file offset zero and nonzero file
    // content. This excludes `__PAGEZERO`, whose file size is zero, and works
    // for executables whose `__TEXT` VM address is not zero.
    if (segment->fileoff == 0 && segment->filesize != 0) {
        if (segment->vmaddr > UINTPTR_MAX ||
            !RorkHookAddressDifference(
                (uintptr_t)context->header,
                (uintptr_t)segment->vmaddr,
                &context->slide)) {
            return false;
        }
        context->found = true;
        return false;
    }
    return true;
}

/// Resolves the VM slide for both executables and dynamically loaded libraries.
intptr_t RorkHookImageSlide(const RorkHookMachHeader *header, bool *resolved) {
    if (resolved != NULL) {
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
    bool valid = RorkHookForEachLoadCommand(
        header,
        RorkHookImageSlideLoadCommand,
        &context);
    if (valid && context.found) {
        if (resolved != NULL) {
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
    vm_region_basic_info_data_64_t info = {0};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t objectName = MACH_PORT_NULL;
    kern_return_t result = vm_region_64(
        mach_task_self(),
        &regionAddress,
        &regionSize,
        VM_REGION_BASIC_INFO_64,
        (vm_region_info_t)&info,
        &count,
        &objectName);
    if (objectName != MACH_PORT_NULL) {
        (void)mach_port_deallocate(mach_task_self(), objectName);
    }
    if (result != KERN_SUCCESS) {
        return false;
    }

    uintptr_t requestedAddress = (uintptr_t)address;
    uintptr_t regionStart = (uintptr_t)regionAddress;
    uintptr_t regionEnd = regionStart + (uintptr_t)regionSize;
    if (regionEnd < regionStart ||
        requestedAddress < regionStart ||
        requestedAddress >= regionEnd) {
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

/// Exposes a region's current protection to implementation files that need to
/// restore it after a temporary mutation.
bool RorkHookMemoryProtection(const void *address, vm_prot_t *protectionOut) {
    return protectionOut != NULL &&
        RorkHookQueryRegion(address, NULL, NULL, protectionOut);
}

/// Reports the readable suffix of the VM region containing an address.
size_t RorkHookReadableMemoryLength(const void *address) {
    vm_address_t regionAddress = 0;
    vm_size_t regionSize = 0;
    vm_prot_t protection = 0;
    if (!RorkHookQueryRegion(address, &regionAddress, &regionSize, &protection) ||
        !(protection & VM_PROT_READ)) {
        return 0;
    }

    uintptr_t start = (uintptr_t)address;
    uintptr_t regionStart = (uintptr_t)regionAddress;
    uintptr_t regionEnd = regionStart + (uintptr_t)regionSize;
    if (regionEnd < regionStart || start < regionStart || start >= regionEnd) {
        return 0;
    }

    return (size_t)(regionEnd - start);
}

/// Verifies that a nonempty byte range fits in one readable VM region.
bool RorkHookMemoryIsReadable(const void *address, size_t length) {
    return length != 0 && RorkHookReadableMemoryLength(address) >= length;
}

/// Re-signs executable addresses for direct calls on pointer-authenticated
/// targets.
///
/// The raw address is used only for the VM query and signature input. A pointer
/// outside executable memory is returned bit-for-bit unchanged so callers do
/// not lose authentication metadata attached to non-code pointers.
void *RorkHookSignPointerIfExecutable(void *pointer) {
    if (pointer == NULL) {
        return NULL;
    }

#if __has_feature(ptrauth_calls)
    void *rawPointer =
        ptrauth_strip(pointer, ptrauth_key_function_pointer);
    vm_prot_t protection = 0;
    if (RorkHookQueryRegion(rawPointer, NULL, NULL, &protection) &&
        (protection & VM_PROT_EXECUTE)) {
        return ptrauth_sign_unauthenticated(
            rawPointer,
            ptrauth_key_function_pointer,
            0);
    }
#endif
    return pointer;
}

/// Reads dyld's published shared-cache slide from the current task metadata.
uintptr_t RorkHookSharedCacheSlide(void) {
    task_dyld_info_data_t dyldInfo = {0};
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    kern_return_t result = task_info(
        mach_task_self(),
        TASK_DYLD_INFO,
        (task_info_t)&dyldInfo,
        &count);
    if (result != KERN_SUCCESS || dyldInfo.all_image_info_addr == 0) {
        return 0;
    }

    struct dyld_all_image_infos *imageInfos =
        (struct dyld_all_image_infos *)(uintptr_t)dyldInfo.all_image_info_addr;
    if (!RorkHookMemoryIsReadable(imageInfos, sizeof(*imageInfos)) ||
        imageInfos->version < 12) {
        return 0;
    }
    return (uintptr_t)imageInfos->sharedCacheSlide;
}
