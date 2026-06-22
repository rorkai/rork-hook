#ifndef RORK_HOOK_DYLD_CACHE_H
#define RORK_HOOK_DYLD_CACHE_H

#include <stddef.h>
#include <stdint.h>

/// On-disk dyld shared-cache records consumed by the local-symbol parser.
///
/// The cache header is represented through the newest field RorkHook reads so
/// every retained member preserves a documented byte offset in older and newer
/// cache generations. The parser copies records with `memcpy`; it never relies
/// on a mapped file being naturally aligned for these C types.

typedef struct RorkHookDyldCacheHeader {
    /// Format and architecture identifier, such as `dyld_v1  arm64e`.
    char magic[16];

    /// File offset and count of the mapping table.
    uint32_t mappingOffset;
    uint32_t mappingCount;

    /// Legacy image-table location used by pre-iOS 15 cache layouts.
    uint32_t imagesOffsetOld;
    uint32_t imagesCountOld;

    /// Cache metadata retained to preserve the on-disk header layout.
    uint64_t dyldBaseAddress;
    uint64_t codeSignatureOffset;
    uint64_t codeSignatureSize;
    uint64_t slideInfoOffsetUnused;
    uint64_t slideInfoSizeUnused;

    /// File range containing local-symbol metadata in this cache or sidecar.
    uint64_t localSymbolsOffset;
    uint64_t localSymbolsSize;

    /// Cache identity and format metadata preceding the modern image table.
    uint8_t uuid[16];
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

    /// UUID of the local-symbol sidecar.
    ///
    /// Its offset is also the format boundary used to distinguish modern
    /// 64-bit per-image local-symbol entries from the legacy 32-bit entries.
    uint8_t symbolFileUUID[16];

    /// Modern fields retained between the sidecar UUID and image table.
    uint64_t rosettaReadOnlyAddr;
    uint64_t rosettaReadOnlySize;
    uint64_t rosettaReadWriteAddr;
    uint64_t rosettaReadWriteSize;

    /// Image-table location used by iOS 15 and newer cache layouts.
    uint32_t imagesOffset;
    uint32_t imagesCount;
} RorkHookDyldCacheHeader;

/// Identifies one image and its path string in the main cache.
typedef struct RorkHookDyldCacheImageInfo {
    /// Unslid load address of the image in the shared region.
    uint64_t address;

    /// Source-file metadata recorded when the cache was built.
    uint64_t modTime;
    uint64_t inode;

    /// File offset of the image's NUL-terminated install name.
    uint32_t pathFileOffset;

    /// Reserved bytes that keep each record eight-byte aligned.
    uint32_t pad;
} RorkHookDyldCacheImageInfo;

/// Describes the three tables inside a cache's local-symbol region.
///
/// Every offset is relative to `localSymbolsOffset` in the containing cache
/// header, not an absolute file offset.
typedef struct RorkHookDyldCacheLocalSymbolsInfo {
    /// Offset and entry count of the complete `nlist_64` table.
    uint32_t nlistOffset;
    uint32_t nlistCount;

    /// Offset and byte length of the NUL-terminated string pool.
    uint32_t stringsOffset;
    uint32_t stringsSize;

    /// Offset and count of per-image symbol-range entries.
    uint32_t entriesOffset;
    uint32_t entriesCount;
} RorkHookDyldCacheLocalSymbolsInfo;

/// Legacy per-image local-symbol range with a 32-bit dylib offset.
typedef struct RorkHookDyldCacheLocalSymbolsEntry32 {
    /// Offset identifying the image this record belongs to.
    uint32_t dylibOffset;

    /// Half-open range in the complete local `nlist_64` table.
    uint32_t nlistStartIndex;
    uint32_t nlistCount;
} RorkHookDyldCacheLocalSymbolsEntry32;

/// Modern per-image local-symbol range with a 64-bit dylib offset.
typedef struct RorkHookDyldCacheLocalSymbolsEntry64 {
    /// Offset identifying the image this record belongs to.
    uint64_t dylibOffset;

    /// Half-open range in the complete local `nlist_64` table.
    uint32_t nlistStartIndex;
    uint32_t nlistCount;
} RorkHookDyldCacheLocalSymbolsEntry64;

#if defined(__LP64__)
_Static_assert(
    offsetof(RorkHookDyldCacheHeader, mappingOffset) == 16,
    "dyld cache mappingOffset layout changed");
_Static_assert(
    offsetof(RorkHookDyldCacheHeader, localSymbolsOffset) == 72,
    "dyld cache localSymbolsOffset layout changed");
_Static_assert(
    offsetof(RorkHookDyldCacheHeader, mappingWithSlideOffset) == 312,
    "dyld cache mappingWithSlideOffset layout changed");
_Static_assert(
    offsetof(RorkHookDyldCacheHeader, symbolFileUUID) == 400,
    "dyld cache symbolFileUUID layout changed");
_Static_assert(
    offsetof(RorkHookDyldCacheHeader, imagesOffset) == 448,
    "dyld cache imagesOffset layout changed");
_Static_assert(
    sizeof(RorkHookDyldCacheHeader) == 456,
    "dyld cache header prefix layout changed");
_Static_assert(
    sizeof(RorkHookDyldCacheImageInfo) == 32,
    "dyld cache image record layout changed");
_Static_assert(
    sizeof(RorkHookDyldCacheLocalSymbolsInfo) == 24,
    "dyld cache local-symbol info layout changed");
_Static_assert(
    sizeof(RorkHookDyldCacheLocalSymbolsEntry32) == 12,
    "legacy dyld local-symbol entry layout changed");
_Static_assert(
    sizeof(RorkHookDyldCacheLocalSymbolsEntry64) == 16,
    "modern dyld local-symbol entry layout changed");
#endif

#endif /* RORK_HOOK_DYLD_CACHE_H */
