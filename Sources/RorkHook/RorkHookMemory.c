#include "RorkHookMemory.h"

#include <TargetConditionals.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/vm_page_size.h>
#include <string.h>

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
#define RORK_HOOK_COMM_PAGE_TPRO_WRITE_ENABLE  (RORK_HOOK_COMM_PAGE_START + 0x0D0)
#define RORK_HOOK_COMM_PAGE_TPRO_WRITE_DISABLE (RORK_HOOK_COMM_PAGE_START + 0x0D8)

/// Reads a 64-bit value from the fixed comm-page slot used by arm64e TPRO.
static inline uint64_t RorkHookCommPageValue(uintptr_t address) {
    return *(const volatile uint64_t *)address;
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
    return RorkHookMachVMProtectTrap(mach_task_self(), address, size, false, protection);
#else
    return vm_protect(mach_task_self(), address, size, false, protection);
#endif
}

/// Marks a range writable with copy-on-write semantics.
kern_return_t RorkHookMakeMemoryWritable(vm_address_t address, vm_size_t size) {
    return RorkHookProtectMemory(address, size, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
}

/// Marks a range readable and executable after code patching.
kern_return_t RorkHookMakeMemoryExecutable(vm_address_t address, vm_size_t size) {
    return RorkHookProtectMemory(address, size, VM_PROT_READ | VM_PROT_EXECUTE);
}

/// Returns the current VM protection for `page` so temporary slot writes can put
/// `__DATA_CONST`/`__AUTH_CONST` pages back the way they were.
static vm_prot_t RorkHookProtectionForPage(vm_address_t page) {
    vm_prot_t protection = VM_PROT_READ;
    vm_address_t regionAddress = page;
    vm_size_t regionSize = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t objectName = MACH_PORT_NULL;
    kern_return_t result = vm_region_64(mach_task_self(),
                                        &regionAddress,
                                        &regionSize,
                                        VM_REGION_BASIC_INFO_64,
                                        (vm_region_info_t)&info,
                                        &count,
                                        &objectName);
    if (result == KERN_SUCCESS) {
        protection = info.protection;
    }
    if (objectName != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), objectName);
    }
    return protection;
}

/// Opens a TPRO write window when VM protection cannot make a slot writable.
static bool RorkHookAllowTPROWrites(void (**restoreReadOnly)(void)) {
    if (!RorkHookSupportsTPRO()) {
        return false;
    }

    if (restoreReadOnly) {
        *restoreReadOnly = NULL;
    }
    if (RorkHookThreadCanWriteTPRO()) {
        return true;
    }

    RorkHookBeginThreadTPROWrite();
    if (restoreReadOnly) {
        *restoreReadOnly = RorkHookEndThreadTPROWrite;
    }
    return true;
}

/// Writes one pointer-sized value while restoring page protections afterward.
bool RorkHookStoreProtectedPointer(void *slot,
                                   const void *value,
                                   vm_prot_t protection) {
    if (slot == NULL) {
        return false;
    }

    const size_t slotSize = sizeof(const void *);
    uintptr_t slotStart = (uintptr_t)slot;
    uintptr_t slotEnd = slotStart + slotSize - 1;
    if (slotEnd < slotStart) {
        return false;
    }

    uintptr_t pageMask = (uintptr_t)vm_page_size - 1;
    vm_address_t firstPage = (vm_address_t)(slotStart & ~pageMask);
    vm_address_t lastPage = (vm_address_t)(slotEnd & ~pageMask);
    bool spansPages = firstPage != lastPage;
    vm_size_t protectSize = (vm_size_t)((lastPage - firstPage) + vm_page_size);
    vm_prot_t firstOriginalProtection = RorkHookProtectionForPage(firstPage);
    vm_prot_t lastOriginalProtection = spansPages
        ? RorkHookProtectionForPage(lastPage)
        : firstOriginalProtection;

    kern_return_t result = RorkHookProtectMemory(firstPage,
                                                 protectSize,
                                                 protection);
    void (*restoreReadOnly)(void) = NULL;
    if (result != KERN_SUCCESS && !RorkHookAllowTPROWrites(&restoreReadOnly)) {
        return false;
    }

    memcpy(slot, &value, slotSize);
    sys_dcache_flush(slot, slotSize);

    if (restoreReadOnly) {
        restoreReadOnly();
        return true;
    }

    kern_return_t firstRestore = RorkHookProtectMemory(firstPage,
                                                       vm_page_size,
                                                       firstOriginalProtection
                                                           ? firstOriginalProtection
                                                           : VM_PROT_READ);
    kern_return_t lastRestore = KERN_SUCCESS;
    if (spansPages) {
        lastRestore = RorkHookProtectMemory(lastPage,
                                            vm_page_size,
                                            lastOriginalProtection
                                                ? lastOriginalProtection
                                                : VM_PROT_READ);
    }
    return firstRestore == KERN_SUCCESS && lastRestore == KERN_SUCCESS;
}

/// Reports whether the current platform exposes the arm64e TPRO comm-page hooks.
bool RorkHookSupportsTPRO(void) {
#if RORK_HOOK_DEVICE_ARM64
    return RorkHookCommPageValue(RORK_HOOK_COMM_PAGE_TPRO_WRITE_ENABLE) != 0;
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
    uint64_t enableState = RorkHookCommPageValue(RORK_HOOK_COMM_PAGE_TPRO_WRITE_ENABLE);
    __asm__ __volatile__(
        "msr s3_6_c15_c1_5, %0\n"
        "isb sy\n"
        :: "r"(enableState)
        : "memory"
    );
#endif
}

/// Disables the calling thread's TPRO write state on supported devices.
void RorkHookEndThreadTPROWrite(void) {
#if RORK_HOOK_DEVICE_ARM64
    if (!RorkHookSupportsTPRO()) {
        return;
    }
    uint64_t disableState = RorkHookCommPageValue(RORK_HOOK_COMM_PAGE_TPRO_WRITE_DISABLE);
    __asm__ __volatile__(
        "msr s3_6_c15_c1_5, %0\n"
        "isb sy\n"
        :: "r"(disableState)
        : "memory"
    );
#endif
}
