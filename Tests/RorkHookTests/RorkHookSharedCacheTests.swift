import RorkHookTestSupport
import XCTest

/// Tests for bounded parsing of dyld shared-cache image and local-symbol data.
final class RorkHookSharedCacheTests: XCTestCase {

    /// Verifies the modern split-symbol layout and 64-bit image entry.
    func testSharedCacheParserResolvesModernSidecar() {
        XCTAssertTrue(RorkHookTestSupportSharedCacheResolvesSidecar())
    }

    /// Verifies legacy image-table fields and inline 32-bit symbol entries.
    func testSharedCacheParserResolvesLegacyInlineSymbols() {
        XCTAssertTrue(RorkHookTestSupportSharedCacheResolvesLegacyInlineSymbols())
    }

    /// Verifies symbol lookup has no arbitrary fixed name-length limit.
    func testSharedCacheParserResolvesLongSymbolName() {
        XCTAssertTrue(RorkHookTestSupportSharedCacheResolvesLongSymbolName())
    }

    /// Verifies the same parser through the production read-only file-mapping
    /// path, including automatic `.symbols` sidecar selection.
    func testSharedCacheParserResolvesMappedFiles() {
        XCTAssertTrue(RorkHookTestSupportSharedCacheResolvesMappedFiles())
    }

    /// Verifies only regular files are eligible for read-only cache mapping.
    func testSharedCacheParserRejectsNonRegularFile() {
        XCTAssertTrue(
            RorkHookTestSupportSharedCacheRejectsNonRegularFile()
        )
    }

    /// Verifies the image table cannot extend beyond the main cache bytes.
    func testSharedCacheParserRejectsTruncatedImageTable() {
        XCTAssertTrue(RorkHookTestSupportSharedCacheRejectsTruncatedImageTable())
    }

    /// Verifies overflowing local-symbol ranges are rejected before arithmetic.
    func testSharedCacheParserRejectsOverflowedSymbolRange() {
        XCTAssertTrue(RorkHookTestSupportSharedCacheRejectsOverflowedSymbolRange())
    }

    /// Verifies each candidate name terminates within the declared string pool.
    func testSharedCacheParserRejectsUnterminatedSymbol() {
        XCTAssertTrue(RorkHookTestSupportSharedCacheRejectsUnterminatedSymbol())
    }

    /// Verifies a sidecar cannot place local-symbol metadata beyond its bytes.
    func testSharedCacheParserRejectsOutOfBoundsLocalSymbols() {
        XCTAssertTrue(RorkHookTestSupportSharedCacheRejectsOutOfBoundsLocalSymbols())
    }

    /// Verifies an image entry cannot claim symbols beyond the nlist table.
    func testSharedCacheParserRejectsEntryBeyondSymbolTable() {
        XCTAssertTrue(RorkHookTestSupportSharedCacheRejectsEntryBeyondSymbolTable())
    }

    /// Verifies applying the runtime slide cannot wrap a resolved pointer.
    func testSharedCacheParserRejectsAddressOverflow() {
        XCTAssertTrue(RorkHookTestSupportSharedCacheRejectsAddressOverflow())
    }
}
