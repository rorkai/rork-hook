#include "RorkHookSharedCache.h"

#include "RorkHookDyldCache.h"
#include "RorkHookInternal.h"

#include <dispatch/dispatch.h>
#include <mach-o/nlist.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslimits.h>
#include <unistd.h>

/// Reads a NUL-terminated string at `offset` into `buffer`, returning `true`
/// when a terminated string fit. Used to read image paths from the cache.
static bool RorkHookReadCStringAt(FILE *file, off_t offset, char *buffer, size_t capacity) {
    if (capacity == 0 || fseeko(file, offset, SEEK_SET) != 0) {
        return false;
    }
    for (size_t index = 0; index < capacity; index += 1) {
        int character = fgetc(file);
        if (character == EOF) {
            return false;
        }
        buffer[index] = (char)character;
        if (character == '\0') {
            return true;
        }
    }
    buffer[capacity - 1] = '\0';
    return false;
}

/// Locates and caches the active dyld shared-cache file path for this process.
const char *RorkHookLocateSharedCache(void) {
    static char cachePath[PATH_MAX];
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        const char *sharedRegion = getenv("DYLD_SHARED_REGION");
        const char *sharedCacheDir = getenv("DYLD_SHARED_CACHE_DIR");
        if (sharedRegion != NULL && sharedCacheDir != NULL && strcmp(sharedRegion, "private") == 0) {
            // A process running against a private cache (e.g. a custom rootless
            // install) advertises it through the environment.
            strlcpy(cachePath, sharedCacheDir, sizeof(cachePath));
            strlcat(cachePath, "/dyld_shared_cache", sizeof(cachePath));
        } else if (access("/private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld", F_OK) == 0) {
            // iOS 16 and later relocate the cache into the OS Cryptex.
            strlcpy(cachePath,
                    "/private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld/dyld_shared_cache",
                    sizeof(cachePath));
        } else if (access("/System/Library/Caches/com.apple.dyld", F_OK) == 0) {
            // iOS 15 and earlier.
            strlcpy(cachePath,
                    "/System/Library/Caches/com.apple.dyld/dyld_shared_cache",
                    sizeof(cachePath));
        }

        // Probe the architecture suffixes in preference order and keep the
        // first that exists. Truncate back to the base before each attempt.
        static const char *const suffixes[] = {"_arm64e", "_arm64", "_armv7s", "_armv7"};
        size_t baseLength = strlen(cachePath);
        if (baseLength != 0) {
            for (size_t index = 0; index < sizeof(suffixes) / sizeof(*suffixes); index += 1) {
                cachePath[baseLength] = '\0';
                strlcat(cachePath, suffixes[index], sizeof(cachePath));
                if (access(cachePath, F_OK) == 0) {
                    return;
                }
            }
        }
        cachePath[0] = '\0';
    });
    return cachePath;
}

/// Resolves a private local symbol from the dyld shared cache symbol metadata.
void *RorkHookFindSharedCacheSymbol(const char *imagePath, const char *symbolName) {
    if (imagePath == NULL || symbolName == NULL) {
        return NULL;
    }

    const char *mainPath = RorkHookLocateSharedCache();
    if (mainPath[0] == '\0') {
        return NULL;
    }

    char symbolsPath[PATH_MAX];
    strlcpy(symbolsPath, mainPath, sizeof(symbolsPath));
    strlcat(symbolsPath, ".symbols", sizeof(symbolsPath));

    void *resolved = NULL;
    FILE *mainFile = fopen(mainPath, "rb");
    if (mainFile == NULL) {
        return NULL;
    }
    // Recent caches keep local symbols in a sidecar `.symbols` file; older ones
    // inline them, so fall back to the main cache file when no sidecar exists.
    FILE *symbolsFile = fopen(symbolsPath, "rb");
    bool usingSidecar = symbolsFile != NULL;
    if (!usingSidecar) {
        symbolsFile = mainFile;
    }

    RorkHookDyldCacheHeader mainHeader;
    if (fread(&mainHeader, sizeof(mainHeader), 1, mainFile) != 1) {
        goto cleanup;
    }

    // Find the image's index in the cache image table.
    int imageIndex = -1;
    for (uint32_t index = 0; index < mainHeader.imagesCount; index += 1) {
        RorkHookDyldCacheImageInfo imageInfo;
        off_t infoOffset = (off_t)mainHeader.imagesOffset + (off_t)(sizeof(imageInfo) * index);
        if (fseeko(mainFile, infoOffset, SEEK_SET) != 0 ||
            fread(&imageInfo, sizeof(imageInfo), 1, mainFile) != 1) {
            goto cleanup;
        }

        char path[PATH_MAX];
        if (!RorkHookReadCStringAt(mainFile, (off_t)imageInfo.pathFileOffset, path, sizeof(path))) {
            continue;
        }
        if (strcmp(path, imagePath) == 0) {
            imageIndex = (int)index;
            break;
        }
    }
    if (imageIndex < 0) {
        goto cleanup;
    }

    // The local-symbols region is described relative to its own file.
    RorkHookDyldCacheHeader symbolsHeader;
    if (fseeko(symbolsFile, 0, SEEK_SET) != 0 ||
        fread(&symbolsHeader, sizeof(symbolsHeader), 1, symbolsFile) != 1 ||
        symbolsHeader.localSymbolsOffset == 0 ||
        symbolsHeader.localSymbolsOffset > (uint64_t)INT64_MAX) {
        goto cleanup;
    }
    off_t localBase = (off_t)symbolsHeader.localSymbolsOffset;

    RorkHookDyldCacheLocalSymbolsInfo symbolsInfo;
    if (fseeko(symbolsFile, localBase, SEEK_SET) != 0 ||
        fread(&symbolsInfo, sizeof(symbolsInfo), 1, symbolsFile) != 1 ||
        (uint32_t)imageIndex >= symbolsInfo.entriesCount) {
        goto cleanup;
    }

    // Caches large enough to carry split-symbol metadata use the 64-bit entry
    // layout; the boundary is whether the header reaches its `symbolFileUUID`.
    uint32_t nlistStartIndex = 0;
    uint32_t nlistCount = 0;
    off_t entriesBase = localBase + (off_t)symbolsInfo.entriesOffset;
    if (mainHeader.mappingOffset >= offsetof(RorkHookDyldCacheHeader, symbolFileUUID)) {
        RorkHookDyldCacheLocalSymbolsEntry64 entry;
        off_t entryOffset = entriesBase + (off_t)(sizeof(entry) * (uint32_t)imageIndex);
        if (fseeko(symbolsFile, entryOffset, SEEK_SET) != 0 ||
            fread(&entry, sizeof(entry), 1, symbolsFile) != 1) {
            goto cleanup;
        }
        nlistStartIndex = entry.nlistStartIndex;
        nlistCount = entry.nlistCount;
    } else {
        RorkHookDyldCacheLocalSymbolsEntry32 entry;
        off_t entryOffset = entriesBase + (off_t)(sizeof(entry) * (uint32_t)imageIndex);
        if (fseeko(symbolsFile, entryOffset, SEEK_SET) != 0 ||
            fread(&entry, sizeof(entry), 1, symbolsFile) != 1) {
            goto cleanup;
        }
        nlistStartIndex = entry.nlistStartIndex;
        nlistCount = entry.nlistCount;
    }

    if ((uint64_t)nlistStartIndex + (uint64_t)nlistCount > (uint64_t)symbolsInfo.nlistCount) {
        goto cleanup;
    }

    off_t nlistBase = localBase + (off_t)symbolsInfo.nlistOffset;
    off_t stringsBase = localBase + (off_t)symbolsInfo.stringsOffset;
    uintptr_t slide = RorkHookSharedCacheSlide();

    for (uint32_t index = nlistStartIndex; index < nlistStartIndex + nlistCount; index += 1) {
        struct nlist_64 symbol;
        off_t symbolOffset = nlistBase + (off_t)(sizeof(symbol) * index);
        if (fseeko(symbolsFile, symbolOffset, SEEK_SET) != 0 ||
            fread(&symbol, sizeof(symbol), 1, symbolsFile) != 1) {
            goto cleanup;
        }
        if (symbol.n_un.n_strx >= symbolsInfo.stringsSize) {
            continue;
        }

        char candidate[1024];
        if (!RorkHookReadCStringAt(symbolsFile,
                                   stringsBase + (off_t)symbol.n_un.n_strx,
                                   candidate,
                                   sizeof(candidate))) {
            continue;
        }
        if (strcmp(candidate, symbolName) == 0) {
            resolved = RorkHookSignPointerIfExecutable((void *)(slide + (uintptr_t)symbol.n_value));
            break;
        }
    }

cleanup:
    if (usingSidecar) {
        fclose(symbolsFile);
    }
    fclose(mainFile);
    return resolved;
}
