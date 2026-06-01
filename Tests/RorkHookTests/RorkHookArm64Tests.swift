import RorkHook
import XCTest

/// Tests for the arm64 instruction decoders used to inspect dyld/runtime stubs.
final class RorkHookArm64Tests: XCTestCase {

    private func encodeADRP(register: UInt8, signedPages: Int64) -> UInt32 {
        let rawImmediate = UInt64(bitPattern: signedPages) & ((1 << 21) - 1)
        let immlo = UInt32(rawImmediate & 0x3) << 29
        let immhi = UInt32((rawImmediate >> 2) & 0x7ffff) << 5
        return 0x9000_0000 | immlo | immhi | UInt32(register & 0x1f)
    }

    func testSignExtendHandlesPositiveAndNegativeValues() {
        XCTAssertEqual(RorkHookArm64SignExtend(0b0_1010, 5), 10)
        XCTAssertEqual(RorkHookArm64SignExtend(0b1_0110, 5), -10)
    }

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

    func testDecodeMOVZImmediate() {
        let instruction = UInt32(0xd280_0000) | (1 << 21) | (0xabcd << 5) | 4
        var value: UInt = 0

        XCTAssertTrue(RorkHookDecodeMOVZImmediate(instruction, &value))
        XCTAssertEqual(value, 0xabcd_0000)
    }

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
}
