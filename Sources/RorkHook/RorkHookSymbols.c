#include "RorkHookSymbols.h"

#include "RorkHookInternal.h"

#include <mach-o/nlist.h>
#include <string.h>

/// Symbol metadata retained after one validated load-command pass.
typedef struct RorkHookFindSymtabContext {
    const struct symtab_command *symtab;
    const RorkHookSegmentCommand *linkedit;
} RorkHookFindSymtabContext;

/// Visitor that captures `LC_SYMTAB` and, when needed, the `__LINKEDIT` segment.
static bool RorkHookFindSymtabLoadCommand(const struct load_command *command,
                                          uint32_t index,
                                          void *contextRaw) {
    (void)index;
    if (command == NULL || contextRaw == NULL) {
        return false;
    }

    RorkHookFindSymtabContext *context =
        (RorkHookFindSymtabContext *)contextRaw;
    if (command->cmd == LC_SYMTAB &&
        command->cmdsize >= sizeof(struct symtab_command)) {
        context->symtab =
            (const struct symtab_command *)(const void *)command;
    } else if (command->cmd == RORK_HOOK_LC_SEGMENT &&
               command->cmdsize >= sizeof(RorkHookSegmentCommand)) {
        const RorkHookSegmentCommand *segment =
            (const RorkHookSegmentCommand *)(const void *)command;
        if (strncmp(
                segment->segname,
                SEG_LINKEDIT,
                sizeof(segment->segname)) == 0) {
            context->linkedit = segment;
        }
    }
    return true;
}

/// Locates symbol metadata within one explicitly bounded Mach-O mapping.
///
/// Live-image callers pass the readable length of the header's VM region;
/// file-image callers pass the mapping length supplied by their caller. Output
/// storage is cleared before validation so malformed input cannot leave stale
/// metadata behind.
static const struct symtab_command *RorkHookFindSymtab(
    const RorkHookMachHeader *header,
    size_t mappedSize,
    const RorkHookSegmentCommand **linkeditOut) {
    if (linkeditOut != NULL) {
        *linkeditOut = NULL;
    }

    RorkHookFindSymtabContext context = {
        .symtab = NULL,
        .linkedit = NULL,
    };
    if (!RorkHookForEachLoadCommandWithSize(
            header,
            mappedSize,
            RorkHookFindSymtabLoadCommand,
            &context)) {
        return NULL;
    }
    if (linkeditOut != NULL) {
        *linkeditOut = context.linkedit;
    }
    return context.symtab;
}

/// Checks a byte range without allowing addition to wrap around `size_t`.
static bool RorkHookRangeIsValid(size_t offset, size_t length, size_t totalSize) {
    return offset <= totalSize && length <= totalSize - offset;
}

/// Computes the byte length of an nlist array without integer overflow.
static bool RorkHookSymbolTableSize(uint32_t symbolCount, size_t *sizeOut) {
    if (sizeOut == NULL) {
        return false;
    }

    size_t size = 0;
    if (__builtin_mul_overflow(
            (size_t)symbolCount,
            sizeof(RorkHookNList),
            &size)) {
        return false;
    }
    *sizeOut = size;
    return true;
}

/// Converts an absolute file range into a segment-relative range.
///
/// Both the start and every byte in the range must be backed by the segment's
/// declared file content. The relative offset is written only on success.
static bool RorkHookFileRangeInSegment(
    uint64_t fileOffset,
    uint64_t length,
    const RorkHookSegmentCommand *segment,
    uint64_t *relativeOffsetOut) {
    if (segment == NULL ||
        relativeOffsetOut == NULL ||
        fileOffset < segment->fileoff) {
        return false;
    }

    uint64_t relativeOffset = fileOffset - segment->fileoff;
    if (relativeOffset > segment->filesize ||
        length > segment->filesize - relativeOffset) {
        return false;
    }
    *relativeOffsetOut = relativeOffset;
    return true;
}

/// Copies the matching N_SECT symbol after validating every table read.
static bool RorkHookFindSymbolEntry(const void *symbols,
                                    const char *stringTable,
                                    size_t stringTableSize,
                                    uint32_t symbolCount,
                                    const char *symbolName,
                                    RorkHookNList *entryOut) {
    if (symbols == NULL ||
        stringTable == NULL ||
        symbolName == NULL ||
        entryOut == NULL) {
        return false;
    }

    size_t symbolNameLength = strlen(symbolName);
    for (uint32_t index = 0; index < symbolCount; index += 1) {
        RorkHookNList entry;
        memcpy(
            &entry,
            (const uint8_t *)symbols + (size_t)index * sizeof(entry),
            sizeof(entry));
        uint32_t stringOffset = entry.n_un.n_strx;
        if (stringOffset == 0 || stringOffset >= stringTableSize ||
            (entry.n_type & N_TYPE) != N_SECT) {
            continue;
        }

        const char *candidate = stringTable + stringOffset;
        size_t remaining = stringTableSize - stringOffset;
        const char *terminator = memchr(candidate, '\0', remaining);
        if (terminator == NULL) {
            continue;
        }

        size_t candidateLength = (size_t)(terminator - candidate);
        if (candidateLength == symbolNameLength &&
            memcmp(candidate, symbolName, candidateLength) == 0) {
            *entryOut = entry;
            return true;
        }
    }
    return false;
}

/// Applies a signed VM slide without allowing native-pointer wraparound.
static bool RorkHookApplySlide(uint64_t address,
                               intptr_t slide,
                               uintptr_t *resultOut) {
    if (resultOut == NULL || address > UINTPTR_MAX) {
        return false;
    }

    uintptr_t nativeAddress = (uintptr_t)address;
    if (slide < 0) {
        uintptr_t magnitude = (uintptr_t)(-(slide + 1)) + 1;
        if (nativeAddress < magnitude) {
            return false;
        }
        *resultOut = nativeAddress - magnitude;
        return true;
    }
    uintptr_t magnitude = (uintptr_t)slide;
    if (nativeAddress > UINTPTR_MAX - magnitude) {
        return false;
    }
    *resultOut = nativeAddress + magnitude;
    return true;
}

/// Resolves a private symbol inside a dyld-loaded image.
void *RorkHookFindSymbol(const RorkHookMachHeader *header,
                         const char *symbolName) {
    if (header == NULL || symbolName == NULL) {
        return NULL;
    }

    const RorkHookSegmentCommand *linkedit = NULL;
    const struct symtab_command *symtab =
        RorkHookFindSymtab(
            header,
            RorkHookReadableMemoryLength(header),
            &linkedit);
    size_t symbolBytes = 0;
    if (symtab == NULL ||
        linkedit == NULL ||
        !RorkHookSymbolTableSize(symtab->nsyms, &symbolBytes)) {
        return NULL;
    }

    bool slideResolved = false;
    intptr_t slide = RorkHookImageSlide(header, &slideResolved);
    uintptr_t linkeditAddress = 0;
    uint64_t symbolRelativeOffset = 0;
    uint64_t stringRelativeOffset = 0;
    if (!slideResolved ||
        !RorkHookApplySlide(linkedit->vmaddr, slide, &linkeditAddress) ||
        !RorkHookFileRangeInSegment(
            symtab->symoff,
            symbolBytes,
            linkedit,
            &symbolRelativeOffset) ||
        !RorkHookFileRangeInSegment(
            symtab->stroff,
            symtab->strsize,
            linkedit,
            &stringRelativeOffset) ||
        symbolRelativeOffset > UINTPTR_MAX ||
        stringRelativeOffset > UINTPTR_MAX ||
        linkeditAddress > UINTPTR_MAX - (uintptr_t)symbolRelativeOffset ||
        linkeditAddress > UINTPTR_MAX - (uintptr_t)stringRelativeOffset) {
        return NULL;
    }

    const void *symbols =
        (const void *)(linkeditAddress +
                       (uintptr_t)symbolRelativeOffset);
    const char *stringTable =
        (const char *)(linkeditAddress +
                       (uintptr_t)stringRelativeOffset);
    if (symbolBytes == 0 ||
        !RorkHookMemoryIsReadable(symbols, symbolBytes) ||
        symtab->strsize == 0 ||
        !RorkHookMemoryIsReadable(stringTable, symtab->strsize)) {
        return NULL;
    }

    RorkHookNList entry;
    if (!RorkHookFindSymbolEntry(
            symbols,
            stringTable,
            symtab->strsize,
            symtab->nsyms,
            symbolName,
            &entry)) {
        return NULL;
    }

    uintptr_t symbolAddress = 0;
    if (!RorkHookApplySlide(entry.n_value, slide, &symbolAddress)) {
        return NULL;
    }
    return RorkHookSignPointerIfExecutable((void *)symbolAddress);
}

/// State used to translate one symbol VM address into a file offset.
typedef struct RorkHookFileAddressContext {
    uint64_t virtualAddress;
    uint64_t fileOffset;
    bool found;
} RorkHookFileAddressContext;

/// Visitor that maps a symbol VM address into its owning file-backed segment.
static bool RorkHookFindFileAddressLoadCommand(const struct load_command *command,
                                               uint32_t index,
                                               void *contextRaw) {
    (void)index;
    if (command == NULL || contextRaw == NULL) {
        return false;
    }
    if (command->cmd != RORK_HOOK_LC_SEGMENT ||
        command->cmdsize < sizeof(RorkHookSegmentCommand)) {
        return true;
    }

    const RorkHookSegmentCommand *segment =
        (const RorkHookSegmentCommand *)(const void *)command;
    RorkHookFileAddressContext *context =
        (RorkHookFileAddressContext *)contextRaw;
    if (context->virtualAddress < segment->vmaddr) {
        return true;
    }

    uint64_t segmentOffset = context->virtualAddress - segment->vmaddr;
    if (segmentOffset >= segment->vmsize ||
        segmentOffset >= segment->filesize ||
        UINT64_MAX - segment->fileoff < segmentOffset) {
        return true;
    }

    context->fileOffset = segment->fileoff + segmentOffset;
    context->found = true;
    return false;
}

/// Resolves a symbol in a file mapping whose byte length is explicitly known.
void *RorkHookFindSymbolInFileImageWithSize(const RorkHookMachHeader *header,
                                            size_t mappedSize,
                                            const char *symbolName) {
    if (header == NULL || symbolName == NULL ||
        mappedSize < sizeof(*header) ||
        !RorkHookMemoryIsReadable(header, mappedSize)) {
        return NULL;
    }

    const struct symtab_command *symtab =
        RorkHookFindSymtab(header, mappedSize, NULL);
    size_t symbolBytes = 0;
    if (symtab == NULL ||
        !RorkHookSymbolTableSize(symtab->nsyms, &symbolBytes)) {
        return NULL;
    }

    if (symbolBytes == 0 ||
        !RorkHookRangeIsValid(symtab->symoff, symbolBytes, mappedSize) ||
        symtab->strsize == 0 ||
        !RorkHookRangeIsValid(symtab->stroff, symtab->strsize, mappedSize)) {
        return NULL;
    }

    const uint8_t *bytes = (const uint8_t *)header;
    const void *symbols = bytes + symtab->symoff;
    const char *stringTable =
        (const char *)(bytes + symtab->stroff);
    RorkHookNList entry;
    if (!RorkHookFindSymbolEntry(
            symbols,
            stringTable,
            symtab->strsize,
            symtab->nsyms,
            symbolName,
            &entry)) {
        return NULL;
    }

    RorkHookFileAddressContext context = {
        .virtualAddress = entry.n_value,
        .fileOffset = 0,
        .found = false,
    };
    if (!RorkHookForEachLoadCommandWithSize(
            header,
            mappedSize,
            RorkHookFindFileAddressLoadCommand,
            &context) ||
        !context.found ||
        context.fileOffset > SIZE_MAX ||
        !RorkHookRangeIsValid((size_t)context.fileOffset, 1, mappedSize)) {
        return NULL;
    }

    return RorkHookSignPointerIfExecutable(
        (void *)(bytes + (size_t)context.fileOffset));
}

/// Resolves a symbol in the readable VM region containing a file mapping.
void *RorkHookFindSymbolInFileImage(const RorkHookMachHeader *header,
                                    const char *symbolName) {
    if (header == NULL || symbolName == NULL) {
        return NULL;
    }
    return RorkHookFindSymbolInFileImageWithSize(
        header,
        RorkHookReadableMemoryLength(header),
        symbolName);
}
