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

    /// Verifies that file-image lookup translates a symbol's VM address through
    /// the segment that owns it rather than treating VM offsets as file offsets.
    func testFindSymbolInFileImageTranslatesSegmentFileOffset() throws {
        let fixture = RorkHookTestSupportCreateFileImageFixture(true)
        defer {
            RorkHookTestSupportDestroyFileImageFixture(fixture)
        }
        let bytes = try XCTUnwrap(fixture.bytes)
        let resolved = bytes.withMemoryRebound(
            to: RorkHookMachHeader.self,
            capacity: 1
        ) { header in
            RorkHookFindSymbolInFileImageWithSize(
                header,
                fixture.size,
                RorkHookTestSupportFileImageSymbolName()
            )
        }

        XCTAssertEqual(resolved, UnsafeMutableRawPointer(bytes + fixture.symbolFileOffset))
    }

    /// Verifies that an explicit mapped length bounds the load-command table.
    func testFindSymbolInFileImageRejectsTruncatedCommands() throws {
        let fixture = RorkHookTestSupportCreateFileImageFixture(false)
        defer {
            RorkHookTestSupportDestroyFileImageFixture(fixture)
        }
        let bytes = try XCTUnwrap(fixture.bytes)
        let resolved = bytes.withMemoryRebound(
            to: RorkHookMachHeader.self,
            capacity: 1
        ) { header in
            RorkHookFindSymbolInFileImageWithSize(
                header,
                MemoryLayout<RorkHookMachHeader>.size,
                RorkHookTestSupportFileImageSymbolName()
            )
        }

        XCTAssertNil(resolved)
    }

    /// Verifies that the compatibility lookup does not dereference load
    /// commands beyond the readable VM region containing the header.
    func testLegacyFileImageLookupRejectsUnreadableCommands() {
        XCTAssertTrue(RorkHookTestSupportLegacyFileImageRejectsUnreadableCommands())
    }

    /// Verifies bounded parsing does not dereference an unaligned nlist pointer.
    func testFindSymbolInFileImageResolvesUnalignedSymbolTable() {
        XCTAssertTrue(RorkHookTestSupportFileImageResolvesUnalignedSymbolTable())
    }

    /// Verifies symbols in zero-fill portions of a segment are not mapped to
    /// unrelated file bytes.
    func testFindSymbolInFileImageRejectsNonFileBackedSymbol() {
        XCTAssertTrue(RorkHookTestSupportFileImageRejectsNonFileBackedSymbol())
    }

    /// Verifies the declared symbol table must fit inside the file mapping.
    func testFindSymbolInFileImageRejectsOutOfBoundsSymbolTable() {
        XCTAssertTrue(RorkHookTestSupportFileImageRejectsOutOfBoundsSymbolTable())
    }

    /// Verifies symbol names terminate inside the declared string table.
    func testFindSymbolInFileImageRejectsUnterminatedSymbol() {
        XCTAssertTrue(RorkHookTestSupportFileImageRejectsUnterminatedSymbol())
    }

    /// Verifies bounded lookup validates commands that follow the symbol table
    /// rather than returning as soon as the desired metadata is found.
    func testFindSymbolInFileImageRejectsMalformedCommandAfterSymtab() {
        XCTAssertTrue(
            RorkHookTestSupportFileImageRejectsMalformedCommandAfterSymtab()
        )
    }

    /// Verifies loaded-image lookup rejects a symbol address whose dyld slide
    /// would wrap the native pointer width.
    func testFindSymbolRejectsAddressOverflow() {
        XCTAssertTrue(RorkHookTestSupportLoadedImageRejectsSymbolAddressOverflow())
    }

    /// Verifies loaded-image lookup cannot treat an unresolved slide as zero.
    func testFindSymbolRejectsUnresolvedImageSlide() {
        XCTAssertTrue(RorkHookTestSupportLoadedImageRejectsUnresolvedSlide())
    }

    /// Verifies live lookup confines symbol metadata to `__LINKEDIT`.
    func testFindSymbolRejectsMetadataOutsideLinkedit() {
        XCTAssertTrue(
            RorkHookTestSupportLoadedImageRejectsMetadataOutsideLinkedit()
        )
    }
}
