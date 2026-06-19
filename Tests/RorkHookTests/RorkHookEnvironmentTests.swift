import XCTest
import RorkHook
import RorkHookTestSupport

/// Smoke tests for the environment-query helpers. These run on the host (macOS
/// or the simulator), where TPRO is unsupported and the device-only fast paths
/// fall back to portable behaviour.
final class RorkHookEnvironmentTests: XCTestCase {

    /// Verifies the package and ABI version probes.
    func testVersionReportsPackageAndABI() {
        XCTAssertEqual(String(cString: RorkHookVersion()), "0.2.0")
        XCTAssertEqual(RorkHookABIVersion(), 1)
    }

    /// Verifies that host builds never touch device-only TPRO state.
    func testTPROIsUnsupportedOnHost() {
        // TPRO hardening only exists on arm64e iOS hardware; the host build must
        // report it as unavailable rather than touch the comm page.
        XCTAssertFalse(RorkHookSupportsTPRO())
        XCTAssertFalse(RorkHookThreadCanWriteTPRO())

        // The window helpers must be safe no-ops when unsupported.
        RorkHookBeginThreadTPROWrite()
        RorkHookEndThreadTPROWrite()
        XCTAssertFalse(RorkHookThreadCanWriteTPRO())
    }

    /// Verifies that shared-cache discovery returns a valid C string on hosts.
    func testLocateSharedCacheReturnsAReadableString() {
        // May be empty on a host without an iOS-style cache layout, but it must
        // always return a valid, NUL-terminated C string and never crash.
        let cPath = RorkHookLocateSharedCache()

        let maxCStringLength = 4096
        var terminatorIndex = maxCStringLength
        for index in 0..<maxCStringLength where cPath[index] == 0 {
            terminatorIndex = index
            break
        }
        XCTAssertLessThan(terminatorIndex, maxCStringLength)
        guard terminatorIndex < maxCStringLength else {
            return
        }
        XCTAssertEqual(cPath[terminatorIndex], 0)
        for index in 0..<terminatorIndex {
            XCTAssertNotEqual(cPath[index], 0)
        }

        let path = String(cString: cPath)
        XCTAssertEqual(path.utf8.count, terminatorIndex)
        XCTAssertTrue(path.isEmpty || path.hasPrefix("/"))
    }

    /// Verifies that empty VM-protection ranges are rejected deterministically.
    func testProtectMemoryRejectsEmptyRange() {
        XCTAssertEqual(RorkHookProtectMemory(0, 0, VM_PROT_READ), KERN_INVALID_ARGUMENT)
    }

    /// Verifies defensive C null guards that are hidden by Swift nullability.
    func testCABIRejectsNullArguments() {
        XCTAssertTrue(RorkHookTestSupportNullArgumentGuardsPass())
    }
}
