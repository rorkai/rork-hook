#include "RorkHookTestSupport.h"

#include "RorkHook.h"

#include <dlfcn.h>
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
           RorkHookFindSharedCacheSymbol(nullString, "anything") == NULL &&
           RorkHookBuildAbsoluteJump(nullConstPointer, instructions, RORK_HOOK_ABSOLUTE_JUMP_WORDS) == 0 &&
           RorkHookReplaceFunction(nullPointer, nullPointer) != KERN_SUCCESS;
}
