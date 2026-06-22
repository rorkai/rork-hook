#import <XCTest/XCTest.h>

#import <RorkHook/RorkHook.h>

#import <dlfcn.h>
#import <unistd.h>

#if __has_include(<os/security_config.h>)
#import <os/security_config.h>
#define RORK_HOOK_DEVICE_TESTS_HAVE_SECURITY_CONFIG 1
#else
#define RORK_HOOK_DEVICE_TESTS_HAVE_SECURITY_CONFIG 0
#endif

static const pid_t RorkHookReplacementProcessIdentifier = 0x5248;

/// Calls an imported function through the test bundle's authenticated import
/// slot so the rebinding test exercises a real arm64e fixup.
__attribute__((noinline))
static pid_t RorkHookCallImportedGetpid(void) {
    return getpid();
}

/// Replaces the imported process identifier with a recognizable value that
/// cannot be confused with a live process identifier.
__attribute__((noinline))
static pid_t RorkHookReplacementGetpid(void) {
    return RorkHookReplacementProcessIdentifier;
}

/// Returns the Mach-O image containing `address`, which lets the test rebind
/// only its own import table instead of modifying every loaded image.
static const RorkHookMachHeader *RorkHookImageContainingAddress(
    const void *address
) {
    Dl_info imageInfo;
    if (dladdr(address, &imageInfo) == 0) {
        return NULL;
    }
    return (const RorkHookMachHeader *)imageInfo.dli_fbase;
}

@interface RorkHookDeviceTests : XCTestCase
@end

@implementation RorkHookDeviceTests

- (void)testTPROCapabilityAndWriteWindow {
    bool supportsTPRO = RorkHookSupportsTPRO();

#if RORK_HOOK_DEVICE_TESTS_HAVE_SECURITY_CONFIG
    if (@available(iOS 26.0, *)) {
        os_security_config_t configuration = os_security_config_get();
        bool systemReportsTPRO =
            (configuration & OS_SECURITY_CONFIG_TPRO) != 0;
        XCTAssertEqual(supportsTPRO, systemReportsTPRO);
        NSLog(
            @"Process security configuration=0x%llx; TPRO=%@",
            (unsigned long long)configuration,
            supportsTPRO ? @"enabled" : @"disabled"
        );
    }
#endif

    XCTAssertFalse(RorkHookThreadCanWriteTPRO());
    RorkHookBeginThreadTPROWrite();
    bool writeWindowOpened = RorkHookThreadCanWriteTPRO();
    RorkHookEndThreadTPROWrite();

    XCTAssertEqual(writeWindowOpened, supportsTPRO);
    XCTAssertFalse(RorkHookThreadCanWriteTPRO());
}

- (void)testLiveSharedCacheLookup {
    const char *sharedCachePath = RorkHookLocateSharedCache();
    XCTAssertNotEqual(sharedCachePath, NULL);
    XCTAssertTrue(sharedCachePath[0] != '\0');
    XCTAssertEqual(access(sharedCachePath, R_OK), 0);

    void *versionMap = RorkHookFindSharedCacheSymbol(
        "/usr/lib/dyld",
        "__ZN5dyld3L11sVersionMapE"
    );
    XCTAssertNotEqual(versionMap, NULL);
}

- (void)testAuthenticatedImportedSymbolRebinding {
#if !defined(__arm64e__)
    XCTFail(@"The device harness must be compiled for arm64e.");
    return;
#endif

    pid_t originalProcessIdentifier = RorkHookCallImportedGetpid();
    XCTAssertGreaterThan(originalProcessIdentifier, 0);
    XCTAssertNotEqual(
        originalProcessIdentifier,
        RorkHookReplacementProcessIdentifier
    );

    const RorkHookMachHeader *testImage = RorkHookImageContainingAddress(
        (const void *)(uintptr_t)&RorkHookCallImportedGetpid
    );
    XCTAssertNotEqual(testImage, NULL);

    void *getpidAddress = dlsym(RTLD_DEFAULT, "getpid");
    XCTAssertNotEqual(getpidAddress, NULL);

    RorkHookRebindSymbolInImage(
        testImage,
        getpidAddress,
        (void *)(uintptr_t)&RorkHookReplacementGetpid
    );

    XCTAssertEqual(
        RorkHookCallImportedGetpid(),
        RorkHookReplacementProcessIdentifier
    );

    RorkHookRebindSymbolInImage(
        testImage,
        (void *)(uintptr_t)&RorkHookReplacementGetpid,
        getpidAddress
    );

    XCTAssertEqual(RorkHookCallImportedGetpid(), originalProcessIdentifier);
}

@end
