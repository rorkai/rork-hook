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
/// and `size` need not be page-aligned. Returns `KERN_INVALID_ARGUMENT` when
/// either value is zero; otherwise returns the result of the underlying Mach
/// VM operation.
kern_return_t RorkHookProtectMemory(vm_address_t address,
                                    vm_size_t size,
                                    vm_prot_t protection);

/// Marks the pages covering the range readable and writable, requesting a
/// copy-on-write fault so read-only shared mappings (`__TEXT`, `__DATA_CONST`)
/// can be edited privately. Equivalent to `RorkHookProtectMemory` with
/// `VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY`.
kern_return_t RorkHookMakeMemoryWritable(vm_address_t address, vm_size_t size);

/// Marks the pages covering the range readable and executable. Equivalent to
/// `RorkHookProtectMemory` with `VM_PROT_READ | VM_PROT_EXECUTE`.
kern_return_t RorkHookMakeMemoryExecutable(vm_address_t address, vm_size_t size);

/// Writes `value` into pointer-sized storage at `slot`, even when `slot` lives
/// in read-only memory such as `__DATA_CONST` or `__AUTH_CONST`.
///
/// The function temporarily applies `protection` to the page or pages covering
/// the slot and restores the previous VM protection after the write. In a
/// TPRO-hardened arm64e process it also opens the calling thread's TPRO write
/// window around the write, because `vm_protect` can report success while the
/// hardware still faults the store; the window is closed again afterwards. The
/// pointer write is followed by a data-cache flush for the slot. `slot` must
/// address pointer storage; `value` may be `NULL`. Returns `false` without
/// changing memory when the slot is unreadable or unmapped, its pointer-sized
/// range overflows, or `protection` does not include `VM_PROT_WRITE`. A `false`
/// result can also report that the pointer was written but one of the original
/// page protections could not be restored; callers that need transactional
/// recovery must verify the stored value when this rare VM failure occurs.
bool RorkHookStoreProtectedPointer(void *slot,
                                   const void *RORK_HOOK_NULLABLE value,
                                   vm_prot_t protection);

/// Returns `true` when the current process enforces TPRO (Text Protection
/// Read-Only) hardening, where some `__DATA_CONST` regions can only be written
/// from a thread that has opened a TPRO write window.
///
/// Hardware support alone is insufficient: ordinary applications running on a
/// TPRO-capable device report `false` unless their process security
/// configuration enables TPRO. Non-arm64e devices and non-iOS platforms always
/// report `false`.
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
/// read-only. Begin/end calls are not reference-counted: do not nest independent
/// owners, and do not call ``RorkHookEndThreadTPROWrite`` for a window that was
/// already open before your code began.
void RorkHookBeginThreadTPROWrite(void);

/// Closes the calling thread's TPRO write window. A no-op when TPRO is
/// unsupported.
void RorkHookEndThreadTPROWrite(void);

RORK_HOOK_ASSUME_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* RORK_HOOK_MEMORY_H */
