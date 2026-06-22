import RorkHook
import RorkHookTestSupport
import XCTest

/// Tests for the arm64 instruction decoders used to inspect dyld/runtime stubs.
final class RorkHookArm64Tests: XCTestCase {

    /// Builds an `ADRP` instruction word with a controlled signed page offset.
    private func encodeADRP(register: UInt8, signedPages: Int64) -> UInt32 {
        let rawImmediate = UInt64(bitPattern: signedPages) & ((1 << 21) - 1)
        let immlo = UInt32(rawImmediate & 0x3) << 29
        let immhi = UInt32((rawImmediate >> 2) & 0x7ffff) << 5
        return 0x9000_0000 | immlo | immhi | UInt32(register & 0x1f)
    }

    /// Verifies fixed-width sign extension for positive and negative immediates.
    func testSignExtendHandlesPositiveAndNegativeValues() {
        XCTAssertEqual(RorkHookArm64SignExtend(0b0_1010, 5), 10)
        XCTAssertEqual(RorkHookArm64SignExtend(0b1_0110, 5), -10)
    }

    /// Verifies that bits above the declared immediate width do not affect the
    /// sign-extended result.
    func testSignExtendMasksBitsOutsideImmediate() {
        XCTAssertEqual(RorkHookArm64SignExtend(0b10_01010, 5), 10)
        XCTAssertEqual(RorkHookArm64SignExtend(0b10_10110, 5), -10)
    }

    /// Verifies that invalid widths are rejected without performing an invalid
    /// shift in the C implementation.
    func testSignExtendRejectsInvalidBitWidths() {
        XCTAssertEqual(RorkHookArm64SignExtend(1, 0), 0)
        XCTAssertEqual(RorkHookArm64SignExtend(1, 65), 0)
    }

    /// Verifies every decoder rejects missing output storage without faulting.
    func testDecodersRejectMissingOutputStorage() {
        XCTAssertTrue(RorkHookTestSupportArm64DecoderNullArgumentGuardsPass())
    }

    /// Verifies that `ADRP` decoding reconstructs its page-relative address.
    func testDecodeADRPComputesPageRelativeAddress() {
        let pc = UInt(0x1_0000_1234)
        let signedPages: Int64 = -3
        let instruction = encodeADRP(register: 8, signedPages: signedPages)
        var register: UInt8 = 0
        var page: UInt = 0

        XCTAssertTrue(RorkHookDecodeADRP(instruction, pc, &register, &page))
        XCTAssertEqual(register, 8)
        XCTAssertEqual(page, (pc & ~0xfff) &- UInt((-signedPages) << 12))
    }

    /// Verifies both legal `ADD (immediate)` byte-offset forms.
    func testDecodeADDImmediateHandlesPlainAndShiftedOffsets() {
        let register: UInt8 = 9
        let plain = UInt32(0x9100_0000) | (0x123 << 10) | (UInt32(register) << 5) | UInt32(register)
        var offset: UInt = 0
        XCTAssertTrue(RorkHookDecodeADDImmediate(plain, register, &offset))
        XCTAssertEqual(offset, 0x123)

        let shifted = plain | (1 << 22)
        XCTAssertTrue(RorkHookDecodeADDImmediate(shifted, register, &offset))
        XCTAssertEqual(offset, 0x123000)

        XCTAssertFalse(RorkHookDecodeADDImmediate(plain, register + 1, &offset))
    }

    /// Verifies scaled and signed load-offset decoders.
    func testDecodeLoadOffsets() {
        let baseRegister: UInt8 = 11
        let ldr = UInt32(0xf940_0000) | (0x44 << 10) | (UInt32(baseRegister) << 5) | 3
        var unsignedOffset: UInt = 0
        XCTAssertTrue(RorkHookDecodeLDRUnsigned64(ldr, baseRegister, &unsignedOffset))
        XCTAssertEqual(unsignedOffset, 0x44 * 8)
        XCTAssertTrue(RorkHookDecodeLDRUnsignedOffset64(ldr, &unsignedOffset))
        XCTAssertEqual(unsignedOffset, 0x44 * 8)
        XCTAssertFalse(RorkHookDecodeLDRUnsigned64(ldr, baseRegister + 1, &unsignedOffset))

        let negativeImm9 = UInt32(0x1f0)
        let preIndex = UInt32(0xf840_0c00) | (negativeImm9 << 12)
        var signedOffset: Int = 0
        XCTAssertTrue(RorkHookDecodeLDRSignedPreIndex64(preIndex, &signedOffset))
        XCTAssertEqual(signedOffset, -16)

        let ldur = UInt32(0xf840_0000) | (negativeImm9 << 12) | (UInt32(baseRegister) << 5) | 7
        XCTAssertTrue(RorkHookDecodeLDUR64(ldur, baseRegister, &signedOffset))
        XCTAssertEqual(signedOffset, -16)
        XCTAssertFalse(RorkHookDecodeLDUR64(ldur, baseRegister + 1, &signedOffset))
    }

    /// Verifies that the unscaled-load decoder does not accept pre-indexed or
    /// post-indexed addressing forms that share the same opcode prefix.
    func testDecodeLDURRejectsIndexedAddressingModes() {
        let baseRegister: UInt8 = 11
        let immediate = UInt32(0x10) << 12
        let registers = (UInt32(baseRegister) << 5) | 7
        let postIndex = UInt32(0xf840_0400) | immediate | registers
        let preIndex = UInt32(0xf840_0c00) | immediate | registers
        var offset: Int = 0

        XCTAssertFalse(RorkHookDecodeLDUR64(postIndex, baseRegister, &offset))
        XCTAssertFalse(RorkHookDecodeLDUR64(preIndex, baseRegister, &offset))
    }

    /// Verifies `MOVZ` immediate reconstruction.
    func testDecodeMOVZImmediate() {
        let instruction = UInt32(0xd280_0000) | (1 << 21) | (0xabcd << 5) | 4
        var value: UInt = 0

        XCTAssertTrue(RorkHookDecodeMOVZImmediate(instruction, &value))
        XCTAssertEqual(value, 0xabcd_0000)
    }

    /// Verifies that the decoder accepts only the 64-bit MOVZ form used for
    /// pointer-sized offsets.
    func testDecodeMOVZImmediateRejects32BitInstruction() {
        let instruction = UInt32(0x5280_0000) | (0xabcd << 5) | 4
        var value: UInt = 0

        XCTAssertFalse(RorkHookDecodeMOVZImmediate(instruction, &value))
    }

    /// Verifies that branch following accepts `B` veneers but rejects `BL` calls.
    func testFollowOneBranchReturnsBranchTargetOrOriginalPointer() {
        var words: [UInt32] = [
            0x1400_0002,
            0xd503_201f,
            0xd503_201f,
        ]

        words.withUnsafeMutableBufferPointer { buffer in
            let base = buffer.baseAddress!
            XCTAssertEqual(RorkHookFollowOneBranch(base), base + 2)
            XCTAssertEqual(RorkHookFollowOneBranch(base + 1), base + 1)

            base[0] = 0x9400_0002
            XCTAssertEqual(RorkHookFollowOneBranch(base), base)
        }
    }

    /// Verifies that a backwards branch resolves relative to its instruction.
    func testFollowOneBranchHandlesNegativeDisplacement() {
        var words: [UInt32] = [
            0xd503_201f,
            0xd503_201f,
            0x17ff_fffe,
        ]

        words.withUnsafeMutableBufferPointer { buffer in
            let base = buffer.baseAddress!
            XCTAssertEqual(RorkHookFollowOneBranch(base + 2), base)
        }
    }
}
