#ifndef RORK_HOOK_MEMORY_H
#define RORK_HOOK_MEMORY_H

#include "RorkHookTypes.h"

#include <mach/mach.h>
#include <mach/vm_prot.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

RORK_HOOK_ASSUME_NONNULL_BEGIN

/// Changes the protection of the pages covering `[address, address + size)`.
///
/// On arm64 iOS hardware the call is issued through the kernel's `vm_protect`
/// trap directly, deliberately bypassing the libsystem stub. RorkHook exists to
/// rewrite import tables and code, and a hook installed over `vm_protect` must
/// not be able to interpose the very call used to install further hooks. On the
/// simulator and other platforms it forwards to `vm_protect`.
///
/// The kernel rounds the range out to enclosing page boundaries, so `address`
/// and `size` need not be page-aligned.
kern_return_t RorkHookProtectMemory(vm_address_t address,
                                    vm_size_t size,
                                    vm_prot_t protection);

/// Marks the pages covering the range readable and writable, requesting a
/// copy-on-write fault so read-only shared mappings (`__TEXT`, `__DATA_CONST`)
/// can be edited privately. Equivalent to `RorkHookProtectMemory` with
/// `VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY`.
kern_return_t RorkHookMakeMemoryWritable(vm_address_t address, vm_size_t size);

/// Restores the pages covering the range to readable and executable. Equivalent
/// to `RorkHookProtectMemory` with `VM_PROT_READ | VM_PROT_EXECUTE`.
kern_return_t RorkHookMakeMemoryExecutable(vm_address_t address, vm_size_t size);

/// Writes `value` into pointer-sized storage at `slot`, even when `slot` lives
/// in read-only memory such as `__DATA_CONST` or `__AUTH_CONST`.
///
/// The function temporarily applies `protection` to the page or pages covering
/// the slot and restores the previous VM protection after the write. If the
/// protection change fails on a TPRO-hardened arm64e device, it opens the
/// current thread's TPRO write window, performs the pointer write, flushes the
/// data cache for the slot, and closes the write window again. `slot` must
/// address pointer storage; `value` may be `NULL`.
bool RorkHookStoreProtectedPointer(void *slot,
                                   const void *RORK_HOOK_NULLABLE value,
                                   vm_prot_t protection);

/// Returns `true` when the current device enforces TPRO (Text Protection
/// Read-Only) hardening, where some `__DATA_CONST` regions can only be written
/// from a thread that has opened a TPRO write window. arm64e devices on recent
/// iOS report `true`; everything else reports `false`.
bool RorkHookSupportsTPRO(void);

/// Returns `true` when the calling thread currently has its TPRO write window
/// open. Always `false` when ``RorkHookSupportsTPRO`` is `false`.
bool RorkHookThreadCanWriteTPRO(void);

/// Opens the calling thread's TPRO write window so TPRO-protected memory can be
/// modified. Must be balanced by ``RorkHookEndThreadTPROWrite``. A no-op when
/// TPRO is unsupported.
///
/// The window is a per-thread CPU state change, not a memory-protection change;
/// pair it with ``RorkHookMakeMemoryWritable`` when the target is also mapped
/// read-only.
void RorkHookBeginThreadTPROWrite(void);

/// Closes the calling thread's TPRO write window. A no-op when TPRO is
/// unsupported.
void RorkHookEndThreadTPROWrite(void);

RORK_HOOK_ASSUME_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* RORK_HOOK_MEMORY_H */
