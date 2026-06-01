#ifndef RORK_HOOK_TEST_SUPPORT_H
#define RORK_HOOK_TEST_SUPPORT_H

#include <stdbool.h>

#include "RorkHookTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Returns the Mach-O header for the test-support image.
const RorkHookMachHeader *RorkHookTestSupportImageHeader(void);

/// Returns a direct function pointer to ``RorkHookTestSupportSymbolAnchor``.
void *RorkHookTestSupportSymbolAnchorPointer(void);

/// Returns `true` when `pointer` resolves to ``RorkHookTestSupportSymbolAnchor``,
/// ignoring pointer-authentication decoration when present.
bool RorkHookTestSupportPointerMatchesSymbolAnchor(void *pointer);

/// Exported symbol used to prove loaded-image symbol lookup.
int RorkHookTestSupportSymbolAnchor(void);

/// Calls the imported `strcmp` function from the test-support image.
int RorkHookTestSupportCallImportedStrcmp(const char *lhs, const char *rhs);

/// Returns the resolved imported `strcmp` function pointer.
void *RorkHookTestSupportImportedStrcmpPointer(void);

/// Returns a replacement function pointer with the same ABI as `strcmp`.
void *RorkHookTestSupportReplacementStrcmpPointer(void);

/// Returns the fixed value emitted by the replacement `strcmp` implementation.
int RorkHookTestSupportReplacementStrcmpResult(void);

/// Returns `true` when C ABI guard paths reject `NULL` arguments.
bool RorkHookTestSupportNullArgumentGuardsPass(void);

#ifdef __cplusplus
}
#endif

#endif /* RORK_HOOK_TEST_SUPPORT_H */
