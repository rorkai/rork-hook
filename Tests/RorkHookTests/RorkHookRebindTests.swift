import RorkHook
import RorkHookTestSupport
import XCTest

/// Tests for image-local symbol pointer rebinding.
final class RorkHookRebindTests: XCTestCase {

    /// Verifies image-local rebinding by redirecting an imported `strcmp` slot.
    func testRebindSymbolInImageRedirectsImportedFunctionSlot() throws {
        let header = try XCTUnwrap(RorkHookTestSupportImageHeader())
        XCTAssertLessThan(RorkHookTestSupportCallImportedStrcmp("a", "b"), 0)

        RorkHookRebindSymbolInImage(
            header,
            RorkHookTestSupportImportedStrcmpPointer(),
            RorkHookTestSupportReplacementStrcmpPointer()
        )

        XCTAssertEqual(
            RorkHookTestSupportCallImportedStrcmp("a", "b"),
            RorkHookTestSupportReplacementStrcmpResult()
        )
    }
}
