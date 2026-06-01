#ifndef RORK_HOOK_DYLD_CACHE_H
#define RORK_HOOK_DYLD_CACHE_H

#include <stdint.h>

/// Minimal, owned subset of the dyld shared cache on-disk layout.
///
/// These records mirror the public `dyld_cache_format.h` from Apple's dyld
/// source, trimmed to the fields RorkHook reads when resolving local symbols.
/// Defining them here keeps the package self-contained instead of vendoring the
/// full Apple header. Only fields up to `imagesCount` are referenced; the cache
/// header is far larger on disk, so always read it with its real on-disk size
/// rather than `sizeof` of this struct when seeking past it.

typedef struct {
    char     magic[16];
    uint32_t mappingOffset;
    uint32_t mappingCount;
    uint32_t imagesOffsetOld;       // pre-iOS 15 image table offset
    uint32_t imagesCountOld;        // pre-iOS 15 image table count
    uint64_t dyldBaseAddress;
    uint64_t codeSignatureOffset;
    uint64_t codeSignatureSize;
    uint64_t slideInfoOffsetUnused;
    uint64_t slideInfoSizeUnused;
    uint64_t localSymbolsOffset;    // file offset of the local-symbols region
    uint64_t localSymbolsSize;
    uint8_t  uuid[16];
    uint64_t cacheType;
    uint32_t branchPoolsOffset;
    uint32_t branchPoolsCount;
    uint64_t dyldInCacheMH;
    uint64_t dyldInCacheEntry;
    uint64_t imagesTextOffset;
    uint64_t imagesTextCount;
    uint64_t patchInfoAddr;
    uint64_t patchInfoSize;
    uint64_t otherImageGroupAddrUnused;
    uint64_t otherImageGroupSizeUnused;
    uint64_t progClosuresAddr;
    uint64_t progClosuresSize;
    uint64_t progClosuresTrieAddr;
    uint64_t progClosuresTrieSize;
    uint32_t platform;
    uint32_t formatAndFlags;
    uint64_t sharedRegionStart;
    uint64_t sharedRegionSize;
    uint64_t maxSlide;
    uint64_t dylibsImageArrayAddr;
    uint64_t dylibsImageArraySize;
    uint64_t dylibsTrieAddr;
    uint64_t dylibsTrieSize;
    uint64_t otherImageArrayAddr;
    uint64_t otherImageArraySize;
    uint64_t otherTrieAddr;
    uint64_t otherTrieSize;
    uint32_t mappingWithSlideOffset;
    uint32_t mappingWithSlideCount;
    uint64_t dylibsPBLStateArrayAddrUnused;
    uint64_t dylibsPBLSetAddr;
    uint64_t programsPBLSetPoolAddr;
    uint64_t programsPBLSetPoolSize;
    uint64_t programTrieAddr;
    uint32_t programTrieSize;
    uint32_t osVersion;
    uint32_t altPlatform;
    uint32_t altOsVersion;
    uint64_t swiftOptsOffset;
    uint64_t swiftOptsSize;
    uint32_t subCacheArrayOffset;
    uint32_t subCacheArrayCount;
    uint8_t  symbolFileUUID[16];    // boundary marker: split-symbols caches add fields from here
    uint64_t rosettaReadOnlyAddr;
    uint64_t rosettaReadOnlySize;
    uint64_t rosettaReadWriteAddr;
    uint64_t rosettaReadWriteSize;
    uint32_t imagesOffset;          // iOS 15+ image table offset
    uint32_t imagesCount;           // iOS 15+ image table count
} RorkHookDyldCacheHeader;

typedef struct {
    uint64_t address;
    uint64_t modTime;
    uint64_t inode;
    uint32_t pathFileOffset;
    uint32_t pad;
} RorkHookDyldCacheImageInfo;

typedef struct {
    uint32_t nlistOffset;       // offset (from localSymbolsOffset) of the nlist array
    uint32_t nlistCount;
    uint32_t stringsOffset;     // offset (from localSymbolsOffset) of the string pool
    uint32_t stringsSize;
    uint32_t entriesOffset;     // offset (from localSymbolsOffset) of the per-image entries
    uint32_t entriesCount;
} RorkHookDyldCacheLocalSymbolsInfo;

typedef struct {
    uint32_t dylibOffset;
    uint32_t nlistStartIndex;
    uint32_t nlistCount;
} RorkHookDyldCacheLocalSymbolsEntry32;

typedef struct {
    uint64_t dylibOffset;
    uint32_t nlistStartIndex;
    uint32_t nlistCount;
} RorkHookDyldCacheLocalSymbolsEntry64;

#endif /* RORK_HOOK_DYLD_CACHE_H */
