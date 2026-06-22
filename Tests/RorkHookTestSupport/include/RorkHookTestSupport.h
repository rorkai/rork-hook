#ifndef RORK_HOOK_TEST_SUPPORT_H
#define RORK_HOOK_TEST_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "RorkHookTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Returns the Mach-O header for the test-support image.
const RorkHookMachHeader *RorkHookTestSupportImageHeader(void);

/// Returns a direct function pointer to ``RorkHookTestSupportSymbolAnchor``.
void *RorkHookTestSupportSymbolAnchorPointer(void);

/// Returns `true` when `pointer` resolves to ``RorkHookTestSupportSymbolAnchor``,
/// ignoring pointer-authentication decoration when present.
bool RorkHookTestSupportPointerMatchesSymbolAnchor(void *pointer);

/// Exported symbol used to prove loaded-image symbol lookup.
int RorkHookTestSupportSymbolAnchor(void);

/// Calls the imported `strcmp` function from the test-support image.
int RorkHookTestSupportCallImportedStrcmp(const char *lhs, const char *rhs);

/// Returns the resolved imported `strcmp` function pointer.
void *RorkHookTestSupportImportedStrcmpPointer(void);

/// Returns a replacement function pointer with the same ABI as `strcmp`.
void *RorkHookTestSupportReplacementStrcmpPointer(void);

/// Returns the fixed value emitted by the replacement `strcmp` implementation.
int RorkHookTestSupportReplacementStrcmpResult(void);

/// Returns `true` when C ABI guard paths reject `NULL` arguments.
bool RorkHookTestSupportNullArgumentGuardsPass(void);

/// Returns `true` when executable-pointer signing preserves the exact bit
/// pattern of an authenticated pointer that addresses non-executable memory.
bool RorkHookTestSupportPreservesSignedNonExecutablePointer(void);

/// Runs the arm64 decoders with missing output storage in a child process.
///
/// The child must exit normally after every decoder rejects the invalid call.
/// Isolating the calls ensures a missing guard is reported as a failed test
/// instead of terminating the test runner with a null-pointer dereference.
bool RorkHookTestSupportArm64DecoderNullArgumentGuardsPass(void);

/// Owns a synthetic file-mapped Mach-O image used by symbol-resolution tests.
typedef struct RorkHookTestFileImageFixture {
    /// Heap allocation containing the complete synthetic file image.
    uint8_t *bytes;

    /// Readable byte length beginning at `bytes`.
    size_t size;

    /// Expected file offset of the fixture's single resolved symbol.
    size_t symbolFileOffset;
} RorkHookTestFileImageFixture;

/// Creates a file-layout Mach-O containing one symbol in a data segment.
///
/// When `hasVirtualGap` is true, the data segment's VM address is deliberately
/// separated from its file offset so tests can detect VM-to-file translation
/// mistakes. The returned fixture must be destroyed with
/// ``RorkHookTestSupportDestroyFileImageFixture``.
RorkHookTestFileImageFixture RorkHookTestSupportCreateFileImageFixture(bool hasVirtualGap);

/// Releases storage owned by a synthetic file-image fixture.
void RorkHookTestSupportDestroyFileImageFixture(RorkHookTestFileImageFixture fixture);

/// Returns the symbol name stored in synthetic file-image fixtures.
const char *RorkHookTestSupportFileImageSymbolName(void);

/// Runs legacy file-image lookup in a child process with load commands placed
/// on an unreadable page, returning `true` only when lookup safely returns NULL.
bool RorkHookTestSupportLegacyFileImageRejectsUnreadableCommands(void);

/// Returns `true` when an unaligned but otherwise valid symbol table resolves
/// without performing an unaligned typed load.
bool RorkHookTestSupportFileImageResolvesUnalignedSymbolTable(void);

/// Returns `true` when a symbol whose VM address lies outside its segment's
/// file-backed bytes is rejected.
bool RorkHookTestSupportFileImageRejectsNonFileBackedSymbol(void);

/// Returns `true` when a symbol table extending beyond the mapped file is
/// rejected before it is read.
bool RorkHookTestSupportFileImageRejectsOutOfBoundsSymbolTable(void);

/// Returns `true` when a candidate symbol name does not terminate inside the
/// declared string table.
bool RorkHookTestSupportFileImageRejectsUnterminatedSymbol(void);

/// Returns `true` when symbol lookup rejects a malformed load command placed
/// after otherwise valid symbol-table metadata.
bool RorkHookTestSupportFileImageRejectsMalformedCommandAfterSymtab(void);

/// Returns `true` when a loaded-image symbol plus its dyld slide would overflow
/// the native address space and is therefore rejected.
bool RorkHookTestSupportLoadedImageRejectsSymbolAddressOverflow(void);

/// Returns `true` when loaded-image lookup rejects a Mach-O whose mapping slide
/// cannot be established from a segment that contains the header.
bool RorkHookTestSupportLoadedImageRejectsUnresolvedSlide(void);

/// Returns `true` when loaded-image lookup rejects symbol metadata stored
/// outside the file-backed range declared by `__LINKEDIT`.
bool RorkHookTestSupportLoadedImageRejectsMetadataOutsideLinkedit(void);

/// Returns `true` when a non-lazy pointer section in `segmentName` is rebound.
bool RorkHookTestSupportRebindsPointerSection(const char *segmentName);

/// Runs authenticated-slot rebinding in a child process with a pointer signed
/// under a foreign PAC schema, returning `true` when scanning does not fault.
bool RorkHookTestSupportRebindsForeignAuthenticatedPointer(void);

/// Registers a global rebind between dynamically built fixture images and
/// verifies the consumer observes the replacement.
bool RorkHookTestSupportGloballyRebindsFixture(const char *providerPath,
                                               const char *consumerPath,
                                               bool loadConsumerBeforeRegistration);

/// Registers a global rebind with a rejecting image filter and verifies the
/// consumer continues calling the original implementation.
bool RorkHookTestSupportGlobalRebindHonorsRejectingFilter(
    const char *providerPath,
    const char *consumerPath);

/// Returns `true` when the checked detour API rejects fewer bytes than its
/// absolute jump requires without mutating the target function.
bool RorkHookTestSupportCheckedDetourRejectsShortRegion(void);

/// Returns `true` when the checked detour API redirects a dedicated function in
/// an isolated child process.
bool RorkHookTestSupportCheckedDetourRedirectsTarget(void);

/// Returns `true` when the checked detour API rejects an unaligned target.
bool RorkHookTestSupportCheckedDetourRejectsUnalignedTarget(void);

/// Returns `true` when the checked detour API rejects a jump sequence that
/// crosses a VM page boundary.
bool RorkHookTestSupportCheckedDetourRejectsCrossPageTarget(void);

/// Returns `true` when the checked detour API rejects readable, writable memory
/// that is not executable.
bool RorkHookTestSupportCheckedDetourRejectsNonExecutableTarget(void);

/// Returns `true` when a modern cache and 64-bit local-symbol sidecar resolve a
/// synthetic private symbol.
bool RorkHookTestSupportSharedCacheResolvesSidecar(void);

/// Returns `true` when a legacy cache resolves 32-bit local-symbol metadata
/// stored inline in the main cache file.
bool RorkHookTestSupportSharedCacheResolvesLegacyInlineSymbols(void);

/// Returns `true` when the parser supports a symbol name larger than its former
/// fixed 1,024-byte scratch buffer.
bool RorkHookTestSupportSharedCacheResolvesLongSymbolName(void);

/// Returns `true` when the file-backed parser maps a main cache and `.symbols`
/// sidecar and resolves their synthetic symbol.
bool RorkHookTestSupportSharedCacheResolvesMappedFiles(void);

/// Returns `true` when the file-backed parser rejects a FIFO instead of treating
/// it as a cache mapping or blocking while opening it.
bool RorkHookTestSupportSharedCacheRejectsNonRegularFile(void);

/// Returns `true` when truncated image metadata is rejected.
bool RorkHookTestSupportSharedCacheRejectsTruncatedImageTable(void);

/// Returns `true` when overflowing local-symbol offsets are rejected.
bool RorkHookTestSupportSharedCacheRejectsOverflowedSymbolRange(void);

/// Returns `true` when a symbol string lacks a terminator inside its table.
bool RorkHookTestSupportSharedCacheRejectsUnterminatedSymbol(void);

/// Returns `true` when local-symbol metadata begins beyond the mapped sidecar.
bool RorkHookTestSupportSharedCacheRejectsOutOfBoundsLocalSymbols(void);

/// Returns `true` when an image entry references symbols beyond the declared
/// nlist table.
bool RorkHookTestSupportSharedCacheRejectsEntryBeyondSymbolTable(void);

/// Returns `true` when applying the shared-cache slide would overflow a pointer.
bool RorkHookTestSupportSharedCacheRejectsAddressOverflow(void);

/// Returns `true` when bounded load-command iteration rejects malformed command
/// counts, command sizes, and truncated tables.
bool RorkHookTestSupportRejectsMalformedLoadCommands(void);

/// Returns `true` when an unmapped address is not mistaken for the next VM
/// region returned by Mach.
bool RorkHookTestSupportRejectsUnmappedMemoryRegion(void);

/// Returns `true` when a TPRO-capable device requires a verified write window
/// even if VM reprotection reported success.
bool RorkHookTestSupportProtectedPointerWriteRequiresTPROWindow(void);

/// Evaluates process-level TPRO detection with synthetic hardware, process
/// configuration, and legacy write-window probe results.
bool RorkHookTestSupportProcessUsesTPRO(
    bool hardwareSupportsTPRO,
    bool processConfigurationKnown,
    bool processConfigurationEnablesTPRO,
    bool writeWindowProbeSucceeded
);

#ifdef __cplusplus
}
#endif

#endif /* RORK_HOOK_TEST_SUPPORT_H */
