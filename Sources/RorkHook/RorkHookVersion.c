#include "RorkHookVersion.h"

/// Returns the semantic package version compiled into this target.
const char *RorkHookVersion(void) {
    return RORK_HOOK_VERSION_STRING;
}

/// Returns the ABI version compiled into this target.
uint32_t RorkHookABIVersion(void) {
    return RORK_HOOK_ABI_VERSION;
}
