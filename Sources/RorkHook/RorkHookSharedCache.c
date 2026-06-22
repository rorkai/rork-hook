#include "RorkHookSharedCache.h"

#include "RorkHookDyldCache.h"
#include "RorkHookInternal.h"

#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <mach-o/nlist.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <unistd.h>

/// Non-owning view over a bounded cache byte range.
typedef struct RorkHookDataView {
    /// First readable byte in the borrowed range.
    const uint8_t *bytes;

    /// Number of readable bytes beginning at `bytes`.
    size_t size;
} RorkHookDataView;

/// Owned read-only mapping of one complete cache file.
typedef struct RorkHookFileMapping {
    /// Base address returned by `mmap`.
    void *bytes;

    /// Exact byte length supplied when the mapping is released.
    size_t size;
} RorkHookFileMapping;

/// Returns a bounded subrange without permitting conversion or addition wrap.
///
/// A zero-length range at `view.size` is valid, but a missing backing pointer is
/// never treated as storage even when both the offset and length are zero.
static const uint8_t *RorkHookDataAt(RorkHookDataView view,
                                     uint64_t offset,
                                     size_t length) {
    if (view.bytes == NULL || offset > SIZE_MAX) {
        return NULL;
    }
    size_t nativeOffset = (size_t)offset;
    if (nativeOffset > view.size || length > view.size - nativeOffset) {
        return NULL;
    }
    return view.bytes + nativeOffset;
}

/// Calculates `base + index * stride` without allowing integer overflow.
///
/// `offsetOut` is left unchanged when either arithmetic operation fails.
static bool RorkHookIndexedOffset(uint64_t base,
                                  uint64_t index,
                                  size_t stride,
                                  uint64_t *offsetOut) {
    if (offsetOut == NULL) {
        return false;
    }

    uint64_t elementOffset = 0;
    uint64_t result = 0;
    if (__builtin_mul_overflow(
            index,
            (uint64_t)stride,
            &elementOffset) ||
        __builtin_add_overflow(base, elementOffset, &result)) {
        return false;
    }
    *offsetOut = result;
    return true;
}

/// Copies one fixed-size record from a bounded, potentially unaligned range.
static bool RorkHookReadRecord(RorkHookDataView view,
                               uint64_t offset,
                               void *record,
                               size_t recordSize) {
    if (record == NULL || recordSize == 0) {
        return false;
    }

    const uint8_t *bytes = RorkHookDataAt(view, offset, recordSize);
    if (bytes == NULL) {
        return false;
    }
    memcpy(record, bytes, recordSize);
    return true;
}

/// Compares a cache string without reading beyond its declared byte range.
static bool RorkHookStringEquals(RorkHookDataView view,
                                 uint64_t offset,
                                 const char *expected) {
    if (view.bytes == NULL ||
        expected == NULL ||
        offset > SIZE_MAX ||
        (size_t)offset >= view.size) {
        return false;
    }

    const char *candidate = (const char *)view.bytes + (size_t)offset;
    size_t remaining = view.size - (size_t)offset;
    const char *terminator = memchr(candidate, '\0', remaining);
    if (terminator == NULL) {
        return false;
    }
    size_t candidateLength = (size_t)(terminator - candidate);
    size_t expectedLength = strlen(expected);
    return candidateLength == expectedLength &&
        memcmp(candidate, expected, expectedLength) == 0;
}

/// Maps a nonempty regular file read-only and closes its descriptor immediately.
///
/// `mappingOut` is reset before the open attempt and remains empty on every
/// failure path. `O_NONBLOCK` prevents an unexpected FIFO path from blocking
/// before `fstat` can reject it.
static bool RorkHookMapFile(const char *path, RorkHookFileMapping *mappingOut) {
    if (mappingOut == NULL) {
        return false;
    }
    *mappingOut = (RorkHookFileMapping){0};
    if (path == NULL) {
        return false;
    }
    int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0) {
        return false;
    }

    struct stat status;
    bool valid = fstat(descriptor, &status) == 0 &&
        S_ISREG(status.st_mode) &&
        status.st_size > 0 &&
        (uint64_t)status.st_size <= SIZE_MAX;
    if (!valid) {
        close(descriptor);
        return false;
    }

    size_t size = (size_t)status.st_size;
    void *bytes = mmap(NULL, size, PROT_READ, MAP_PRIVATE, descriptor, 0);
    close(descriptor);
    if (bytes == MAP_FAILED) {
        return false;
    }

    *mappingOut = (RorkHookFileMapping){
        .bytes = bytes,
        .size = size,
    };
    return true;
}

/// Releases a read-only file mapping; an empty mapping is a no-op.
static void RorkHookUnmapFile(RorkHookFileMapping mapping) {
    if (mapping.bytes != NULL && mapping.size != 0) {
        (void)munmap(mapping.bytes, mapping.size);
    }
}

/// Locates and permanently caches the readable shared-cache path for this process.
const char *RorkHookLocateSharedCache(void) {
    static const char cryptexCacheDirectory[] =
        "/private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld";
    static const char cryptexCacheBasePath[] =
        "/private/preboot/Cryptexes/OS/System/Library/Caches/"
        "com.apple.dyld/dyld_shared_cache";
    static const char systemCacheDirectory[] =
        "/System/Library/Caches/com.apple.dyld";
    static const char systemCacheBasePath[] =
        "/System/Library/Caches/com.apple.dyld/dyld_shared_cache";
    static char cachePath[PATH_MAX];
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        const char *sharedRegion = getenv("DYLD_SHARED_REGION");
        const char *sharedCacheDir = getenv("DYLD_SHARED_CACHE_DIR");
        if (sharedRegion != NULL &&
            sharedCacheDir != NULL &&
            strcmp(sharedRegion, "private") == 0) {
            int written = snprintf(
                cachePath,
                sizeof(cachePath),
                "%s/dyld_shared_cache",
                sharedCacheDir);
            if (written < 0 || (size_t)written >= sizeof(cachePath)) {
                cachePath[0] = '\0';
                return;
            }
        } else if (access(cryptexCacheDirectory, R_OK | X_OK) == 0) {
            strlcpy(
                cachePath,
                cryptexCacheBasePath,
                sizeof(cachePath));
        } else if (access(systemCacheDirectory, R_OK | X_OK) == 0) {
            strlcpy(
                cachePath,
                systemCacheBasePath,
                sizeof(cachePath));
        }

        static const char *const suffixes[] = {
            "_arm64e",
            "_arm64",
            "_armv7s",
            "_armv7",
        };
        size_t baseLength = strlen(cachePath);
        if (baseLength != 0) {
            for (size_t index = 0;
                 index < sizeof(suffixes) / sizeof(*suffixes);
                 index += 1) {
                cachePath[baseLength] = '\0';
                if (strlcat(cachePath, suffixes[index], sizeof(cachePath)) <
                        sizeof(cachePath) &&
                    access(cachePath, R_OK) == 0) {
                    return;
                }
            }
        }
        cachePath[0] = '\0';
    });
    return cachePath;
}

/// Resolves one local symbol from independently bounded cache byte ranges.
///
/// The main view supplies image paths and ordering. The symbols view may be the
/// same mapping for a legacy inline cache or a separate `.symbols` sidecar.
/// Every table, record, string, index, and final address is validated before it
/// is read or returned.
void *RorkHookFindSharedCacheSymbolInData(const void *mainData,
                                          size_t mainSize,
                                          const void *symbolsData,
                                          size_t symbolsSize,
                                          uintptr_t slide,
                                          const char *imagePath,
                                          const char *symbolName) {
    if (mainData == NULL ||
        symbolsData == NULL ||
        imagePath == NULL ||
        symbolName == NULL) {
        return NULL;
    }

    RorkHookDataView mainView = {
        .bytes = mainData,
        .size = mainSize,
    };
    RorkHookDataView symbolsView = {
        .bytes = symbolsData,
        .size = symbolsSize,
    };

    RorkHookDyldCacheHeader mainHeader;
    RorkHookDyldCacheHeader symbolsHeader;
    if (!RorkHookReadRecord(mainView, 0, &mainHeader, sizeof(mainHeader)) ||
        !RorkHookReadRecord(symbolsView, 0, &symbolsHeader, sizeof(symbolsHeader)) ||
        memcmp(mainHeader.magic, "dyld_v1", 7) != 0 ||
        memcmp(symbolsHeader.magic, "dyld_v1", 7) != 0) {
        return NULL;
    }

    uint64_t imagesOffset = mainHeader.imagesOffset;
    uint64_t imagesCount = mainHeader.imagesCount;
    if (imagesOffset == 0 || imagesCount == 0) {
        imagesOffset = mainHeader.imagesOffsetOld;
        imagesCount = mainHeader.imagesCountOld;
    }
    if (imagesOffset == 0 || imagesCount == 0) {
        return NULL;
    }

    uint64_t imageTableSize = 0;
    if (__builtin_mul_overflow(
            imagesCount,
            (uint64_t)sizeof(RorkHookDyldCacheImageInfo),
            &imageTableSize) ||
        imageTableSize > SIZE_MAX ||
        RorkHookDataAt(mainView, imagesOffset, (size_t)imageTableSize) == NULL) {
        return NULL;
    }

    uint64_t imageIndex = UINT64_MAX;
    for (uint64_t index = 0; index < imagesCount; index += 1) {
        uint64_t infoOffset = 0;
        RorkHookDyldCacheImageInfo imageInfo;
        if (!RorkHookIndexedOffset(
                imagesOffset,
                index,
                sizeof(imageInfo),
                &infoOffset) ||
            !RorkHookReadRecord(
                mainView,
                infoOffset,
                &imageInfo,
                sizeof(imageInfo))) {
            return NULL;
        }
        if (RorkHookStringEquals(
                mainView,
                imageInfo.pathFileOffset,
                imagePath)) {
            imageIndex = index;
            break;
        }
    }
    if (imageIndex == UINT64_MAX ||
        symbolsHeader.localSymbolsOffset == 0 ||
        symbolsHeader.localSymbolsOffset > SIZE_MAX) {
        return NULL;
    }

    size_t localBase = (size_t)symbolsHeader.localSymbolsOffset;
    if (localBase > symbolsView.size ||
        symbolsHeader.localSymbolsSize > SIZE_MAX) {
        return NULL;
    }
    size_t localSize = symbolsHeader.localSymbolsSize == 0
        ? symbolsView.size - localBase
        : (size_t)symbolsHeader.localSymbolsSize;
    if (localSize > symbolsView.size - localBase) {
        return NULL;
    }
    RorkHookDataView localView = {
        .bytes = symbolsView.bytes + localBase,
        .size = localSize,
    };

    RorkHookDyldCacheLocalSymbolsInfo symbolsInfo;
    if (!RorkHookReadRecord(
            localView,
            0,
            &symbolsInfo,
            sizeof(symbolsInfo)) ||
        imageIndex >= symbolsInfo.entriesCount) {
        return NULL;
    }

    uint32_t nlistStartIndex = 0;
    uint32_t nlistCount = 0;
    bool uses64BitEntry =
        mainHeader.mappingOffset >=
        offsetof(RorkHookDyldCacheHeader, symbolFileUUID);
    // Caches whose mapping table begins after `symbolFileUUID` use the modern
    // per-image record containing a 64-bit dylib offset. Older headers end
    // before that format boundary and use the compact 32-bit record.
    uint64_t entryOffset = 0;
    if (uses64BitEntry) {
        RorkHookDyldCacheLocalSymbolsEntry64 entry;
        if (!RorkHookIndexedOffset(
                symbolsInfo.entriesOffset,
                imageIndex,
                sizeof(entry),
                &entryOffset) ||
            !RorkHookReadRecord(
                localView,
                entryOffset,
                &entry,
                sizeof(entry))) {
            return NULL;
        }
        nlistStartIndex = entry.nlistStartIndex;
        nlistCount = entry.nlistCount;
    } else {
        RorkHookDyldCacheLocalSymbolsEntry32 entry;
        if (!RorkHookIndexedOffset(
                symbolsInfo.entriesOffset,
                imageIndex,
                sizeof(entry),
                &entryOffset) ||
            !RorkHookReadRecord(
                localView,
                entryOffset,
                &entry,
                sizeof(entry))) {
            return NULL;
        }
        nlistStartIndex = entry.nlistStartIndex;
        nlistCount = entry.nlistCount;
    }

    uint64_t nlistEnd = 0;
    if (__builtin_add_overflow(
            (uint64_t)nlistStartIndex,
            (uint64_t)nlistCount,
            &nlistEnd) ||
        nlistEnd > symbolsInfo.nlistCount) {
        return NULL;
    }

    uint64_t nlistTableSize = 0;
    if (__builtin_mul_overflow(
            (uint64_t)symbolsInfo.nlistCount,
            (uint64_t)sizeof(struct nlist_64),
            &nlistTableSize) ||
        nlistTableSize > SIZE_MAX ||
        RorkHookDataAt(
            localView,
            symbolsInfo.nlistOffset,
            (size_t)nlistTableSize) == NULL ||
        RorkHookDataAt(
            localView,
            symbolsInfo.stringsOffset,
            symbolsInfo.stringsSize) == NULL) {
        return NULL;
    }

    RorkHookDataView stringsView = {
        .bytes = localView.bytes + symbolsInfo.stringsOffset,
        .size = symbolsInfo.stringsSize,
    };
    for (uint64_t index = nlistStartIndex; index < nlistEnd; index += 1) {
        uint64_t symbolOffset = 0;
        struct nlist_64 symbol;
        if (!RorkHookIndexedOffset(
                symbolsInfo.nlistOffset,
                index,
                sizeof(symbol),
                &symbolOffset) ||
            !RorkHookReadRecord(
                localView,
                symbolOffset,
                &symbol,
                sizeof(symbol)) ||
            symbol.n_un.n_strx == 0 ||
            symbol.n_un.n_strx >= stringsView.size ||
            (symbol.n_type & N_TYPE) != N_SECT ||
            !RorkHookStringEquals(
                stringsView,
                symbol.n_un.n_strx,
                symbolName)) {
            continue;
        }

        if (symbol.n_value > UINTPTR_MAX - slide) {
            return NULL;
        }
        return RorkHookSignPointerIfExecutable(
            (void *)(slide + (uintptr_t)symbol.n_value));
    }
    return NULL;
}

/// Maps an explicitly selected cache and resolves one local symbol.
///
/// A readable sibling `.symbols` file takes precedence over inline metadata.
/// Both mappings are released before the computed run-time address is returned.
void *RorkHookFindSharedCacheSymbolAtPath(const char *mainPath,
                                          uintptr_t slide,
                                          const char *imagePath,
                                          const char *symbolName) {
    if (mainPath == NULL || imagePath == NULL || symbolName == NULL) {
        return NULL;
    }

    RorkHookFileMapping mainMapping;
    if (!RorkHookMapFile(mainPath, &mainMapping)) {
        return NULL;
    }

    char symbolsPath[PATH_MAX];
    int written = snprintf(
        symbolsPath,
        sizeof(symbolsPath),
        "%s.symbols",
        mainPath);
    RorkHookFileMapping symbolsMapping;
    bool hasSidecar =
        written >= 0 &&
        (size_t)written < sizeof(symbolsPath) &&
        RorkHookMapFile(symbolsPath, &symbolsMapping);
    if (!hasSidecar) {
        symbolsMapping = mainMapping;
    }

    void *resolved = RorkHookFindSharedCacheSymbolInData(
        mainMapping.bytes,
        mainMapping.size,
        symbolsMapping.bytes,
        symbolsMapping.size,
        slide,
        imagePath,
        symbolName);

    if (hasSidecar) {
        RorkHookUnmapFile(symbolsMapping);
    }
    RorkHookUnmapFile(mainMapping);
    return resolved;
}

/// Resolves one private local symbol from the active shared cache.
void *RorkHookFindSharedCacheSymbol(const char *imagePath,
                                    const char *symbolName) {
    if (imagePath == NULL || symbolName == NULL) {
        return NULL;
    }

    const char *mainPath = RorkHookLocateSharedCache();
    if (mainPath[0] == '\0') {
        return NULL;
    }
    return RorkHookFindSharedCacheSymbolAtPath(
        mainPath,
        RorkHookSharedCacheSlide(),
        imagePath,
        symbolName);
}
