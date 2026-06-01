#include "RorkHookArm64.h"

int64_t RorkHookArm64SignExtend(uint64_t value, int bits) {
    uint64_t signBit = 1ULL << (bits - 1);
    return (int64_t)((value ^ signBit) - signBit);
}

bool RorkHookDecodeADRP(uint32_t instruction,
                        uintptr_t pc,
                        uint8_t *targetRegister,
                        uintptr_t *pageAddress) {
    if ((instruction & 0x9f000000u) != 0x90000000u) {
        return false;
    }

    uint64_t immlo = (instruction >> 29) & 0x3u;
    uint64_t immhi = (instruction >> 5) & 0x7ffffu;
    int64_t signedPages = RorkHookArm64SignExtend((immhi << 2) | immlo, 21);
    *targetRegister = (uint8_t)(instruction & 0x1fu);
    *pageAddress = (pc & ~0xfffULL) + ((uintptr_t)(signedPages << 12));
    return true;
}

bool RorkHookDecodeADDImmediate(uint32_t instruction,
                                uint8_t baseRegister,
                                uintptr_t *offset) {
    if ((instruction & 0xff800000u) != 0x91000000u) {
        return false;
    }
    uint8_t destination = instruction & 0x1fu;
    uint8_t source = (instruction >> 5) & 0x1fu;
    if (destination != baseRegister || source != baseRegister) {
        return false;
    }

    uint32_t imm12 = (instruction >> 10) & 0xfffu;
    uint32_t shift = (instruction >> 22) & 0x3u;
    *offset = shift == 1 ? ((uintptr_t)imm12 << 12) : imm12;
    return true;
}

bool RorkHookDecodeLDRUnsigned64(uint32_t instruction,
                                 uint8_t baseRegister,
                                 uintptr_t *offset) {
    if ((instruction & 0xffc00000u) != 0xf9400000u) {
        return false;
    }
    uint8_t source = (instruction >> 5) & 0x1fu;
    if (source != baseRegister) {
        return false;
    }
    uint32_t imm12 = (instruction >> 10) & 0xfffu;
    *offset = (uintptr_t)imm12 << 3;
    return true;
}

bool RorkHookDecodeLDRUnsignedOffset64(uint32_t instruction, uintptr_t *offset) {
    if ((instruction & 0xffc00000u) != 0xf9400000u) {
        return false;
    }

    uint32_t imm12 = (instruction >> 10) & 0xfffu;
    *offset = (uintptr_t)imm12 << 3;
    return true;
}

bool RorkHookDecodeLDRSignedPreIndex64(uint32_t instruction, intptr_t *offset) {
    if ((instruction & 0xffe00c00u) != 0xf8400c00u) {
        return false;
    }

    uint64_t imm9 = (instruction >> 12) & 0x1ffu;
    *offset = (intptr_t)RorkHookArm64SignExtend(imm9, 9);
    return true;
}

bool RorkHookDecodeLDUR64(uint32_t instruction,
                          uint8_t baseRegister,
                          intptr_t *offset) {
    if ((instruction & 0xffc00000u) != 0xf8400000u) {
        return false;
    }
    uint8_t source = (instruction >> 5) & 0x1fu;
    if (source != baseRegister) {
        return false;
    }
    uint64_t imm9 = (instruction >> 12) & 0x1ffu;
    *offset = (intptr_t)RorkHookArm64SignExtend(imm9, 9);
    return true;
}

bool RorkHookDecodeMOVZImmediate(uint32_t instruction, uintptr_t *value) {
    if ((instruction & 0x7f800000u) != 0x52800000u) {
        return false;
    }

    uint32_t imm16 = (instruction >> 5) & 0xffffu;
    uint32_t shift = ((instruction >> 21) & 0x3u) * 16u;
    *value = (uintptr_t)imm16 << shift;
    return true;
}

uint32_t *RorkHookFollowOneBranch(uint32_t *instructions) {
    uint32_t instruction = instructions[0];
    if ((instruction & 0xfc000000u) != 0x14000000u) {
        return instructions;
    }

    int64_t imm26 = RorkHookArm64SignExtend(instruction & 0x03ffffffu, 26);
    return (uint32_t *)((uintptr_t)instructions + (uintptr_t)(imm26 << 2));
}
