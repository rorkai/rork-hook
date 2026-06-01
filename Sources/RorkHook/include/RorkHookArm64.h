#ifndef RORK_HOOK_ARM64_H
#define RORK_HOOK_ARM64_H

#include "RorkHookTypes.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

RORK_HOOK_ASSUME_NONNULL_BEGIN

/// Sign-extends `value`, whose high bit is at `bits - 1`, into a 64-bit integer.
int64_t RorkHookArm64SignExtend(uint64_t value, int bits);

/// Decodes an arm64 `ADRP` instruction and returns the destination register and
/// page address it computes for the instruction located at `pc`.
bool RorkHookDecodeADRP(uint32_t instruction,
                        uintptr_t pc,
                        uint8_t *targetRegister,
                        uintptr_t *pageAddress);

/// Decodes an arm64 `ADD (immediate)` that uses `baseRegister` as both source
/// and destination, returning the computed immediate byte offset.
bool RorkHookDecodeADDImmediate(uint32_t instruction,
                                uint8_t baseRegister,
                                uintptr_t *offset);

/// Decodes an arm64 unsigned 64-bit `LDR` using `baseRegister` as the base.
bool RorkHookDecodeLDRUnsigned64(uint32_t instruction,
                                 uint8_t baseRegister,
                                 uintptr_t *offset);

/// Decodes an arm64 unsigned 64-bit `LDR` and returns its byte offset without
/// constraining the base register.
bool RorkHookDecodeLDRUnsignedOffset64(uint32_t instruction, uintptr_t *offset);

/// Decodes an arm64 signed pre-indexed 64-bit `LDR`.
bool RorkHookDecodeLDRSignedPreIndex64(uint32_t instruction, intptr_t *offset);

/// Decodes an arm64 unscaled signed 64-bit `LDUR` using `baseRegister` as the
/// base.
bool RorkHookDecodeLDUR64(uint32_t instruction,
                          uint8_t baseRegister,
                          intptr_t *offset);

/// Decodes an arm64 `MOVZ (immediate)` and returns the shifted immediate value.
bool RorkHookDecodeMOVZImmediate(uint32_t instruction, uintptr_t *value);

/// Follows a single unconditional arm64 `B` instruction at `instructions`; when
/// the first instruction is not a branch, returns `instructions` unchanged.
uint32_t *RorkHookFollowOneBranch(uint32_t *instructions);

RORK_HOOK_ASSUME_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* RORK_HOOK_ARM64_H */
