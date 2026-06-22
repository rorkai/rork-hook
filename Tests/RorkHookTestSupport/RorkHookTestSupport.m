#include "RorkHookTestSupport.h"

#include "RorkHook.h"
#include "../../Sources/RorkHook/private/RorkHookDyldCache.h"
#include "../../Sources/RorkHook/private/RorkHookInternal.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <mach-o/nlist.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#if __has_feature(ptrauth_calls)
#include <ptrauth.h>
#endif

/// Removes function-pointer signing so tests can compare raw code addresses.
static void *RorkHookTestStripFunctionPointer(void *pointer) {
#if __has_feature(ptrauth_calls)
    return ptrauth_strip(pointer, ptrauth_key_function_pointer);
#else
    return pointer;
#endif
}

/// Finds the Mach-O image header that owns a known test-support address.
static const RorkHookMachHeader *RorkHookTestImageHeaderForAddress(const void *address) {
    Dl_info info;
    if (dladdr(address, &info) == 0 || info.dli_fbase == NULL) {
        return NULL;
    }
    return (const RorkHookMachHeader *)info.dli_fbase;
}

/// Reads the host page size without allowing a failed signed result to become a
/// very large unsigned mapping length.
static bool RorkHookTestPageSize(size_t *pageSizeOut) {
    if (pageSizeOut == NULL) {
        return false;
    }

    int nativePageSize = getpagesize();
    if (nativePageSize <= 0) {
        return false;
    }
    *pageSizeOut = (size_t)nativePageSize;
    return true;
}

/// Returns the Mach-O header for this test-support target.
const RorkHookMachHeader *RorkHookTestSupportImageHeader(void) {
    return RorkHookTestImageHeaderForAddress((const void *)RorkHookTestSupportImageHeader);
}

/// Provides a stable exported symbol body for symbol-resolution tests.
int RorkHookTestSupportSymbolAnchor(void) {
    return 37;
}

/// Returns the direct function pointer for the exported anchor symbol.
void *RorkHookTestSupportSymbolAnchorPointer(void) {
    return (void *)RorkHookTestSupportSymbolAnchor;
}

/// Compares a resolved pointer with the anchor symbol after stripping PAC bits.
bool RorkHookTestSupportPointerMatchesSymbolAnchor(void *pointer) {
    return RorkHookTestStripFunctionPointer(pointer) ==
           RorkHookTestStripFunctionPointer((void *)RorkHookTestSupportSymbolAnchor);
}

/// Calls through the test-support image's imported `strcmp` slot.
int RorkHookTestSupportCallImportedStrcmp(const char *lhs, const char *rhs) {
    return strcmp(lhs, rhs);
}

/// Returns the unresolved replacement target used for rebinding `strcmp`.
void *RorkHookTestSupportImportedStrcmpPointer(void) {
    return (void *)strcmp;
}

/// Replacement implementation used to prove import-slot rebinding.
static int RorkHookTestReplacementStrcmp(const char *lhs, const char *rhs) {
    (void)lhs;
    (void)rhs;
    return RorkHookTestSupportReplacementStrcmpResult();
}

/// Returns a function pointer with the same ABI as `strcmp`.
void *RorkHookTestSupportReplacementStrcmpPointer(void) {
    return (void *)RorkHookTestReplacementStrcmp;
}

/// Returns the sentinel result emitted by the replacement `strcmp`.
int RorkHookTestSupportReplacementStrcmpResult(void) {
    return 4242;
}

/// Exercises C-only null guard paths that Swift cannot call after nullability import.
bool RorkHookTestSupportNullArgumentGuardsPass(void) {
    uint32_t instructions[RORK_HOOK_ABSOLUTE_JUMP_WORDS] = {0};
    void *nullPointer = NULL;
    const void *nullConstPointer = NULL;
    const RorkHookMachHeader *nullHeader = NULL;
    const char *nullString = NULL;

    return RorkHookStoreProtectedPointer(nullPointer, NULL, VM_PROT_READ) == false &&
           RorkHookFindSymbol(nullHeader, "anything") == NULL &&
           RorkHookFindSymbolInFileImage(nullHeader, "anything") == NULL &&
           RorkHookFindSymbolInFileImageWithSize(nullHeader, 0, "anything") == NULL &&
           RorkHookFindSharedCacheSymbol(nullString, "anything") == NULL &&
           RorkHookBuildAbsoluteJump(
               nullConstPointer,
               instructions,
               RORK_HOOK_ABSOLUTE_JUMP_WORDS) == 0 &&
           RorkHookReplaceFunction(nullPointer, nullPointer) != KERN_SUCCESS &&
           RorkHookReplaceFunctionWithSize(nullPointer, 0, nullPointer) != KERN_SUCCESS;
}

/// Verifies non-code pointers retain their original authentication decoration.
bool RorkHookTestSupportPreservesSignedNonExecutablePointer(void) {
#if __has_feature(ptrauth_calls)
    uint8_t storage = 0;
    void *signedPointer = ptrauth_sign_unauthenticated(
        &storage,
        ptrauth_key_function_pointer,
        0x524f524b);
    void *result = RorkHookSignPointerIfExecutable(signedPointer);
    return memcmp(&result, &signedPointer, sizeof(result)) == 0;
#else
    return true;
#endif
}

/// Runs the decoder nullability checks outside the test runner process.
bool RorkHookTestSupportArm64DecoderNullArgumentGuardsPass(void) {
    pid_t child = fork();
    if (child == 0) {
        uint8_t targetRegister = 0;
        uintptr_t unsignedValue = 0;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnonnull"
        bool rejected =
            !RorkHookDecodeADRP(0x90000000u, 0, NULL, &unsignedValue) &&
            !RorkHookDecodeADRP(0x90000000u, 0, &targetRegister, NULL) &&
            !RorkHookDecodeADDImmediate(0x91000000u, 0, NULL) &&
            !RorkHookDecodeLDRUnsigned64(0xf9400000u, 0, NULL) &&
            !RorkHookDecodeLDRUnsignedOffset64(0xf9400000u, NULL) &&
            !RorkHookDecodeLDRSignedPreIndex64(0xf8400c00u, NULL) &&
            !RorkHookDecodeLDUR64(0xf8400000u, 0, NULL) &&
            !RorkHookDecodeMOVZImmediate(0xd2800000u, NULL) &&
            RorkHookFollowOneBranch(NULL) == NULL;
#pragma clang diagnostic pop
        _exit(rejected ? 0 : 1);
    }
    if (child < 0) {
        return false;
    }

    int childStatus = 0;
    return waitpid(child, &childStatus, 0) == child &&
        WIFEXITED(childStatus) &&
        WEXITSTATUS(childStatus) == 0;
}

/// Symbol stored in every synthetic file-layout Mach-O fixture.
static const char RorkHookTestFileImageSymbol[] = "_RorkHookFixtureSymbol";

/// Creates a segment command with no sections for a synthetic file image.
static struct segment_command_64 RorkHookTestSegment(const char *name,
                                                     uint64_t vmAddress,
                                                     uint64_t vmSize,
                                                     uint64_t fileOffset,
                                                     uint64_t fileSize,
                                                     vm_prot_t protection) {
    struct segment_command_64 segment = {
        .cmd = LC_SEGMENT_64,
        .cmdsize = sizeof(struct segment_command_64),
        .vmaddr = vmAddress,
        .vmsize = vmSize,
        .fileoff = fileOffset,
        .filesize = fileSize,
        .maxprot = protection,
        .initprot = protection,
        .nsects = 0,
        .flags = 0,
    };
    strlcpy(segment.segname, name, sizeof(segment.segname));
    return segment;
}

/// Builds a minimal but valid Mach-O file layout with one N_SECT symbol.
RorkHookTestFileImageFixture RorkHookTestSupportCreateFileImageFixture(bool hasVirtualGap) {
    const size_t imageSize = 0x3000;
    const size_t symbolTableOffset = 0x2000;
    const size_t stringTableOffset = 0x2100;
    const size_t symbolFileOffset = 0x1010;
    const uint64_t textVMAddress = 0x100000000ULL;
    const uint64_t dataVMAddress = textVMAddress + (hasVirtualGap ? 0x4000 : 0x1000);

    uint8_t *bytes = calloc(1, imageSize);
    if (bytes == NULL) {
        return (RorkHookTestFileImageFixture){0};
    }

    struct mach_header_64 *header = (struct mach_header_64 *)bytes;
    header->magic = MH_MAGIC_64;
    header->cputype = CPU_TYPE_ARM64;
    header->cpusubtype = CPU_SUBTYPE_ARM64_ALL;
    header->filetype = MH_DYLIB;
    header->ncmds = 3;
    header->sizeofcmds =
        (uint32_t)(sizeof(struct segment_command_64) * 2 + sizeof(struct symtab_command));

    uint8_t *commandBytes = bytes + sizeof(*header);
    struct segment_command_64 text = RorkHookTestSegment(
        SEG_TEXT,
        textVMAddress,
        0x1000,
        0,
        0x1000,
        VM_PROT_READ | VM_PROT_EXECUTE);
    memcpy(commandBytes, &text, sizeof(text));
    commandBytes += sizeof(text);

    struct segment_command_64 data = RorkHookTestSegment(
        SEG_DATA,
        dataVMAddress,
        0x1000,
        0x1000,
        0x1000,
        VM_PROT_READ | VM_PROT_WRITE);
    memcpy(commandBytes, &data, sizeof(data));
    commandBytes += sizeof(data);

    struct symtab_command symtab = {
        .cmd = LC_SYMTAB,
        .cmdsize = sizeof(struct symtab_command),
        .symoff = (uint32_t)symbolTableOffset,
        .nsyms = 1,
        .stroff = (uint32_t)stringTableOffset,
        .strsize = (uint32_t)(sizeof(RorkHookTestFileImageSymbol) + 1),
    };
    memcpy(commandBytes, &symtab, sizeof(symtab));

    struct nlist_64 *symbol = (struct nlist_64 *)(bytes + symbolTableOffset);
    symbol->n_un.n_strx = 1;
    symbol->n_type = N_SECT;
    symbol->n_sect = 1;
    symbol->n_value = dataVMAddress + (symbolFileOffset - (size_t)data.fileoff);

    bytes[stringTableOffset] = '\0';
    memcpy(bytes + stringTableOffset + 1,
           RorkHookTestFileImageSymbol,
           sizeof(RorkHookTestFileImageSymbol));

    return (RorkHookTestFileImageFixture){
        .bytes = bytes,
        .size = imageSize,
        .symbolFileOffset = symbolFileOffset,
    };
}

/// Releases the heap buffer backing a synthetic file image.
void RorkHookTestSupportDestroyFileImageFixture(RorkHookTestFileImageFixture fixture) {
    free(fixture.bytes);
}

/// Returns the fixture's single symbol name.
const char *RorkHookTestSupportFileImageSymbolName(void) {
    return RorkHookTestFileImageSymbol;
}

/// Returns the data-segment command from a valid synthetic file image.
static struct segment_command_64 *RorkHookTestFileImageDataSegment(
    RorkHookTestFileImageFixture fixture) {
    return (struct segment_command_64 *)(
        fixture.bytes +
        sizeof(struct mach_header_64) +
        sizeof(struct segment_command_64));
}

/// Returns the symbol-table command from a valid synthetic file image.
static struct symtab_command *RorkHookTestFileImageSymtab(
    RorkHookTestFileImageFixture fixture) {
    return (struct symtab_command *)(
        fixture.bytes +
        sizeof(struct mach_header_64) +
        sizeof(struct segment_command_64) * 2);
}

/// Resolves the fixture symbol through the bounded production API.
static void *RorkHookTestResolveFileImageFixture(
    RorkHookTestFileImageFixture fixture) {
    return RorkHookFindSymbolInFileImageWithSize(
        (const RorkHookMachHeader *)fixture.bytes,
        fixture.size,
        RorkHookTestFileImageSymbol);
}

/// Isolates an intentionally unreadable load-command table from the test host.
bool RorkHookTestSupportLegacyFileImageRejectsUnreadableCommands(void) {
    size_t pageSize = 0;
    if (!RorkHookTestPageSize(&pageSize)) {
        return false;
    }

    uint8_t *mapping = mmap(
        NULL,
        pageSize * 2,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANON,
        -1,
        0);
    if (mapping == MAP_FAILED) {
        return false;
    }

    struct mach_header_64 *header =
        (struct mach_header_64 *)(mapping + pageSize - sizeof(struct mach_header_64));
    header->magic = MH_MAGIC_64;
    header->ncmds = 1;
    header->sizeofcmds = sizeof(struct load_command);
    if (mprotect(mapping + pageSize, pageSize, PROT_NONE) != 0) {
        (void)munmap(mapping, pageSize * 2);
        return false;
    }

    pid_t child = fork();
    if (child == 0) {
        void *result = RorkHookFindSymbolInFileImage(
            (const RorkHookMachHeader *)header,
            RorkHookTestFileImageSymbol);
        _exit(result == NULL ? 0 : 1);
    }

    int childStatus = 0;
    bool succeeded = child > 0 &&
        waitpid(child, &childStatus, 0) == child &&
        WIFEXITED(childStatus) &&
        WEXITSTATUS(childStatus) == 0;
    (void)munmap(mapping, pageSize * 2);
    return succeeded;
}

/// Moves a valid nlist record to an unaligned address and resolves it.
bool RorkHookTestSupportFileImageResolvesUnalignedSymbolTable(void) {
    RorkHookTestFileImageFixture fixture =
        RorkHookTestSupportCreateFileImageFixture(false);
    if (fixture.bytes == NULL) {
        return false;
    }

    struct symtab_command *symtab = RorkHookTestFileImageSymtab(fixture);
    struct nlist_64 symbol;
    memcpy(&symbol, fixture.bytes + symtab->symoff, sizeof(symbol));
    symtab->symoff += 1;
    memcpy(fixture.bytes + symtab->symoff, &symbol, sizeof(symbol));

    void *resolved = RorkHookTestResolveFileImageFixture(fixture);
    bool succeeded =
        resolved == fixture.bytes + fixture.symbolFileOffset;
    RorkHookTestSupportDestroyFileImageFixture(fixture);
    return succeeded;
}

/// Shrinks a segment's file-backed range so its symbol VM address has no file
/// representation.
bool RorkHookTestSupportFileImageRejectsNonFileBackedSymbol(void) {
    RorkHookTestFileImageFixture fixture =
        RorkHookTestSupportCreateFileImageFixture(false);
    if (fixture.bytes == NULL) {
        return false;
    }

    struct segment_command_64 *data =
        RorkHookTestFileImageDataSegment(fixture);
    data->filesize = fixture.symbolFileOffset - (size_t)data->fileoff;

    bool rejected = RorkHookTestResolveFileImageFixture(fixture) == NULL;
    RorkHookTestSupportDestroyFileImageFixture(fixture);
    return rejected;
}

/// Moves the nlist table far enough that its final byte exceeds the mapping.
bool RorkHookTestSupportFileImageRejectsOutOfBoundsSymbolTable(void) {
    RorkHookTestFileImageFixture fixture =
        RorkHookTestSupportCreateFileImageFixture(false);
    if (fixture.bytes == NULL) {
        return false;
    }

    struct symtab_command *symtab = RorkHookTestFileImageSymtab(fixture);
    symtab->symoff =
        (uint32_t)(fixture.size - sizeof(struct nlist_64) + 1);

    bool rejected = RorkHookTestResolveFileImageFixture(fixture) == NULL;
    RorkHookTestSupportDestroyFileImageFixture(fixture);
    return rejected;
}

/// Removes the final terminator from the declared string-table range.
bool RorkHookTestSupportFileImageRejectsUnterminatedSymbol(void) {
    RorkHookTestFileImageFixture fixture =
        RorkHookTestSupportCreateFileImageFixture(false);
    if (fixture.bytes == NULL) {
        return false;
    }

    struct symtab_command *symtab = RorkHookTestFileImageSymtab(fixture);
    symtab->strsize -= 1;

    bool rejected = RorkHookTestResolveFileImageFixture(fixture) == NULL;
    RorkHookTestSupportDestroyFileImageFixture(fixture);
    return rejected;
}

/// Appends malformed metadata after a valid symbol command to prove validation
/// covers commands that a lookup visitor does not otherwise need.
bool RorkHookTestSupportFileImageRejectsMalformedCommandAfterSymtab(void) {
    RorkHookTestFileImageFixture fixture =
        RorkHookTestSupportCreateFileImageFixture(false);
    if (fixture.bytes == NULL) {
        return false;
    }

    struct mach_header_64 *header =
        (struct mach_header_64 *)fixture.bytes;
    struct symtab_command *symtab =
        RorkHookTestFileImageSymtab(fixture);
    struct load_command *malformed =
        (struct load_command *)((uint8_t *)symtab + sizeof(*symtab));
    malformed->cmd = LC_UUID;
    malformed->cmdsize = sizeof(*malformed) - 1;
    header->ncmds += 1;
    header->sizeofcmds += sizeof(*malformed);

    bool rejected = RorkHookTestResolveFileImageFixture(fixture) == NULL;
    RorkHookTestSupportDestroyFileImageFixture(fixture);
    return rejected;
}

/// Constructs a loaded image whose symbol address plus slide cannot fit in a
/// native pointer.
bool RorkHookTestSupportLoadedImageRejectsSymbolAddressOverflow(void) {
    const size_t imageSize = 0x3000;
    const size_t symbolTableOffset = 0x2000;
    const size_t stringTableOffset = 0x2100;
    const uintptr_t slide = 0x200;
    uint8_t *bytes = calloc(1, imageSize);
    if (bytes == NULL) {
        return false;
    }

    struct mach_header_64 *header = (struct mach_header_64 *)bytes;
    header->magic = MH_MAGIC_64;
    header->cputype = CPU_TYPE_ARM64;
    header->cpusubtype = CPU_SUBTYPE_ARM64_ALL;
    header->filetype = MH_DYLIB;
    header->ncmds = 3;
    header->sizeofcmds =
        (uint32_t)(sizeof(struct segment_command_64) * 2 +
                   sizeof(struct symtab_command));

    uint8_t *commandBytes = bytes + sizeof(*header);
    struct segment_command_64 text = RorkHookTestSegment(
        SEG_TEXT,
        (uintptr_t)bytes - slide,
        0x1000,
        0,
        0x1000,
        VM_PROT_READ | VM_PROT_EXECUTE);
    memcpy(commandBytes, &text, sizeof(text));
    commandBytes += sizeof(text);

    struct segment_command_64 linkedit = RorkHookTestSegment(
        SEG_LINKEDIT,
        (uintptr_t)(bytes + symbolTableOffset) - slide,
        0x1000,
        symbolTableOffset,
        0x1000,
        VM_PROT_READ);
    memcpy(commandBytes, &linkedit, sizeof(linkedit));
    commandBytes += sizeof(linkedit);

    struct symtab_command symtab = {
        .cmd = LC_SYMTAB,
        .cmdsize = sizeof(struct symtab_command),
        .symoff = (uint32_t)symbolTableOffset,
        .nsyms = 1,
        .stroff = (uint32_t)stringTableOffset,
        .strsize = (uint32_t)(sizeof(RorkHookTestFileImageSymbol) + 1),
    };
    memcpy(commandBytes, &symtab, sizeof(symtab));

    struct nlist_64 symbol = {
        .n_un.n_strx = 1,
        .n_type = N_SECT,
        .n_sect = 1,
        .n_value = UINTPTR_MAX - 0x100,
    };
    memcpy(bytes + symbolTableOffset, &symbol, sizeof(symbol));
    bytes[stringTableOffset] = '\0';
    memcpy(
        bytes + stringTableOffset + 1,
        RorkHookTestFileImageSymbol,
        sizeof(RorkHookTestFileImageSymbol));

    void *resolved = RorkHookFindSymbol(
        (const RorkHookMachHeader *)header,
        RorkHookTestFileImageSymbol);
    free(bytes);
    return resolved == NULL;
}

/// Builds symbol metadata without a segment that can establish the image slide.
bool RorkHookTestSupportLoadedImageRejectsUnresolvedSlide(void) {
    const size_t imageSize = 0x3000;
    const size_t symbolTableOffset = 0x2000;
    const size_t stringTableOffset = 0x2100;
    const size_t symbolValueOffset = 0x1000;
    uint8_t *bytes = calloc(1, imageSize);
    if (bytes == NULL) {
        return false;
    }

    struct mach_header_64 *header = (struct mach_header_64 *)bytes;
    header->magic = MH_MAGIC_64;
    header->cputype = CPU_TYPE_ARM64;
    header->cpusubtype = CPU_SUBTYPE_ARM64_ALL;
    header->filetype = MH_DYLIB;
    header->ncmds = 2;
    header->sizeofcmds =
        (uint32_t)(sizeof(struct segment_command_64) +
                   sizeof(struct symtab_command));

    uint8_t *commandBytes = bytes + sizeof(*header);
    struct segment_command_64 linkedit = RorkHookTestSegment(
        SEG_LINKEDIT,
        (uintptr_t)bytes + symbolTableOffset,
        0x1000,
        symbolTableOffset,
        0x1000,
        VM_PROT_READ);
    memcpy(commandBytes, &linkedit, sizeof(linkedit));
    commandBytes += sizeof(linkedit);

    struct symtab_command symtab = {
        .cmd = LC_SYMTAB,
        .cmdsize = sizeof(struct symtab_command),
        .symoff = (uint32_t)symbolTableOffset,
        .nsyms = 1,
        .stroff = (uint32_t)stringTableOffset,
        .strsize = (uint32_t)(sizeof(RorkHookTestFileImageSymbol) + 1),
    };
    memcpy(commandBytes, &symtab, sizeof(symtab));

    struct nlist_64 symbol = {
        .n_un.n_strx = 1,
        .n_type = N_SECT,
        .n_sect = 1,
        .n_value = (uintptr_t)bytes + symbolValueOffset,
    };
    memcpy(bytes + symbolTableOffset, &symbol, sizeof(symbol));
    bytes[stringTableOffset] = '\0';
    memcpy(
        bytes + stringTableOffset + 1,
        RorkHookTestFileImageSymbol,
        sizeof(RorkHookTestFileImageSymbol));

    void *resolved = RorkHookFindSymbol(
        (const RorkHookMachHeader *)header,
        RorkHookTestFileImageSymbol);
    free(bytes);
    return resolved == NULL;
}

/// Places a readable string table beyond the declared `__LINKEDIT` file range.
bool RorkHookTestSupportLoadedImageRejectsMetadataOutsideLinkedit(void) {
    const size_t imageSize = 0x3000;
    const size_t symbolTableOffset = 0x2000;
    const size_t stringTableOffset = 0x2100;
    const uintptr_t slide = 0x200;
    uint8_t *bytes = calloc(1, imageSize);
    if (bytes == NULL) {
        return false;
    }

    struct mach_header_64 *header = (struct mach_header_64 *)bytes;
    header->magic = MH_MAGIC_64;
    header->cputype = CPU_TYPE_ARM64;
    header->cpusubtype = CPU_SUBTYPE_ARM64_ALL;
    header->filetype = MH_DYLIB;
    header->ncmds = 3;
    header->sizeofcmds =
        (uint32_t)(sizeof(struct segment_command_64) * 2 +
                   sizeof(struct symtab_command));

    uint8_t *commandBytes = bytes + sizeof(*header);
    struct segment_command_64 text = RorkHookTestSegment(
        SEG_TEXT,
        (uintptr_t)bytes - slide,
        0x1000,
        0,
        0x1000,
        VM_PROT_READ | VM_PROT_EXECUTE);
    memcpy(commandBytes, &text, sizeof(text));
    commandBytes += sizeof(text);

    struct segment_command_64 linkedit = RorkHookTestSegment(
        SEG_LINKEDIT,
        (uintptr_t)(bytes + symbolTableOffset) - slide,
        sizeof(struct nlist_64),
        symbolTableOffset,
        sizeof(struct nlist_64),
        VM_PROT_READ);
    memcpy(commandBytes, &linkedit, sizeof(linkedit));
    commandBytes += sizeof(linkedit);

    struct symtab_command symtab = {
        .cmd = LC_SYMTAB,
        .cmdsize = sizeof(struct symtab_command),
        .symoff = (uint32_t)symbolTableOffset,
        .nsyms = 1,
        .stroff = (uint32_t)stringTableOffset,
        .strsize = (uint32_t)(sizeof(RorkHookTestFileImageSymbol) + 1),
    };
    memcpy(commandBytes, &symtab, sizeof(symtab));

    struct nlist_64 symbol = {
        .n_un.n_strx = 1,
        .n_type = N_SECT,
        .n_sect = 1,
        .n_value = (uintptr_t)(bytes + 0x1000) - slide,
    };
    memcpy(bytes + symbolTableOffset, &symbol, sizeof(symbol));
    bytes[stringTableOffset] = '\0';
    memcpy(
        bytes + stringTableOffset + 1,
        RorkHookTestFileImageSymbol,
        sizeof(RorkHookTestFileImageSymbol));

    void *resolved = RorkHookFindSymbol(
        (const RorkHookMachHeader *)header,
        RorkHookTestFileImageSymbol);
    free(bytes);
    return resolved == NULL;
}

/// Complete in-memory Mach-O containing one mutable import slot.
typedef struct RorkHookTestRebindFixture {
    struct mach_header_64 header;
    struct segment_command_64 segment;
    struct section_64 section;
    void *slot;
} RorkHookTestRebindFixture;

/// Dedicated function addresses keep synthetic slot tests independent from the
/// process-wide imported `strcmp` slot mutated by the integration test.
static int RorkHookTestRebindOriginal(void) {
    return 1;
}

/// Replacement address stored into synthetic import slots.
static int RorkHookTestRebindReplacement(void) {
    return 2;
}

/// Initializes a synthetic loaded image with one symbol-pointer section.
static void RorkHookTestInitializeRebindFixture(RorkHookTestRebindFixture *fixture,
                                                const char *segmentName,
                                                const char *sectionName,
                                                void *slotValue) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->header.magic = MH_MAGIC_64;
    fixture->header.cputype = CPU_TYPE_ARM64;
    fixture->header.cpusubtype = CPU_SUBTYPE_ARM64_ALL;
    fixture->header.filetype = MH_DYLIB;
    fixture->header.ncmds = 1;
    fixture->header.sizeofcmds =
        (uint32_t)(sizeof(fixture->segment) + sizeof(fixture->section));

    fixture->segment.cmd = LC_SEGMENT_64;
    fixture->segment.cmdsize =
        (uint32_t)(sizeof(fixture->segment) + sizeof(fixture->section));
    strlcpy(
        fixture->segment.segname,
        segmentName,
        sizeof(fixture->segment.segname));
    fixture->segment.vmaddr = (uint64_t)(uintptr_t)fixture;
    fixture->segment.vmsize = sizeof(*fixture);
    fixture->segment.fileoff = 0;
    fixture->segment.filesize = sizeof(*fixture);
    fixture->segment.maxprot = VM_PROT_READ | VM_PROT_WRITE;
    fixture->segment.initprot = VM_PROT_READ | VM_PROT_WRITE;
    fixture->segment.nsects = 1;

    strlcpy(
        fixture->section.sectname,
        sectionName,
        sizeof(fixture->section.sectname));
    strlcpy(
        fixture->section.segname,
        segmentName,
        sizeof(fixture->section.segname));
    fixture->section.addr = (uint64_t)(uintptr_t)&fixture->slot;
    fixture->section.size = sizeof(fixture->slot);
    fixture->section.offset =
        (uint32_t)((uintptr_t)&fixture->slot - (uintptr_t)fixture);
    fixture->section.align = 3;
    fixture->section.flags = S_NON_LAZY_SYMBOL_POINTERS;
    fixture->slot = slotValue;
}

/// Exercises section discovery without relying on a dyld-loaded fixture image.
bool RorkHookTestSupportRebindsPointerSection(const char *segmentName) {
    RorkHookTestRebindFixture fixture;
    void *replacee = (void *)RorkHookTestRebindOriginal;
    void *replacement = (void *)RorkHookTestRebindReplacement;
    RorkHookTestInitializeRebindFixture(&fixture, segmentName, "__got", replacee);

    RorkHookRebindSymbolInImage(
        (const RorkHookMachHeader *)&fixture.header,
        replacee,
        replacement);
    return RorkHookTestStripFunctionPointer(fixture.slot) ==
           RorkHookTestStripFunctionPointer(replacement);
}

/// Proves that scanning an authenticated slot never authenticates an unknown
/// PAC schema merely to compare its underlying address.
bool RorkHookTestSupportRebindsForeignAuthenticatedPointer(void) {
#if __has_feature(ptrauth_calls)
    RorkHookTestRebindFixture fixture;
    void *replacee = (void *)RorkHookTestRebindOriginal;
    void *replacement = (void *)RorkHookTestRebindReplacement;
    void *rawReplacee = ptrauth_strip(replacee, ptrauth_key_function_pointer);
    void *foreignPointer = ptrauth_sign_unauthenticated(
        rawReplacee,
        ptrauth_key_process_dependent_code,
        0x524f524b);
    RorkHookTestInitializeRebindFixture(
        &fixture,
        "__AUTH_CONST",
        "__auth_got",
        foreignPointer);

    RorkHookRebindSymbolInImage(
        (const RorkHookMachHeader *)&fixture.header,
        replacee,
        replacement);
    void *stored = ptrauth_strip(fixture.slot, ptrauth_key_function_pointer);
    void *expected = ptrauth_strip(replacement, ptrauth_key_function_pointer);
    return stored == expected;
#else
    return true;
#endif
}

/// Sentinel implementation installed by process-wide rebinding tests.
static int RorkHookTestGlobalReplacement(void) {
    return 7331;
}

/// Rejects every image so the global filter contract can be observed directly.
static bool RorkHookTestRejectGlobalRebindImage(
    const RorkHookMachHeader *header) {
    (void)header;
    return false;
}

/// Loads a two-level-namespace fixture and verifies current or future image
/// processing through the public global-rebind API.
bool RorkHookTestSupportGloballyRebindsFixture(
    const char *providerPath,
    const char *consumerPath,
    bool loadConsumerBeforeRegistration) {
    void *provider = dlopen(providerPath, RTLD_NOW | RTLD_GLOBAL);
    if (provider == NULL) {
        return false;
    }
    void *replacee = dlsym(provider, "RorkHookGlobalFixtureOriginal");
    if (replacee == NULL) {
        (void)dlclose(provider);
        return false;
    }

    void *consumer = NULL;
    if (loadConsumerBeforeRegistration) {
        consumer = dlopen(consumerPath, RTLD_NOW | RTLD_LOCAL);
        if (consumer == NULL) {
            (void)dlclose(provider);
            return false;
        }
    }

    if (!RorkHookRebindSymbolGlobally(
            replacee,
            (void *)RorkHookTestGlobalReplacement,
            RORK_HOOK_NO_FILTER)) {
        if (consumer != NULL) {
            (void)dlclose(consumer);
        }
        (void)dlclose(provider);
        return false;
    }

    if (consumer == NULL) {
        consumer = dlopen(consumerPath, RTLD_NOW | RTLD_LOCAL);
        if (consumer == NULL) {
            return false;
        }
    }

    // Successful global registrations retain raw addresses from both images,
    // so the handles intentionally remain open for the rest of the test process.
    int (*callFixture)(void) =
        (int (*)(void))dlsym(consumer, "RorkHookGlobalFixtureCall");
    return callFixture != NULL && callFixture() == RorkHookTestGlobalReplacement();
}

/// Registers a permanent rebind whose filter rejects the consumer fixture.
bool RorkHookTestSupportGlobalRebindHonorsRejectingFilter(
    const char *providerPath,
    const char *consumerPath) {
    void *provider = dlopen(providerPath, RTLD_NOW | RTLD_GLOBAL);
    if (provider == NULL) {
        return false;
    }
    void *replacee = dlsym(provider, "RorkHookGlobalFixtureOriginal");
    if (replacee == NULL) {
        (void)dlclose(provider);
        return false;
    }
    if (!RorkHookRebindSymbolGlobally(
            replacee,
            (void *)RorkHookTestGlobalReplacement,
            RorkHookTestRejectGlobalRebindImage)) {
        (void)dlclose(provider);
        return false;
    }

    void *consumer = dlopen(consumerPath, RTLD_NOW | RTLD_LOCAL);
    if (consumer == NULL) {
        return false;
    }
    // The registration outlives this call, so both fixture images remain loaded.
    int (*callFixture)(void) =
        (int (*)(void))dlsym(consumer, "RorkHookGlobalFixtureCall");
    return callFixture != NULL && callFixture() == 17;
}

#if defined(__arm64__)

/// Provides a dedicated, sufficiently large prologue for destructive detours.
__attribute__((naked, noinline))
static int RorkHookTestDetourRedirectTarget(void) {
    __asm__ volatile(
        "mov w0, #7\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "ret\n");
}

/// Provides a second known prologue that rejection tests must leave unchanged.
__attribute__((naked, noinline))
static int RorkHookTestDetourRejectTarget(void) {
    __asm__ volatile(
        "mov w0, #11\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "ret\n");
}

/// Returns the sentinel value expected after a successful destructive detour.
static int RorkHookTestDetourReplacement(void) {
    return 29;
}

#endif

/// Isolates a rejected detour so tests can also verify the target remains intact.
bool RorkHookTestSupportCheckedDetourRejectsShortRegion(void) {
#if defined(__arm64__)
    kern_return_t result = RorkHookReplaceFunctionWithSize(
        (void *)RorkHookTestDetourRejectTarget,
        sizeof(uint32_t) * (RORK_HOOK_ABSOLUTE_JUMP_WORDS - 1),
        (void *)RorkHookTestDetourReplacement);
    return result == KERN_INVALID_ARGUMENT &&
        RorkHookTestDetourRejectTarget() == 11;
#else
    return true;
#endif
}

/// Performs and calls through a destructive detour in a disposable child.
bool RorkHookTestSupportCheckedDetourRedirectsTarget(void) {
#if defined(__arm64__)
    kern_return_t result = RorkHookReplaceFunctionWithSize(
        (void *)RorkHookTestDetourRedirectTarget,
        sizeof(uint32_t) * 6,
        (void *)RorkHookTestDetourReplacement);
    return result == KERN_SUCCESS &&
        RorkHookTestDetourRedirectTarget() == 29;
#else
    return true;
#endif
}

/// Passes an instruction-misaligned address to the checked detour API.
bool RorkHookTestSupportCheckedDetourRejectsUnalignedTarget(void) {
#if defined(__arm64__)
    void *unalignedTarget =
        (uint8_t *)(void *)RorkHookTestDetourRejectTarget + 1;
    return RorkHookReplaceFunctionWithSize(
               unalignedTarget,
               sizeof(uint32_t) * RORK_HOOK_ABSOLUTE_JUMP_WORDS,
               (void *)RorkHookTestDetourReplacement) ==
        KERN_INVALID_ARGUMENT;
#else
    return true;
#endif
}

/// Places a nominal patch range across a VM page boundary.
bool RorkHookTestSupportCheckedDetourRejectsCrossPageTarget(void) {
#if defined(__arm64__)
    size_t pageSize = 0;
    if (!RorkHookTestPageSize(&pageSize)) {
        return false;
    }
    uint8_t *mapping = mmap(
        NULL,
        pageSize * 2,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANON,
        -1,
        0);
    if (mapping == MAP_FAILED) {
        return false;
    }

    size_t patchSize =
        sizeof(uint32_t) * RORK_HOOK_ABSOLUTE_JUMP_WORDS;
    void *target = mapping + pageSize - sizeof(uint32_t) * 4;
    kern_return_t result = RorkHookReplaceFunctionWithSize(
        target,
        patchSize,
        (void *)RorkHookTestDetourReplacement);
    (void)munmap(mapping, pageSize * 2);
    return result == KERN_INVALID_ARGUMENT;
#else
    return true;
#endif
}

/// Uses a readable and writable mapping that deliberately lacks execute access.
bool RorkHookTestSupportCheckedDetourRejectsNonExecutableTarget(void) {
#if defined(__arm64__)
    size_t pageSize = 0;
    if (!RorkHookTestPageSize(&pageSize)) {
        return false;
    }
    void *mapping = mmap(
        NULL,
        pageSize,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANON,
        -1,
        0);
    if (mapping == MAP_FAILED) {
        return false;
    }

    kern_return_t result = RorkHookReplaceFunctionWithSize(
        mapping,
        sizeof(uint32_t) * RORK_HOOK_ABSOLUTE_JUMP_WORDS,
        (void *)RorkHookTestDetourReplacement);
    (void)munmap(mapping, pageSize);
    return result == KERN_PROTECTION_FAILURE;
#else
    return true;
#endif
}

/// Owns the main-cache and local-symbol buffers used by parser tests.
///
/// `symbolsBytes` aliases `mainBytes` for legacy inline layouts and owns a
/// separate allocation for modern sidecar layouts.
typedef struct RorkHookTestSharedCacheFixture {
    uint8_t *mainBytes;
    size_t mainSize;
    uint8_t *symbolsBytes;
    size_t symbolsSize;
    uintptr_t slide;
    uintptr_t symbolValue;
} RorkHookTestSharedCacheFixture;

/// Install name stored in the synthetic main-cache image table.
static const char RorkHookTestSharedCacheImagePath[] =
    "/System/Library/Frameworks/RorkFixture.framework/RorkFixture";

/// Local symbol stored in the synthetic cache's nlist and string tables.
static const char RorkHookTestSharedCacheSymbol[] = "_RorkHookSharedCacheFixture";

/// Writes a value into a fixture only when its destination range is available.
static bool RorkHookTestWriteFixtureBytes(uint8_t *bytes,
                                          size_t size,
                                          size_t offset,
                                          const void *value,
                                          size_t valueSize) {
    if (bytes == NULL ||
        value == NULL ||
        valueSize == 0 ||
        offset > size ||
        valueSize > size - offset) {
        return false;
    }
    memcpy(bytes + offset, value, valueSize);
    return true;
}

/// Builds the minimal image table and local-symbol metadata consumed by the
/// parser, with independent modern/legacy and sidecar/inline layouts.
static RorkHookTestSharedCacheFixture RorkHookTestCreateSharedCacheFixture(
    bool usesSidecar,
    bool uses64BitEntry,
    const char *symbolName) {
    if (symbolName == NULL) {
        return (RorkHookTestSharedCacheFixture){0};
    }

    size_t symbolNameLength = strlen(symbolName);
    if (symbolNameLength > UINT32_MAX - 2) {
        return (RorkHookTestSharedCacheFixture){0};
    }

    const size_t mainSize = 0x3000;
    const size_t symbolsSize = usesSidecar ? 0x3000 : mainSize;
    const size_t imageTableOffset = 0x400;
    const size_t imagePathOffset = 0x500;
    const size_t localSymbolsOffset = 0x1000;
    const size_t entriesOffset = sizeof(RorkHookDyldCacheLocalSymbolsInfo);
    const size_t nlistOffset = 0x100;
    const size_t stringsOffset = 0x300;
    const uintptr_t slide = 0x100000;
    const uintptr_t symbolValue = 0x2340;

    uint8_t *mainBytes = calloc(1, mainSize);
    uint8_t *symbolsBytes = usesSidecar ? calloc(1, symbolsSize) : mainBytes;
    if (mainBytes == NULL || symbolsBytes == NULL) {
        free(mainBytes);
        if (usesSidecar) {
            free(symbolsBytes);
        }
        return (RorkHookTestSharedCacheFixture){0};
    }

    RorkHookDyldCacheHeader *mainHeader =
        (RorkHookDyldCacheHeader *)mainBytes;
    memcpy(mainHeader->magic, "dyld_v1  arm64e", sizeof(mainHeader->magic));
    if (uses64BitEntry) {
        mainHeader->mappingOffset =
            (uint32_t)offsetof(RorkHookDyldCacheHeader, symbolFileUUID);
        mainHeader->imagesOffset = (uint32_t)imageTableOffset;
        mainHeader->imagesCount = 1;
    } else {
        mainHeader->mappingOffset =
            (uint32_t)offsetof(RorkHookDyldCacheHeader, mappingWithSlideOffset);
        mainHeader->imagesOffsetOld = (uint32_t)imageTableOffset;
        mainHeader->imagesCountOld = 1;
    }

    RorkHookDyldCacheImageInfo imageInfo = {
        .pathFileOffset = (uint32_t)imagePathOffset,
    };
    bool metadataWritten =
        RorkHookTestWriteFixtureBytes(
            mainBytes,
            mainSize,
            imageTableOffset,
            &imageInfo,
            sizeof(imageInfo)) &&
        RorkHookTestWriteFixtureBytes(
            mainBytes,
            mainSize,
            imagePathOffset,
            RorkHookTestSharedCacheImagePath,
            sizeof(RorkHookTestSharedCacheImagePath));

    RorkHookDyldCacheHeader *symbolsHeader =
        (RorkHookDyldCacheHeader *)symbolsBytes;
    if (usesSidecar) {
        memcpy(
            symbolsHeader->magic,
            "dyld_v1  arm64e",
            sizeof(symbolsHeader->magic));
    }
    symbolsHeader->localSymbolsOffset = localSymbolsOffset;
    symbolsHeader->localSymbolsSize = symbolsSize - localSymbolsOffset;

    RorkHookDyldCacheLocalSymbolsInfo symbolsInfo = {
        .nlistOffset = (uint32_t)nlistOffset,
        .nlistCount = 1,
        .stringsOffset = (uint32_t)stringsOffset,
        .stringsSize = (uint32_t)(symbolNameLength + 2),
        .entriesOffset = (uint32_t)entriesOffset,
        .entriesCount = 1,
    };
    metadataWritten =
        metadataWritten &&
        RorkHookTestWriteFixtureBytes(
            symbolsBytes,
            symbolsSize,
            localSymbolsOffset,
            &symbolsInfo,
            sizeof(symbolsInfo));

    bool entryWritten = false;
    if (uses64BitEntry) {
        RorkHookDyldCacheLocalSymbolsEntry64 entry = {
            .dylibOffset = 0,
            .nlistStartIndex = 0,
            .nlistCount = 1,
        };
        entryWritten = RorkHookTestWriteFixtureBytes(
            symbolsBytes,
            symbolsSize,
            localSymbolsOffset + entriesOffset,
            &entry,
            sizeof(entry));
    } else {
        RorkHookDyldCacheLocalSymbolsEntry32 entry = {
            .dylibOffset = 0,
            .nlistStartIndex = 0,
            .nlistCount = 1,
        };
        entryWritten = RorkHookTestWriteFixtureBytes(
            symbolsBytes,
            symbolsSize,
            localSymbolsOffset + entriesOffset,
            &entry,
            sizeof(entry));
    }

    struct nlist_64 symbol = {
        .n_un.n_strx = 1,
        .n_type = N_SECT,
        .n_sect = 1,
        .n_value = symbolValue,
    };
    const char stringTablePrefix = '\0';
    bool symbolWritten =
        RorkHookTestWriteFixtureBytes(
            symbolsBytes,
            symbolsSize,
            localSymbolsOffset + nlistOffset,
            &symbol,
            sizeof(symbol)) &&
        RorkHookTestWriteFixtureBytes(
            symbolsBytes,
            symbolsSize,
            localSymbolsOffset + stringsOffset,
            &stringTablePrefix,
            sizeof(stringTablePrefix)) &&
        RorkHookTestWriteFixtureBytes(
            symbolsBytes,
            symbolsSize,
            localSymbolsOffset + stringsOffset + 1,
            symbolName,
            symbolNameLength + 1);
    if (!metadataWritten || !entryWritten || !symbolWritten) {
        if (usesSidecar) {
            free(symbolsBytes);
        }
        free(mainBytes);
        return (RorkHookTestSharedCacheFixture){0};
    }

    return (RorkHookTestSharedCacheFixture){
        .mainBytes = mainBytes,
        .mainSize = mainSize,
        .symbolsBytes = symbolsBytes,
        .symbolsSize = symbolsSize,
        .slide = slide,
        .symbolValue = symbolValue,
    };
}

/// Releases both buffers while respecting inline-symbol ownership.
static void RorkHookTestDestroySharedCacheFixture(
    RorkHookTestSharedCacheFixture fixture) {
    if (fixture.symbolsBytes != fixture.mainBytes) {
        free(fixture.symbolsBytes);
    }
    free(fixture.mainBytes);
}

/// Resolves the fixture's symbol through the production bounded parser.
static bool RorkHookTestSharedCacheFixtureResolves(
    RorkHookTestSharedCacheFixture fixture,
    const char *symbolName) {
    if (fixture.mainBytes == NULL ||
        fixture.symbolsBytes == NULL ||
        symbolName == NULL) {
        return false;
    }
    void *resolved = RorkHookFindSharedCacheSymbolInData(
        fixture.mainBytes,
        fixture.mainSize,
        fixture.symbolsBytes,
        fixture.symbolsSize,
        fixture.slide,
        RorkHookTestSharedCacheImagePath,
        symbolName);
    return resolved ==
        (void *)(fixture.slide + fixture.symbolValue);
}

/// Exercises the modern main-cache plus 64-bit sidecar entry layout.
bool RorkHookTestSupportSharedCacheResolvesSidecar(void) {
    RorkHookTestSharedCacheFixture fixture =
        RorkHookTestCreateSharedCacheFixture(
            true,
            true,
            RorkHookTestSharedCacheSymbol);
    bool resolved = RorkHookTestSharedCacheFixtureResolves(
        fixture,
        RorkHookTestSharedCacheSymbol);
    RorkHookTestDestroySharedCacheFixture(fixture);
    return resolved;
}

/// Exercises the legacy inline-symbol layout and 32-bit image entry.
bool RorkHookTestSupportSharedCacheResolvesLegacyInlineSymbols(void) {
    RorkHookTestSharedCacheFixture fixture =
        RorkHookTestCreateSharedCacheFixture(
            false,
            false,
            RorkHookTestSharedCacheSymbol);
    bool resolved = RorkHookTestSharedCacheFixtureResolves(
        fixture,
        RorkHookTestSharedCacheSymbol);
    RorkHookTestDestroySharedCacheFixture(fixture);
    return resolved;
}

/// Builds a symbol name larger than the parser's historical fixed buffer.
bool RorkHookTestSupportSharedCacheResolvesLongSymbolName(void) {
    const size_t nameLength = 1536;
    char *symbolName = malloc(nameLength + 1);
    if (symbolName == NULL) {
        return false;
    }
    symbolName[0] = '_';
    memset(symbolName + 1, 'R', nameLength - 1);
    symbolName[nameLength] = '\0';

    RorkHookTestSharedCacheFixture fixture =
        RorkHookTestCreateSharedCacheFixture(true, true, symbolName);
    bool resolved = RorkHookTestSharedCacheFixtureResolves(fixture, symbolName);
    RorkHookTestDestroySharedCacheFixture(fixture);
    free(symbolName);
    return resolved;
}

/// Writes a complete fixture buffer to a newly created test file.
static bool RorkHookTestWriteFile(const char *path,
                                  const uint8_t *bytes,
                                  size_t size) {
    if (path == NULL || bytes == NULL || size == 0) {
        return false;
    }

    int descriptor = open(
        path,
        O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
        0600);
    if (descriptor < 0) {
        return false;
    }

    size_t written = 0;
    while (written < size) {
        ssize_t result = write(descriptor, bytes + written, size - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            (void)close(descriptor);
            return false;
        }
        written += (size_t)result;
    }
    return close(descriptor) == 0;
}

/// Writes a complete main cache and sidecar, then resolves them through mmap.
bool RorkHookTestSupportSharedCacheResolvesMappedFiles(void) {
    RorkHookTestSharedCacheFixture fixture =
        RorkHookTestCreateSharedCacheFixture(
            true,
            true,
            RorkHookTestSharedCacheSymbol);
    if (fixture.mainBytes == NULL || fixture.symbolsBytes == NULL) {
        RorkHookTestDestroySharedCacheFixture(fixture);
        return false;
    }

    char directoryTemplate[] = "/tmp/rork-hook-cache-XXXXXX";
    char *directory = mkdtemp(directoryTemplate);
    if (directory == NULL) {
        RorkHookTestDestroySharedCacheFixture(fixture);
        return false;
    }

    char mainPath[PATH_MAX];
    char symbolsPath[PATH_MAX];
    int mainPathLength = snprintf(
        mainPath,
        sizeof(mainPath),
        "%s/cache",
        directory);
    int symbolsPathLength =
        mainPathLength >= 0 &&
        (size_t)mainPathLength < sizeof(mainPath)
        ? snprintf(
              symbolsPath,
              sizeof(symbolsPath),
              "%s.symbols",
              mainPath)
        : -1;
    bool pathsValid =
        symbolsPathLength >= 0 &&
        (size_t)symbolsPathLength < sizeof(symbolsPath);
    bool filesWritten = pathsValid &&
        RorkHookTestWriteFile(
            mainPath,
            fixture.mainBytes,
            fixture.mainSize) &&
        RorkHookTestWriteFile(
            symbolsPath,
            fixture.symbolsBytes,
            fixture.symbolsSize);
    void *resolved = filesWritten
        ? RorkHookFindSharedCacheSymbolAtPath(
              mainPath,
              fixture.slide,
              RorkHookTestSharedCacheImagePath,
              RorkHookTestSharedCacheSymbol)
        : NULL;

    if (pathsValid) {
        (void)unlink(symbolsPath);
        (void)unlink(mainPath);
    }
    (void)rmdir(directory);
    bool succeeded =
        resolved == (void *)(fixture.slide + fixture.symbolValue);
    RorkHookTestDestroySharedCacheFixture(fixture);
    return succeeded;
}

/// Proves the cache reader validates the file type before attempting to map it.
bool RorkHookTestSupportSharedCacheRejectsNonRegularFile(void) {
    char directoryTemplate[] = "/tmp/rork-hook-fifo-XXXXXX";
    char *directory = mkdtemp(directoryTemplate);
    if (directory == NULL) {
        return false;
    }

    char fifoPath[PATH_MAX];
    int written = snprintf(
        fifoPath,
        sizeof(fifoPath),
        "%s/cache",
        directory);
    if (written < 0 ||
        (size_t)written >= sizeof(fifoPath) ||
        mkfifo(fifoPath, 0600) != 0) {
        (void)rmdir(directory);
        return false;
    }

    // Keeping both FIFO ends open prevents a blocking implementation from
    // stalling the test before it can reject the non-regular file type.
    int peer = open(fifoPath, O_RDWR | O_NONBLOCK);
    if (peer < 0) {
        (void)unlink(fifoPath);
        (void)rmdir(directory);
        return false;
    }

    void *resolved = RorkHookFindSharedCacheSymbolAtPath(
        fifoPath,
        0,
        RorkHookTestSharedCacheImagePath,
        RorkHookTestSharedCacheSymbol);

    (void)close(peer);
    (void)unlink(fifoPath);
    (void)rmdir(directory);
    return resolved == NULL;
}

/// Truncates the main mapping before its declared image table.
bool RorkHookTestSupportSharedCacheRejectsTruncatedImageTable(void) {
    RorkHookTestSharedCacheFixture fixture =
        RorkHookTestCreateSharedCacheFixture(
            true,
            true,
            RorkHookTestSharedCacheSymbol);
    void *resolved = RorkHookFindSharedCacheSymbolInData(
        fixture.mainBytes,
        sizeof(RorkHookDyldCacheHeader),
        fixture.symbolsBytes,
        fixture.symbolsSize,
        fixture.slide,
        RorkHookTestSharedCacheImagePath,
        RorkHookTestSharedCacheSymbol);
    RorkHookTestDestroySharedCacheFixture(fixture);
    return resolved == NULL;
}

/// Places the nlist table at an offset whose declared extent cannot fit.
bool RorkHookTestSupportSharedCacheRejectsOverflowedSymbolRange(void) {
    RorkHookTestSharedCacheFixture fixture =
        RorkHookTestCreateSharedCacheFixture(
            true,
            true,
            RorkHookTestSharedCacheSymbol);
    RorkHookDyldCacheHeader *symbolsHeader =
        (RorkHookDyldCacheHeader *)fixture.symbolsBytes;
    RorkHookDyldCacheLocalSymbolsInfo *symbolsInfo =
        (RorkHookDyldCacheLocalSymbolsInfo *)(
            fixture.symbolsBytes + symbolsHeader->localSymbolsOffset);
    symbolsInfo->nlistOffset = UINT32_MAX;
    symbolsInfo->nlistCount = UINT32_MAX;

    void *resolved = RorkHookFindSharedCacheSymbolInData(
        fixture.mainBytes,
        fixture.mainSize,
        fixture.symbolsBytes,
        fixture.symbolsSize,
        fixture.slide,
        RorkHookTestSharedCacheImagePath,
        RorkHookTestSharedCacheSymbol);
    RorkHookTestDestroySharedCacheFixture(fixture);
    return resolved == NULL;
}

/// Replaces the complete declared string pool with non-NUL bytes.
bool RorkHookTestSupportSharedCacheRejectsUnterminatedSymbol(void) {
    RorkHookTestSharedCacheFixture fixture =
        RorkHookTestCreateSharedCacheFixture(
            true,
            true,
            RorkHookTestSharedCacheSymbol);
    RorkHookDyldCacheHeader *symbolsHeader =
        (RorkHookDyldCacheHeader *)fixture.symbolsBytes;
    RorkHookDyldCacheLocalSymbolsInfo *symbolsInfo =
        (RorkHookDyldCacheLocalSymbolsInfo *)(
            fixture.symbolsBytes + symbolsHeader->localSymbolsOffset);
    size_t stringOffset =
        (size_t)symbolsHeader->localSymbolsOffset +
        symbolsInfo->stringsOffset;
    memset(
        fixture.symbolsBytes + stringOffset,
        'X',
        symbolsInfo->stringsSize);

    void *resolved = RorkHookFindSharedCacheSymbolInData(
        fixture.mainBytes,
        fixture.mainSize,
        fixture.symbolsBytes,
        fixture.symbolsSize,
        fixture.slide,
        RorkHookTestSharedCacheImagePath,
        RorkHookTestSharedCacheSymbol);
    RorkHookTestDestroySharedCacheFixture(fixture);
    return resolved == NULL;
}

/// Moves the local-symbol region one byte beyond the sidecar mapping.
bool RorkHookTestSupportSharedCacheRejectsOutOfBoundsLocalSymbols(void) {
    RorkHookTestSharedCacheFixture fixture =
        RorkHookTestCreateSharedCacheFixture(
            true,
            true,
            RorkHookTestSharedCacheSymbol);
    RorkHookDyldCacheHeader *symbolsHeader =
        (RorkHookDyldCacheHeader *)fixture.symbolsBytes;
    symbolsHeader->localSymbolsOffset = fixture.symbolsSize + 1;
    symbolsHeader->localSymbolsSize = 0;

    void *resolved = RorkHookFindSharedCacheSymbolInData(
        fixture.mainBytes,
        fixture.mainSize,
        fixture.symbolsBytes,
        fixture.symbolsSize,
        fixture.slide,
        RorkHookTestSharedCacheImagePath,
        RorkHookTestSharedCacheSymbol);
    RorkHookTestDestroySharedCacheFixture(fixture);
    return resolved == NULL;
}

/// Makes one image-local range begin at the end of the complete nlist table.
bool RorkHookTestSupportSharedCacheRejectsEntryBeyondSymbolTable(void) {
    RorkHookTestSharedCacheFixture fixture =
        RorkHookTestCreateSharedCacheFixture(
            true,
            true,
            RorkHookTestSharedCacheSymbol);
    RorkHookDyldCacheHeader *symbolsHeader =
        (RorkHookDyldCacheHeader *)fixture.symbolsBytes;
    RorkHookDyldCacheLocalSymbolsInfo *symbolsInfo =
        (RorkHookDyldCacheLocalSymbolsInfo *)(
            fixture.symbolsBytes + symbolsHeader->localSymbolsOffset);
    RorkHookDyldCacheLocalSymbolsEntry64 *entry =
        (RorkHookDyldCacheLocalSymbolsEntry64 *)(
            (uint8_t *)symbolsInfo + symbolsInfo->entriesOffset);
    entry->nlistStartIndex = symbolsInfo->nlistCount;
    entry->nlistCount = 1;

    void *resolved = RorkHookFindSharedCacheSymbolInData(
        fixture.mainBytes,
        fixture.mainSize,
        fixture.symbolsBytes,
        fixture.symbolsSize,
        fixture.slide,
        RorkHookTestSharedCacheImagePath,
        RorkHookTestSharedCacheSymbol);
    RorkHookTestDestroySharedCacheFixture(fixture);
    return resolved == NULL;
}

/// Sets a symbol value that overflows when the shared-cache slide is applied.
bool RorkHookTestSupportSharedCacheRejectsAddressOverflow(void) {
    RorkHookTestSharedCacheFixture fixture =
        RorkHookTestCreateSharedCacheFixture(
            true,
            true,
            RorkHookTestSharedCacheSymbol);
    RorkHookDyldCacheHeader *symbolsHeader =
        (RorkHookDyldCacheHeader *)fixture.symbolsBytes;
    RorkHookDyldCacheLocalSymbolsInfo *symbolsInfo =
        (RorkHookDyldCacheLocalSymbolsInfo *)(
            fixture.symbolsBytes + symbolsHeader->localSymbolsOffset);
    struct nlist_64 *symbol = (struct nlist_64 *)(
        (uint8_t *)symbolsInfo + symbolsInfo->nlistOffset);
    symbol->n_value = UINTPTR_MAX;

    void *resolved = RorkHookFindSharedCacheSymbolInData(
        fixture.mainBytes,
        fixture.mainSize,
        fixture.symbolsBytes,
        fixture.symbolsSize,
        1,
        RorkHookTestSharedCacheImagePath,
        RorkHookTestSharedCacheSymbol);
    RorkHookTestDestroySharedCacheFixture(fixture);
    return resolved == NULL;
}

/// Visitor used only to prove whether a syntactically valid command is reached.
static bool RorkHookTestVisitLoadCommand(const struct load_command *command,
                                         uint32_t index,
                                         void *context) {
    (void)command;
    (void)index;
    bool *visited = context;
    *visited = true;
    return true;
}

/// Stops after the first command so tests can verify that validation still
/// covers the unvisited remainder of the declared load-command table.
static bool RorkHookTestStopAfterFirstLoadCommand(
    const struct load_command *command,
    uint32_t index,
    void *context) {
    (void)command;
    (void)index;
    (void)context;
    return false;
}

/// Smallest valid bounded load-command image used by metadata mutations.
typedef struct RorkHookTestLoadCommandImage {
    struct mach_header_64 header;
    struct load_command command;
} RorkHookTestLoadCommandImage;

/// Mutates independent load-command invariants and verifies each is rejected.
bool RorkHookTestSupportRejectsMalformedLoadCommands(void) {
    RorkHookTestLoadCommandImage image = {0};
    image.header.magic = MH_MAGIC_64;
    image.header.ncmds = 1;
    image.header.sizeofcmds = sizeof(image.command);
    image.command.cmd = LC_UUID;
    image.command.cmdsize = sizeof(image.command);

    bool visited = false;
    if (!RorkHookForEachLoadCommandWithSize(
            (const RorkHookMachHeader *)&image.header,
            sizeof(image),
            RorkHookTestVisitLoadCommand,
            &visited) ||
        !visited) {
        return false;
    }

    struct mach_header_64 invalidMagic = image.header;
    invalidMagic.magic = 0;
    if (RorkHookForEachLoadCommandWithSize(
            (const RorkHookMachHeader *)&invalidMagic,
            sizeof(invalidMagic),
            RorkHookTestVisitLoadCommand,
            &visited)) {
        return false;
    }

    struct mach_header_64 excessiveCount = image.header;
    excessiveCount.ncmds = 2;
    if (RorkHookForEachLoadCommandWithSize(
            (const RorkHookMachHeader *)&excessiveCount,
            sizeof(image),
            RorkHookTestVisitLoadCommand,
            &visited)) {
        return false;
    }

    RorkHookTestLoadCommandImage invalidCommand = image;
    invalidCommand.command.cmdsize = sizeof(struct load_command) - 1;
    if (RorkHookForEachLoadCommandWithSize(
            (const RorkHookMachHeader *)&invalidCommand.header,
            sizeof(invalidCommand),
            RorkHookTestVisitLoadCommand,
            &visited)) {
        return false;
    }

    uint8_t misalignedStorage[
        sizeof(struct mach_header_64) + sizeof(struct load_command) + 1
    ] __attribute__((aligned(__alignof__(struct mach_header_64)))) = {0};
    struct mach_header_64 *misalignedHeader =
        (struct mach_header_64 *)misalignedStorage;
    struct load_command *misalignedCommand =
        (struct load_command *)(misalignedHeader + 1);
    misalignedHeader->magic = MH_MAGIC_64;
    misalignedHeader->ncmds = 1;
    misalignedHeader->sizeofcmds = sizeof(struct load_command) + 1;
    misalignedCommand->cmd = LC_UUID;
    misalignedCommand->cmdsize = sizeof(struct load_command) + 1;
    if (RorkHookForEachLoadCommandWithSize(
            (const RorkHookMachHeader *)misalignedHeader,
            sizeof(misalignedStorage),
            RorkHookTestVisitLoadCommand,
            &visited)) {
        return false;
    }

    struct {
        struct mach_header_64 header;
        struct load_command command;
        uint64_t trailingBytes;
    } oversizedTable = {
        .header = image.header,
        .command = image.command,
        .trailingBytes = 0,
    };
    oversizedTable.header.sizeofcmds += sizeof(oversizedTable.trailingBytes);
    if (RorkHookForEachLoadCommandWithSize(
            (const RorkHookMachHeader *)&oversizedTable.header,
            sizeof(oversizedTable),
            RorkHookTestVisitLoadCommand,
            &visited)) {
        return false;
    }

    struct {
        struct mach_header_64 header;
        struct load_command first;
        struct load_command malformed;
    } malformedAfterEarlyStop = {
        .header = {
            .magic = MH_MAGIC_64,
            .ncmds = 2,
            .sizeofcmds =
                sizeof(struct load_command) * 2,
        },
        .first = {
            .cmd = LC_UUID,
            .cmdsize = sizeof(struct load_command),
        },
        .malformed = {
            .cmd = LC_UUID,
            .cmdsize = sizeof(struct load_command) - 1,
        },
    };
    if (RorkHookForEachLoadCommandWithSize(
            (const RorkHookMachHeader *)&malformedAfterEarlyStop.header,
            sizeof(malformedAfterEarlyStop),
            RorkHookTestStopAfterFirstLoadCommand,
            NULL)) {
        return false;
    }

    return !RorkHookForEachLoadCommandWithSize(
        (const RorkHookMachHeader *)&image.header,
        sizeof(image.header),
        RorkHookTestVisitLoadCommand,
        &visited);
}

/// Queries an address below the first user mapping to catch next-region errors.
bool RorkHookTestSupportRejectsUnmappedMemoryRegion(void) {
    vm_prot_t protection = 0;
    return !RorkHookMemoryProtection((const void *)(uintptr_t)1, &protection);
}

/// Evaluates the decision matrix for ordinary and TPRO-gated pointer stores.
bool RorkHookTestSupportProtectedPointerWriteRequiresTPROWindow(void) {
    return RorkHookProtectedPointerWriteIsAvailable(
               KERN_SUCCESS,
               false,
               false) &&
        RorkHookProtectedPointerWriteIsAvailable(
               KERN_FAILURE,
               true,
               true) &&
        !RorkHookProtectedPointerWriteIsAvailable(
               KERN_SUCCESS,
               true,
               false) &&
        !RorkHookProtectedPointerWriteIsAvailable(
               KERN_FAILURE,
               false,
               false);
}

/// Forwards synthetic process inputs to the internal TPRO policy function.
bool RorkHookTestSupportProcessUsesTPRO(
    bool hardwareSupportsTPRO,
    bool processConfigurationKnown,
    bool processConfigurationEnablesTPRO,
    bool writeWindowProbeSucceeded) {
    return RorkHookProcessUsesTPRO(
        hardwareSupportsTPRO,
        processConfigurationKnown,
        processConfigurationEnablesTPRO,
        writeWindowProbeSucceeded);
}
