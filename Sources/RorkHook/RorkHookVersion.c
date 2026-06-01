#include "RorkHookVersion.h"

const char *RorkHookVersion(void) {
    return RORK_HOOK_VERSION_STRING;
}

uint32_t RorkHookABIVersion(void) {
    return RORK_HOOK_ABI_VERSION;
}
