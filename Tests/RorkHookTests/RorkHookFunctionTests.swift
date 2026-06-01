import XCTest
import RorkHook

/// Tests for the pure, host-runnable parts of RorkHook. The detour and memory
/// primitives mutate live process state on device, so the suite focuses on the
/// deterministic ARM64 jump encoder and on smoke-testing the query helpers.
final class RorkHookFunctionTests: XCTestCase {

    /// Reconstructs the 64-bit immediate materialised by the four `MOVK x16`
    /// words so the encoding can be verified without hard-coded golden bytes.
    private func decodeAbsoluteJumpTarget(_ words: [UInt32]) -> UInt64 {
        var target: UInt64 = 0
        for lane in 0..<4 {
            let word = words[lane]
            // MOVK (64-bit): fixed bits 0xF2800000, hw in [22:21], imm16 in [20:5], Rd in [4:0].
            XCTAssertEqual(word & 0x1F, 16, "MOVK must target x16")
            XCTAssertEqual(word & 0xFF80_0000, 0xF280_0000, "word \(lane) is not a 64-bit MOVK")
            let shift = UInt64((word >> 21) & 0x3) * 16
            let immediate = UInt64((word >> 5) & 0xFFFF)
            target |= immediate << shift
        }
        return target
    }

    /// Verifies that absolute-jump generation encodes the target and final branch.
    func testBuildAbsoluteJumpEncodesTargetAndBranch() throws {
        let destination = UInt(0x0000_0001_AABB_CCDD)
        let destinationPointer = try XCTUnwrap(UnsafeRawPointer(bitPattern: destination))
        var words = [UInt32](repeating: 0, count: Int(RORK_HOOK_ABSOLUTE_JUMP_WORDS))

        let written = words.withUnsafeMutableBufferPointer { buffer in
            RorkHookBuildAbsoluteJump(destinationPointer, buffer.baseAddress!, buffer.count)
        }

#if arch(arm64)
        XCTAssertEqual(Int(written), Int(RORK_HOOK_ABSOLUTE_JUMP_WORDS))
        XCTAssertEqual(decodeAbsoluteJumpTarget(words), UInt64(destination), "MOVK lanes must rebuild the destination")
        XCTAssertEqual(words[4], 0xD61F_0200, "final word must be BR x16")
#else
        XCTAssertEqual(written, 0, "absolute jump encoding is arm64-only")
#endif
    }

    /// Verifies that absolute-jump generation refuses undersized buffers.
    func testBuildAbsoluteJumpRejectsUndersizedBuffer() throws {
        let destinationPointer = try XCTUnwrap(UnsafeRawPointer(bitPattern: UInt(0x1000)))
        var words = [UInt32](repeating: 0, count: 2)
        let written = words.withUnsafeMutableBufferPointer { buffer in
            RorkHookBuildAbsoluteJump(destinationPointer, buffer.baseAddress!, buffer.count)
        }
        XCTAssertEqual(written, 0, "a buffer smaller than the jump sequence must be refused")
    }

}
