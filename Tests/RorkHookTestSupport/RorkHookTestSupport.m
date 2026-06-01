#include "RorkHookTestSupport.h"

#include "RorkHook.h"

#include <dlfcn.h>
#include <string.h>

#if __has_feature(ptrauth_calls)
#include <ptrauth.h>
#endif

static void *RorkHookTestStripFunctionPointer(void *pointer) {
#if __has_feature(ptrauth_calls)
    return ptrauth_strip(pointer, ptrauth_key_function_pointer);
#else
    return pointer;
#endif
}

static const RorkHookMachHeader *RorkHookTestImageHeaderForAddress(const void *address) {
    Dl_info info;
    if (dladdr(address, &info) == 0 || info.dli_fbase == NULL) {
        return NULL;
    }
    return (const RorkHookMachHeader *)info.dli_fbase;
}

const RorkHookMachHeader *RorkHookTestSupportImageHeader(void) {
    return RorkHookTestImageHeaderForAddress((const void *)RorkHookTestSupportImageHeader);
}

int RorkHookTestSupportSymbolAnchor(void) {
    return 37;
}

void *RorkHookTestSupportSymbolAnchorPointer(void) {
    return (void *)RorkHookTestSupportSymbolAnchor;
}

bool RorkHookTestSupportPointerMatchesSymbolAnchor(void *pointer) {
    return RorkHookTestStripFunctionPointer(pointer) ==
           RorkHookTestStripFunctionPointer((void *)RorkHookTestSupportSymbolAnchor);
}

int RorkHookTestSupportCallImportedStrcmp(const char *lhs, const char *rhs) {
    return strcmp(lhs, rhs);
}

void *RorkHookTestSupportImportedStrcmpPointer(void) {
    return (void *)strcmp;
}

static int RorkHookTestReplacementStrcmp(const char *lhs, const char *rhs) {
    (void)lhs;
    (void)rhs;
    return RorkHookTestSupportReplacementStrcmpResult();
}

void *RorkHookTestSupportReplacementStrcmpPointer(void) {
    return (void *)RorkHookTestReplacementStrcmp;
}

int RorkHookTestSupportReplacementStrcmpResult(void) {
    return 4242;
}

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
