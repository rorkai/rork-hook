import XCTest
import RorkHook
import RorkHookTestSupport

/// Tests function-hook encoding, validation, and live detour behavior.
final class RorkHookFunctionTests: XCTestCase {

    /// Reconstructs the 64-bit immediate materialized by the four `MOVK x16`
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

    /// Verifies that callers can provide the known patchable prologue length and
    /// receive a deterministic rejection before any instruction is overwritten.
    func testCheckedDetourRejectsShortPatchRegion() {
        XCTAssertTrue(RorkHookTestSupportCheckedDetourRejectsShortRegion())
    }

    /// Verifies the checked detour path by patching and invoking a dedicated
    /// function inside a disposable child process.
    func testCheckedDetourRedirectsDedicatedTarget() {
        XCTAssertTrue(RorkHookTestSupportCheckedDetourRedirectsTarget())
    }

    /// Verifies checked detours require instruction-aligned targets.
    func testCheckedDetourRejectsUnalignedTarget() {
        XCTAssertTrue(RorkHookTestSupportCheckedDetourRejectsUnalignedTarget())
    }

    /// Verifies the emitted jump cannot straddle a VM page boundary.
    func testCheckedDetourRejectsCrossPageTarget() {
        XCTAssertTrue(RorkHookTestSupportCheckedDetourRejectsCrossPageTarget())
    }

    /// Verifies checked detours do not patch non-executable storage.
    func testCheckedDetourRejectsNonExecutableTarget() {
        XCTAssertTrue(RorkHookTestSupportCheckedDetourRejectsNonExecutableTarget())
    }
}
