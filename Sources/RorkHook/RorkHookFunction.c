#include "RorkHookFunction.h"

#include "RorkHookInternal.h"
#include "RorkHookMemory.h"

#include <libkern/OSCacheControl.h>
#include <string.h>
#include <unistd.h>

#if __has_feature(ptrauth_calls)
#include <ptrauth.h>
#endif

#if defined(__arm64__)

/// Encodes `MOVK <Xd>, #imm16, LSL #shift` (64-bit variant). Four of these at
/// shifts 0/16/32/48 fully materialize a 64-bit constant because every 16-bit
/// lane is written exactly once, so no leading `MOVZ` is required.
static inline uint32_t RorkHookEncodeMOVK(uint8_t destinationRegister,
                                         uint16_t immediate,
                                         uint8_t shift) {
    const uint32_t opcode = 0xf2800000u;
    const uint32_t halfword = (uint32_t)(shift / 16u) << 21;
    const uint32_t encodedImmediate = (uint32_t)immediate << 5;
    const uint32_t destination = destinationRegister & 0x1fu;
    return opcode | halfword | encodedImmediate | destination;
}

/// Encodes an unconditional branch to the address held in `sourceRegister`.
static inline uint32_t RorkHookEncodeBR(uint8_t sourceRegister) {
    const uint32_t opcode = 0xd61f0000u;
    const uint32_t source =
        ((uint32_t)sourceRegister & 0x1fu) << 5;
    return opcode | source;
}

#endif /* __arm64__ */

/// Builds the fixed-width arm64 absolute jump sequence used by destructive
/// detours.
///
/// Pointer-authentication decoration is removed before encoding because the
/// emitted `BR` instruction consumes a raw virtual address. No output is
/// written unless every precondition is satisfied.
size_t RorkHookBuildAbsoluteJump(const void *destination,
                                 uint32_t *instructions,
                                 size_t capacity) {
#if defined(__arm64__)
    if (destination == NULL ||
        instructions == NULL ||
        capacity < RORK_HOOK_ABSOLUTE_JUMP_WORDS) {
        return 0;
    }

    const void *stripped = destination;
#if __has_feature(ptrauth_calls)
    // Branch to the raw virtual address; the PAC bits in a signed pointer are
    // not part of the address and would send `BR x16` to a bogus location.
    stripped = ptrauth_strip(destination, ptrauth_key_function_pointer);
#endif
    uintptr_t target = (uintptr_t)stripped;

    // Scratch register x16 (IP0) is reserved for inter-procedural veneers, so
    // clobbering it across the branch is safe.
    const uint8_t scratchRegister = 16;
    instructions[0] =
        RorkHookEncodeMOVK(scratchRegister, (uint16_t)target, 0);
    instructions[1] =
        RorkHookEncodeMOVK(scratchRegister, (uint16_t)(target >> 16), 16);
    instructions[2] =
        RorkHookEncodeMOVK(scratchRegister, (uint16_t)(target >> 32), 32);
    instructions[3] =
        RorkHookEncodeMOVK(scratchRegister, (uint16_t)(target >> 48), 48);
    instructions[4] = RorkHookEncodeBR(scratchRegister);
    return RORK_HOOK_ABSOLUTE_JUMP_WORDS;
#else
    (void)destination;
    (void)instructions;
    (void)capacity;
    return 0;
#endif
}

/// Installs an arm64 absolute jump after validating the complete patch range.
///
/// The target is confined to one VM page so its original protection can be
/// queried and restored as a single value. If restoration fails after the new
/// instructions are written, the original bytes are restored before returning
/// the VM error.
kern_return_t RorkHookReplaceFunctionWithSize(void *function,
                                              size_t patchableBytes,
                                              void *replacement) {
#if defined(__arm64__)
    const size_t patchSize = sizeof(uint32_t) * RORK_HOOK_ABSOLUTE_JUMP_WORDS;
    if (function == NULL ||
        replacement == NULL ||
        patchableBytes < patchSize) {
        return KERN_INVALID_ARGUMENT;
    }

    void *target = function;
#if __has_feature(ptrauth_calls)
    target = ptrauth_strip(function, ptrauth_key_function_pointer);
#endif
    const uintptr_t targetAddress = (uintptr_t)target;
    const int nativePageSize = getpagesize();
    if (nativePageSize <= 0) {
        return KERN_INVALID_ARGUMENT;
    }
    const size_t pageSize = (size_t)nativePageSize;
    if (targetAddress % sizeof(uint32_t) != 0 ||
        pageSize < patchSize ||
        targetAddress % pageSize > pageSize - patchSize) {
        return KERN_INVALID_ARGUMENT;
    }
    if (!RorkHookMemoryIsReadable(target, patchSize)) {
        return KERN_INVALID_ADDRESS;
    }

    vm_prot_t originalProtection = 0;
    if (!RorkHookMemoryProtection(target, &originalProtection) ||
        !(originalProtection & VM_PROT_EXECUTE)) {
        return KERN_PROTECTION_FAILURE;
    }

    uint32_t instructions[RORK_HOOK_ABSOLUTE_JUMP_WORDS];
    if (RorkHookBuildAbsoluteJump(
            replacement,
            instructions,
            RORK_HOOK_ABSOLUTE_JUMP_WORDS) == 0) {
        return KERN_NOT_SUPPORTED;
    }
    uint32_t originalInstructions[RORK_HOOK_ABSOLUTE_JUMP_WORDS];
    memcpy(originalInstructions, target, patchSize);

    kern_return_t result = RorkHookMakeMemoryWritable(
        (vm_address_t)targetAddress,
        (vm_size_t)patchSize);
    if (result != KERN_SUCCESS) {
        return result;
    }

    memcpy(target, instructions, patchSize);
    sys_dcache_flush(target, patchSize);

    result = RorkHookProtectMemory(
        (vm_address_t)targetAddress,
        (vm_size_t)patchSize,
        originalProtection);
    if (result != KERN_SUCCESS) {
        memcpy(target, originalInstructions, patchSize);
        sys_dcache_flush(target, patchSize);
        (void)RorkHookProtectMemory(
            (vm_address_t)targetAddress,
            (vm_size_t)patchSize,
            originalProtection);
        sys_icache_invalidate(target, patchSize);
        return result;
    }

    sys_icache_invalidate(target, patchSize);
    return KERN_SUCCESS;
#else
    (void)function;
    (void)patchableBytes;
    (void)replacement;
    return KERN_NOT_SUPPORTED;
#endif
}

/// Applies the legacy prologue-size assumption through the checked detour
/// implementation.
kern_return_t RorkHookReplaceFunction(void *function, void *replacement) {
    return RorkHookReplaceFunctionWithSize(
        function,
        sizeof(uint32_t) * RORK_HOOK_ABSOLUTE_JUMP_WORDS,
        replacement);
}
