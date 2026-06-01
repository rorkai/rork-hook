import RorkHook
import RorkHookTestSupport
import XCTest

/// Tests for resolving symbols in already-loaded Mach-O images.
final class RorkHookSymbolTests: XCTestCase {

    /// Verifies that loaded-image lookup can resolve an exported test symbol.
    func testFindSymbolResolvesExportedTestSupportFunction() throws {
        let header = try XCTUnwrap(RorkHookTestSupportImageHeader())
        let resolved = RorkHookFindSymbol(header, "_RorkHookTestSupportSymbolAnchor")

        XCTAssertNotNil(resolved)
        XCTAssertTrue(RorkHookTestSupportPointerMatchesSymbolAnchor(resolved))
    }

    /// Verifies that missing symbols return `nil` instead of an arbitrary address.
    func testFindSymbolReturnsNilForMissingSymbol() throws {
        let header = try XCTUnwrap(RorkHookTestSupportImageHeader())

        XCTAssertNil(RorkHookFindSymbol(header, "_RorkHookDefinitelyMissingSymbol"))
    }
}
