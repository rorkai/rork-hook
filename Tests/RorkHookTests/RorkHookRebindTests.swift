import Foundation
import RorkHook
import RorkHookTestSupport
import XCTest

/// Tests for image-local symbol pointer rebinding.
final class RorkHookRebindTests: XCTestCase {

    /// Runs a compiler subprocess and surfaces its complete diagnostic output.
    private func runCompiler(arguments: [String]) throws {
        let process = Process()
        let output = Pipe()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/xcrun")
        process.arguments = ["clang"] + arguments
        process.standardOutput = output
        process.standardError = output
        try process.run()
        let diagnosticsData = output.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()

        let diagnostics = String(
            decoding: diagnosticsData,
            as: UTF8.self
        )
        XCTAssertEqual(process.terminationStatus, 0, diagnostics)
    }

    /// Builds a provider/consumer dylib pair whose only imported call can be
    /// observed after global symbol-pointer rebinding.
    private func makeGlobalRebindFixture() throws -> (provider: String, consumer: String) {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("rork-hook-global-\(UUID().uuidString)")
        try FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: true
        )
        addTeardownBlock {
            try? FileManager.default.removeItem(at: directory)
        }

        let providerSource = directory.appendingPathComponent("provider.c")
        let consumerSource = directory.appendingPathComponent("consumer.c")
        let provider = directory.appendingPathComponent("libProvider.dylib")
        let consumer = directory.appendingPathComponent("libConsumer.dylib")
        try """
        int RorkHookGlobalFixtureOriginal(void) {
            return 17;
        }
        """.write(to: providerSource, atomically: true, encoding: .utf8)
        try """
        extern int RorkHookGlobalFixtureOriginal(void);

        int RorkHookGlobalFixtureCall(void) {
            return RorkHookGlobalFixtureOriginal();
        }
        """.write(to: consumerSource, atomically: true, encoding: .utf8)

        try runCompiler(arguments: [
            "-dynamiclib",
            providerSource.path,
            "-install_name",
            provider.path,
            "-o",
            provider.path,
        ])
        try runCompiler(arguments: [
            "-dynamiclib",
            consumerSource.path,
            provider.path,
            "-o",
            consumer.path,
        ])
        return (provider.path, consumer.path)
    }

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

    /// Verifies pointer sections remain eligible in writable data segments that
    /// do not use the three historically hard-coded segment names.
    func testRebindSymbolInImageScansAdditionalDataSegments() {
        XCTAssertTrue(RorkHookTestSupportRebindsPointerSection("__DATA_DIRTY"))
        XCTAssertTrue(RorkHookTestSupportRebindsPointerSection("__AUTH"))
    }

    /// Verifies that an authenticated pointer with an unknown signing schema is
    /// stripped for comparison rather than authenticated and faulted.
    func testRebindSymbolInImageDoesNotAuthenticateForeignSlot() {
        XCTAssertTrue(RorkHookTestSupportRebindsForeignAuthenticatedPointer())
    }

    /// Verifies registration immediately applies to images that are already
    /// present when the rebind is published.
    func testGlobalRebindAppliesToLoadedImage() throws {
        let fixture = try makeGlobalRebindFixture()

        XCTAssertTrue(
            RorkHookTestSupportGloballyRebindsFixture(
                fixture.provider,
                fixture.consumer,
                true
            )
        )
    }

    /// Verifies dyld's add-image callback applies existing registrations to an
    /// image loaded after registration completes.
    func testGlobalRebindAppliesToFutureImage() throws {
        let fixture = try makeGlobalRebindFixture()

        XCTAssertTrue(
            RorkHookTestSupportGloballyRebindsFixture(
                fixture.provider,
                fixture.consumer,
                false
            )
        )
    }

    /// Verifies a rejecting filter prevents both current- and future-image
    /// processing for its registration.
    func testGlobalRebindHonorsRejectingFilter() throws {
        let fixture = try makeGlobalRebindFixture()

        XCTAssertTrue(
            RorkHookTestSupportGlobalRebindHonorsRejectingFilter(
                fixture.provider,
                fixture.consumer
            )
        )
    }
}
