import RorkHook
import RorkHookTestSupport
import XCTest

/// Tests for resolving symbols in already-loaded Mach-O images.
final class RorkHookSymbolTests: XCTestCase {

    func testFindSymbolResolvesExportedTestSupportFunction() throws {
        let header = try XCTUnwrap(RorkHookTestSupportImageHeader())
        let resolved = RorkHookFindSymbol(header, "_RorkHookTestSupportSymbolAnchor")

        XCTAssertNotNil(resolved)
        XCTAssertTrue(RorkHookTestSupportPointerMatchesSymbolAnchor(resolved))
    }

    func testFindSymbolReturnsNilForMissingSymbol() throws {
        let header = try XCTUnwrap(RorkHookTestSupportImageHeader())

        XCTAssertNil(RorkHookFindSymbol(header, "_RorkHookDefinitelyMissingSymbol"))
    }
}
