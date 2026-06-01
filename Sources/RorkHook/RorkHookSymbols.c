#include "RorkHookSymbols.h"

#include "RorkHookInternal.h"

#include <mach-o/nlist.h>
#include <string.h>

typedef struct {
    const struct symtab_command *symtab;
    const RorkHookSegmentCommand *linkedit;
    const RorkHookSegmentCommand **linkeditOut;
} RorkHookFindSymtabContext;

static bool RorkHookFindSymtabLoadCommand(const struct load_command *command,
                                          uint32_t index,
                                          void *contextRaw) {
    (void)index;
    RorkHookFindSymtabContext *context = (RorkHookFindSymtabContext *)contextRaw;
    if (command->cmd == LC_SYMTAB && command->cmdsize >= sizeof(struct symtab_command)) {
        context->symtab = (const struct symtab_command *)command;
    } else if (command->cmd == RORK_HOOK_LC_SEGMENT &&
               command->cmdsize >= sizeof(RorkHookSegmentCommand)) {
        const RorkHookSegmentCommand *segment = (const RorkHookSegmentCommand *)command;
        if (strncmp(segment->segname, SEG_LINKEDIT, sizeof(segment->segname)) == 0) {
            context->linkedit = segment;
        }
    }
    return !(context->symtab != NULL &&
             (context->linkeditOut == NULL || context->linkedit != NULL));
}

/// Locates the `LC_SYMTAB` command and, optionally, the `__LINKEDIT` segment of
/// an image. Returns `NULL` when the image has no symbol table.
static const struct symtab_command *RorkHookFindSymtab(const RorkHookMachHeader *header,
                                                       const RorkHookSegmentCommand **linkeditOut) {
    RorkHookFindSymtabContext context = {
        .symtab = NULL,
        .linkedit = NULL,
        .linkeditOut = linkeditOut,
    };
    if (!RorkHookForEachLoadCommand(header, RorkHookFindSymtabLoadCommand, &context)) {
        if (linkeditOut != NULL) {
            *linkeditOut = NULL;
        }
        return NULL;
    }
    if (linkeditOut != NULL) {
        *linkeditOut = context.linkedit;
    }
    return context.symtab;
}

/// Scans a symbol table for `symbolName`, returning the matching address
/// relative to `base` (the image slide), signed if it lands in executable
/// memory. `base` is the image slide for both loaded and file images because
/// the slide is defined as `header - __TEXT.vmaddr` in both cases.
static void *RorkHookScanSymbols(const RorkHookNList *symbols,
                                 const char *stringTable,
                                 size_t stringTableSize,
                                 uint32_t symbolCount,
                                 const char *symbolName,
                                 intptr_t base) {
    size_t symbolNameLength = strlen(symbolName);
    for (uint32_t index = 0; index < symbolCount; index += 1) {
        const RorkHookNList *entry = &symbols[index];
        uint32_t stringOffset = entry->n_un.n_strx;
        if (stringOffset == 0 || stringOffset >= stringTableSize) {
            continue;
        }
        if ((entry->n_type & N_TYPE) != N_SECT) {
            continue;
        }
        const char *candidate = stringTable + stringOffset;
        size_t remaining = stringTableSize - stringOffset;
        const char *terminator = (const char *)memchr(candidate, '\0', remaining);
        if (terminator == NULL) {
            continue;
        }

        size_t candidateLength = (size_t)(terminator - candidate);
        if (candidateLength == 0 ||
            candidateLength != symbolNameLength ||
            memcmp(candidate, symbolName, candidateLength) != 0) {
            continue;
        }
        return RorkHookSignPointerIfExecutable((void *)((uintptr_t)base + (uintptr_t)entry->n_value));
    }
    return NULL;
}

void *RorkHookFindSymbol(const RorkHookMachHeader *header, const char *symbolName) {
    if (header == NULL || symbolName == NULL) {
        return NULL;
    }

    const RorkHookSegmentCommand *linkedit = NULL;
    const struct symtab_command *symtab = RorkHookFindSymtab(header, &linkedit);
    if (symtab == NULL || linkedit == NULL) {
        return NULL;
    }

    intptr_t slide = RorkHookImageSlide(header, NULL);
    // __LINKEDIT data lives at `slide + vmaddr`, while file offsets in the
    // symtab are anchored at the segment's `fileoff`; bridge the two so a file
    // offset maps to its run-time address.
    uintptr_t linkeditBase = (uintptr_t)((intptr_t)linkedit->vmaddr + slide) - (uintptr_t)linkedit->fileoff;
    const RorkHookNList *symbols = (const RorkHookNList *)(linkeditBase + symtab->symoff);
    const char *stringTable = (const char *)(linkeditBase + symtab->stroff);

    return RorkHookScanSymbols(symbols, stringTable, symtab->strsize, symtab->nsyms, symbolName, slide);
}

void *RorkHookFindSymbolInFileImage(const RorkHookMachHeader *header, const char *symbolName) {
    if (header == NULL || symbolName == NULL) {
        return NULL;
    }

    const struct symtab_command *symtab = RorkHookFindSymtab(header, NULL);
    if (symtab == NULL) {
        return NULL;
    }

    // The whole file is mapped contiguously from `header`, so symtab and string
    // table are reached by raw file offset rather than the __LINKEDIT slide.
    const RorkHookNList *symbols = (const RorkHookNList *)((uintptr_t)header + symtab->symoff);
    const char *stringTable = (const char *)((uintptr_t)header + symtab->stroff);
    intptr_t base = RorkHookImageSlide(header, NULL);

    return RorkHookScanSymbols(symbols, stringTable, symtab->strsize, symtab->nsyms, symbolName, base);
}
