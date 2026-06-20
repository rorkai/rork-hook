#include "RorkHookMemory.h"

#include "RorkHookInternal.h"

#include <TargetConditionals.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/vm_page_size.h>
#include <string.h>

#if __has_include(<os/security_config.h>)
#include <os/security_config.h>
#define RORK_HOOK_HAS_PROCESS_SECURITY_CONFIG 1
#else
#define RORK_HOOK_HAS_PROCESS_SECURITY_CONFIG 0
#endif

/// Raw `mach_vm_protect` is only meaningful as a trap-bypassing syscall on real
/// arm64 iOS hardware. The simulator and macOS use the ordinary library stub,
/// and the TPRO comm-page state exists only on arm64e iOS devices.
#if defined(__arm64__) && TARGET_OS_IOS && !TARGET_OS_SIMULATOR
#define RORK_HOOK_DEVICE_ARM64 1
#else
#define RORK_HOOK_DEVICE_ARM64 0
#endif

#if RORK_HOOK_DEVICE_ARM64

/// Mach trap -14 is `_kernelrpc_mach_vm_protect_trap`. Calling it directly keeps
/// memory re-protection working even when the libsystem `vm_protect` stub has
/// itself been rebound by a hook. On arm64 `vm_address_t`/`vm_size_t` are 64-bit,
/// matching the trap's `mach_vm_address_t`/`mach_vm_size_t` argument widths.
extern kern_return_t RorkHookMachVMProtectTrap(mach_port_name_t target,
                                               vm_address_t address,
                                               vm_size_t size,
                                               boolean_t setMaximum,
                                               vm_prot_t newProtection);
__asm__(
    ".text\n"
    ".p2align 2\n"
    ".globl _RorkHookMachVMProtectTrap\n"
    "_RorkHookMachVMProtectTrap:\n"
    "    mov x16, #-0xe\n"
    "    svc #0x80\n"
    "    ret\n"
);

/// The TPRO write-enable state for the current thread lives in bit 0x24 of the
/// implementation-defined system register `s3_6_c15_c1_5`.
extern uint64_t RorkHookThreadTPROWriteEnabledRaw(void);
__asm__(
    ".text\n"
    ".p2align 2\n"
    ".globl _RorkHookThreadTPROWriteEnabledRaw\n"
    "_RorkHookThreadTPROWriteEnabledRaw:\n"
    "    mrs x0, s3_6_c15_c1_5\n"
    "    ubfx x0, x0, #0x24, #1\n"
    "    ret\n"
);

#define RORK_HOOK_COMM_PAGE_START 0x0000000FFFFFC000ULL
#define RORK_HOOK_COMM_PAGE_TPRO_WRITE_ENABLE \
    (RORK_HOOK_COMM_PAGE_START + 0x0d0)
#define RORK_HOOK_COMM_PAGE_TPRO_WRITE_DISABLE \
    (RORK_HOOK_COMM_PAGE_START + 0x0d8)

/// Reads a 64-bit value from the fixed comm-page slot used by arm64e TPRO.
static inline uint64_t RorkHookCommPageValue(uintptr_t address) {
    return *(const volatile uint64_t *)address;
}

/// Writes the implementation-defined register value that controls the calling
/// thread's TPRO state, then synchronizes subsequent instructions with it.
static inline void RorkHookWriteThreadTPROState(uint64_t state) {
    __asm__ __volatile__(
        "msr s3_6_c15_c1_5, %0\n"
        "isb sy\n"
        :: "r"(state)
        : "memory"
    );
}

/// Returns whether the comm page advertises the register values needed to
/// control TPRO on this hardware.
static bool RorkHookHardwareSupportsTPRO(void) {
    return
        RorkHookCommPageValue(RORK_HOOK_COMM_PAGE_TPRO_WRITE_ENABLE) != 0 &&
        RorkHookCommPageValue(RORK_HOOK_COMM_PAGE_TPRO_WRITE_DISABLE) != 0;
}

/// Detects process-level TPRO enforcement when no process configuration API is
/// available.
///
/// A thread whose write window was already open is left unchanged. Otherwise
/// the probe always writes the disable state after attempting to open the
/// window, even when readback does not confirm the transition.
static bool RorkHookProbeProcessTPRO(void) {
    if (RorkHookThreadTPROWriteEnabledRaw() != 0) {
        return true;
    }

    uint64_t enableState =
        RorkHookCommPageValue(RORK_HOOK_COMM_PAGE_TPRO_WRITE_ENABLE);
    RorkHookWriteThreadTPROState(enableState);
    bool writeWindowOpened = RorkHookThreadTPROWriteEnabledRaw() != 0;
    uint64_t disableState =
        RorkHookCommPageValue(RORK_HOOK_COMM_PAGE_TPRO_WRITE_DISABLE);
    RorkHookWriteThreadTPROState(disableState);
    return writeWindowOpened;
}

#endif /* RORK_HOOK_DEVICE_ARM64 */

/// Applies VM protection to a range, using the direct trap path on arm64 iOS.
kern_return_t RorkHookProtectMemory(vm_address_t address,
                                    vm_size_t size,
                                    vm_prot_t protection) {
    if (address == 0 || size == 0) {
        return KERN_INVALID_ARGUMENT;
    }

#if RORK_HOOK_DEVICE_ARM64
    return RorkHookMachVMProtectTrap(
        mach_task_self(),
        address,
        size,
        false,
        protection);
#else
    return vm_protect(
        mach_task_self(),
        address,
        size,
        false,
        protection);
#endif
}

/// Marks a range writable with copy-on-write semantics.
kern_return_t RorkHookMakeMemoryWritable(vm_address_t address, vm_size_t size) {
    return RorkHookProtectMemory(
        address,
        size,
        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
}

/// Marks a range readable and executable after code patching.
kern_return_t RorkHookMakeMemoryExecutable(vm_address_t address, vm_size_t size) {
    return RorkHookProtectMemory(address, size, VM_PROT_READ | VM_PROT_EXECUTE);
}

/// Opens a verified TPRO write window for one protected pointer transaction.
///
/// If this call changes the thread state, `restoreReadOnly` receives the
/// matching close operation. An already-open caller-owned window is preserved,
/// and a failed open attempt is explicitly closed before returning.
static bool RorkHookAllowTPROWrites(void (**restoreReadOnly)(void)) {
    if (restoreReadOnly == NULL) {
        return false;
    }
    *restoreReadOnly = NULL;

    if (!RorkHookSupportsTPRO()) {
        return false;
    }

    if (RorkHookThreadCanWriteTPRO()) {
        return true;
    }

    RorkHookBeginThreadTPROWrite();
    if (!RorkHookThreadCanWriteTPRO()) {
        RorkHookEndThreadTPROWrite();
        return false;
    }
    *restoreReadOnly = RorkHookEndThreadTPROWrite;
    return true;
}

/// Decides whether either the normal VM path or a verified TPRO window makes
/// the pending pointer store safe to execute.
bool RorkHookProtectedPointerWriteIsAvailable(kern_return_t protectionResult,
                                              bool supportsTPRO,
                                              bool tproWindowOpened) {
    if (supportsTPRO) {
        return tproWindowOpened;
    }
    return protectionResult == KERN_SUCCESS;
}

/// Combines hardware capability with the strongest available evidence that the
/// current process actually enforces TPRO.
bool RorkHookProcessUsesTPRO(bool hardwareSupportsTPRO,
                             bool processConfigurationKnown,
                             bool processConfigurationEnablesTPRO,
                             bool writeWindowProbeSucceeded) {
    if (!hardwareSupportsTPRO) {
        return false;
    }
    if (processConfigurationKnown) {
        return processConfigurationEnablesTPRO;
    }
    return writeWindowProbeSucceeded;
}

/// Restores the original protection of every page touched by a pointer slot.
///
/// Both pages are attempted even if the first restoration fails. This keeps a
/// cross-page slot from leaving the second page writable because of an
/// unrelated failure on the first page.
static bool RorkHookRestorePointerPageProtections(
    vm_address_t firstPage,
    vm_address_t lastPage,
    vm_size_t pageSize,
    bool spansPages,
    vm_prot_t firstProtection,
    vm_prot_t lastProtection) {
    kern_return_t firstResult = RorkHookProtectMemory(
        firstPage,
        pageSize,
        firstProtection);
    kern_return_t lastResult = KERN_SUCCESS;
    if (spansPages) {
        lastResult = RorkHookProtectMemory(
            lastPage,
            pageSize,
            lastProtection);
    }
    return firstResult == KERN_SUCCESS && lastResult == KERN_SUCCESS;
}

/// Performs one protected pointer store as a balanced memory transaction.
bool RorkHookStoreProtectedPointer(void *slot,
                                   const void *value,
                                   vm_prot_t protection) {
    if (slot == NULL || !(protection & VM_PROT_WRITE)) {
        return false;
    }

    vm_size_t pageSize = vm_page_size;
    if (pageSize == 0 || (pageSize & (pageSize - 1)) != 0) {
        return false;
    }

    const size_t slotSize = sizeof(void *);
    uintptr_t slotStart = (uintptr_t)slot;
    uintptr_t slotEnd = slotStart + slotSize - 1;
    if (slotEnd < slotStart) {
        return false;
    }

    uintptr_t pageMask = (uintptr_t)pageSize - 1;
    vm_address_t firstPage = (vm_address_t)(slotStart & ~pageMask);
    vm_address_t lastPage = (vm_address_t)(slotEnd & ~pageMask);
    bool spansPages = firstPage != lastPage;
    vm_size_t protectSize = (vm_size_t)((lastPage - firstPage) + pageSize);
    size_t firstPageBytes = spansPages
        ? (size_t)(lastPage - slotStart)
        : slotSize;
    size_t lastPageBytes = slotSize - firstPageBytes;
    if (!RorkHookMemoryIsReadable(slot, firstPageBytes) ||
        (spansPages &&
         !RorkHookMemoryIsReadable((const void *)lastPage, lastPageBytes))) {
        return false;
    }

    vm_prot_t firstOriginalProtection = 0;
    vm_prot_t lastOriginalProtection = 0;
    if (!RorkHookMemoryProtection((const void *)firstPage, &firstOriginalProtection) ||
        (spansPages &&
         !RorkHookMemoryProtection((const void *)lastPage, &lastOriginalProtection))) {
        return false;
    }
    if (!spansPages) {
        lastOriginalProtection = firstOriginalProtection;
    }

    kern_return_t result = RorkHookProtectMemory(
        firstPage,
        protectSize,
        protection);

    // On arm64e, __DATA_CONST/__AUTH_CONST in the shared cache is TPRO-hardened:
    // vm_protect can report success while the hardware still faults the store
    // unless the calling thread's TPRO write window is open. Open it around the
    // write whenever the process uses TPRO, regardless of the vm_protect result;
    // it is a no-op on pages that are not TPRO-gated. Without process-level TPRO
    // enforcement and with a failed vm_protect, the page cannot be made writable.
    bool supportsTPRO = RorkHookSupportsTPRO();
    void (*restoreReadOnly)(void) = NULL;
    bool tproWindowOpened =
        supportsTPRO && RorkHookAllowTPROWrites(&restoreReadOnly);
    bool writeCompleted = RorkHookProtectedPointerWriteIsAvailable(
        result,
        supportsTPRO,
        tproWindowOpened);
    if (writeCompleted) {
        memcpy(slot, &value, slotSize);
        sys_dcache_flush(slot, slotSize);
    }

    if (restoreReadOnly != NULL) {
        restoreReadOnly();
    }

    if (result != KERN_SUCCESS) {
        // `vm_protect` left the page protection unchanged, so there is nothing
        // to put back. A completed write used only the verified TPRO window.
        return writeCompleted;
    }

    bool protectionsRestored = RorkHookRestorePointerPageProtections(
        firstPage,
        lastPage,
        pageSize,
        spansPages,
        firstOriginalProtection,
        lastOriginalProtection);
    return writeCompleted && protectionsRestored;
}

/// Reports whether TPRO is enabled for the current process.
bool RorkHookSupportsTPRO(void) {
#if RORK_HOOK_DEVICE_ARM64
    bool hardwareSupportsTPRO = RorkHookHardwareSupportsTPRO();
    if (!hardwareSupportsTPRO) {
        return false;
    }

#if RORK_HOOK_HAS_PROCESS_SECURITY_CONFIG
    if (__builtin_available(iOS 26.0, *)) {
        os_security_config_t configuration = os_security_config_get();
        bool processConfigurationEnablesTPRO =
            (configuration & OS_SECURITY_CONFIG_TPRO) != 0;
        return RorkHookProcessUsesTPRO(
            hardwareSupportsTPRO,
            true,
            processConfigurationEnablesTPRO,
            false);
    }
#endif

    return RorkHookProcessUsesTPRO(
        hardwareSupportsTPRO,
        false,
        false,
        RorkHookProbeProcessTPRO());
#else
    return false;
#endif
}

/// Reports whether this thread currently has its TPRO write bit set.
bool RorkHookThreadCanWriteTPRO(void) {
#if RORK_HOOK_DEVICE_ARM64
    return RorkHookSupportsTPRO() && RorkHookThreadTPROWriteEnabledRaw() != 0;
#else
    return false;
#endif
}

/// Enables the calling thread's TPRO write state on supported devices.
void RorkHookBeginThreadTPROWrite(void) {
#if RORK_HOOK_DEVICE_ARM64
    if (!RorkHookSupportsTPRO()) {
        return;
    }
    uint64_t enableState =
        RorkHookCommPageValue(RORK_HOOK_COMM_PAGE_TPRO_WRITE_ENABLE);
    RorkHookWriteThreadTPROState(enableState);
#endif
}

/// Disables the calling thread's TPRO write state on supported devices.
void RorkHookEndThreadTPROWrite(void) {
#if RORK_HOOK_DEVICE_ARM64
    if (!RorkHookSupportsTPRO()) {
        return;
    }
    uint64_t disableState =
        RorkHookCommPageValue(RORK_HOOK_COMM_PAGE_TPRO_WRITE_DISABLE);
    RorkHookWriteThreadTPROState(disableState);
#endif
}
