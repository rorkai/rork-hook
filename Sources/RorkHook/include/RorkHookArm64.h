#ifndef RORK_HOOK_ARM64_H
#define RORK_HOOK_ARM64_H

#include "RorkHookTypes.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

RORK_HOOK_ASSUME_NONNULL_BEGIN

/// Sign-extends the least-significant `bits` of `value` into a 64-bit integer.
///
/// Bits above the declared immediate width are ignored. Returns 0 when `bits`
/// falls outside the inclusive range 1 through 64. A valid 64-bit width
/// preserves the complete two's-complement bit pattern.
int64_t RorkHookArm64SignExtend(uint64_t value, int bits);

/// Decodes an arm64 `ADRP` instruction and returns the destination register and
/// page address it computes for the instruction located at `pc`.
///
/// Returns `false` for another instruction class, missing output storage, or a
/// displacement that would overflow the native address space. Both outputs are
/// left unchanged on failure.
bool RorkHookDecodeADRP(uint32_t instruction,
                        uintptr_t pc,
                        uint8_t *targetRegister,
                        uintptr_t *pageAddress);

/// Decodes an arm64 `ADD (immediate)` that uses `baseRegister` as both source
/// and destination, returning the computed immediate byte offset.
///
/// Both the plain and 12-bit-shifted immediate forms are supported. Returns
/// `false` and leaves `offset` unchanged when the instruction does not match.
bool RorkHookDecodeADDImmediate(uint32_t instruction,
                                uint8_t baseRegister,
                                uintptr_t *offset);

/// Decodes an arm64 unsigned 64-bit `LDR` using `baseRegister` as the base.
///
/// The returned byte offset includes the instruction's implicit eight-byte
/// scale. Returns `false` and leaves `offset` unchanged when decoding fails.
bool RorkHookDecodeLDRUnsigned64(uint32_t instruction,
                                 uint8_t baseRegister,
                                 uintptr_t *offset);

/// Decodes an arm64 unsigned 64-bit `LDR` and returns its byte offset without
/// constraining the base register.
///
/// The returned byte offset includes the instruction's implicit eight-byte
/// scale. Returns `false` and leaves `offset` unchanged when decoding fails.
bool RorkHookDecodeLDRUnsignedOffset64(uint32_t instruction, uintptr_t *offset);

/// Decodes an arm64 signed pre-indexed 64-bit `LDR`.
///
/// Returns its sign-extended byte displacement. Returns `false` and leaves
/// `offset` unchanged when the instruction does not match.
bool RorkHookDecodeLDRSignedPreIndex64(uint32_t instruction, intptr_t *offset);

/// Decodes an arm64 unscaled signed 64-bit `LDUR` using `baseRegister` as the
/// base.
///
/// Returns its sign-extended byte displacement. Indexed `LDR` encodings are
/// rejected. On failure, `offset` is left unchanged.
bool RorkHookDecodeLDUR64(uint32_t instruction,
                          uint8_t baseRegister,
                          intptr_t *offset);

/// Decodes a 64-bit arm64 `MOVZ (immediate)` and returns its shifted value.
///
/// The 32-bit variant and other move-wide instructions are rejected. Returns
/// `false` and leaves `value` unchanged when decoding fails.
bool RorkHookDecodeMOVZImmediate(uint32_t instruction, uintptr_t *value);

/// Follows a single unconditional arm64 `B` instruction at `instructions`; when
/// the first instruction is not a branch, returns `instructions` unchanged.
///
/// `instructions` must address at least one readable, instruction-aligned
/// 32-bit word. A displacement that would overflow the native address space is
/// treated as an invalid veneer and also returns the original pointer.
uint32_t *RorkHookFollowOneBranch(uint32_t *instructions);

RORK_HOOK_ASSUME_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* RORK_HOOK_ARM64_H */
