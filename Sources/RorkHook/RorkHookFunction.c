#include "RorkHookFunction.h"

#include "RorkHookMemory.h"

#include <libkern/OSCacheControl.h>
#include <string.h>

#if __has_feature(ptrauth_calls)
#include <ptrauth.h>
#endif

#if defined(__arm64__)

/// Encodes `MOVK <Xd>, #imm16, LSL #shift` (64-bit variant). Four of these at
/// shifts 0/16/32/48 fully materialise a 64-bit constant because every 16-bit
/// lane is written exactly once, so no leading `MOVZ` is required.
static inline uint32_t RorkHookEncodeMOVK(uint8_t destinationRegister, uint16_t immediate, uint8_t shift) {
    uint32_t base = 0xF2800000u;                       // MOVK Xd, #0
    uint32_t hw = (uint32_t)(shift / 16u) << 21;       // LSL amount: 0,16,32,48 -> 0..3
    uint32_t imm = (uint32_t)immediate << 5;
    uint32_t rd = destinationRegister & 0x1Fu;
    return base | hw | imm | rd;
}

/// Encodes `BR <Xn>` (branch to register).
static inline uint32_t RorkHookEncodeBR(uint8_t sourceRegister) {
    uint32_t base = 0xD61F0000u;
    uint32_t rn = ((uint32_t)sourceRegister & 0x1Fu) << 5;
    return base | rn;
}

#endif /* __arm64__ */

/// Builds the fixed-width arm64 absolute jump sequence used by destructive detours.
size_t RorkHookBuildAbsoluteJump(const void *destination, uint32_t *instructions, size_t capacity) {
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
    uint64_t target = (uint64_t)(uintptr_t)stripped;

    // Scratch register x16 (IP0) is reserved for inter-procedural veneers, so
    // clobbering it across the branch is safe.
    const uint8_t scratch = 16;
    instructions[0] = RorkHookEncodeMOVK(scratch, (uint16_t)(target >> 0), 0);
    instructions[1] = RorkHookEncodeMOVK(scratch, (uint16_t)(target >> 16), 16);
    instructions[2] = RorkHookEncodeMOVK(scratch, (uint16_t)(target >> 32), 32);
    instructions[3] = RorkHookEncodeMOVK(scratch, (uint16_t)(target >> 48), 48);
    instructions[4] = RorkHookEncodeBR(scratch);
    return RORK_HOOK_ABSOLUTE_JUMP_WORDS;
#else
    (void)destination;
    (void)instructions;
    (void)capacity;
    return 0;
#endif
}

/// Patches a function prologue in place so it branches directly to `replacement`.
kern_return_t RorkHookReplaceFunction(void *function, void *replacement) {
#if defined(__arm64__)
    if (function == NULL || replacement == NULL) {
        return KERN_INVALID_ARGUMENT;
    }

    void *target = function;
#if __has_feature(ptrauth_calls)
    target = ptrauth_strip(function, ptrauth_key_function_pointer);
#endif

    uint32_t instructions[RORK_HOOK_ABSOLUTE_JUMP_WORDS];
    if (RorkHookBuildAbsoluteJump(replacement, instructions, RORK_HOOK_ABSOLUTE_JUMP_WORDS) == 0) {
        return KERN_NOT_SUPPORTED;
    }
    const vm_size_t patchSize = sizeof(instructions);

    kern_return_t result = RorkHookMakeMemoryWritable((vm_address_t)(uintptr_t)target, patchSize);
    if (result != KERN_SUCCESS) {
        return result;
    }

    memcpy(target, instructions, sizeof(instructions));

    result = RorkHookMakeMemoryExecutable((vm_address_t)(uintptr_t)target, patchSize);
    if (result != KERN_SUCCESS) {
        return result;
    }

    sys_icache_invalidate(target, sizeof(instructions));
    return KERN_SUCCESS;
#else
    (void)function;
    (void)replacement;
    return KERN_NOT_SUPPORTED;
#endif
}
