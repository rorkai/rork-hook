import Darwin
import RorkHook
import RorkHookTestSupport
import XCTest

/// Tests for host-runnable memory protection and protected pointer writes.
final class RorkHookMemoryTests: XCTestCase {

    /// Reads the current VM protection for assertions after temporary writes.
    private func protection(for address: vm_address_t) throws -> vm_prot_t {
        var regionAddress = address
        var regionSize: vm_size_t = 0
        var info = vm_region_basic_info_data_64_t()
        var count = mach_msg_type_number_t(MemoryLayout<vm_region_basic_info_data_64_t>.size / MemoryLayout<integer_t>.size)
        var objectName = mach_port_t(MACH_PORT_NULL)
        let result = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) { rebound in
                vm_region_64(
                    mach_task_self_,
                    &regionAddress,
                    &regionSize,
                    VM_REGION_BASIC_INFO_64,
                    rebound,
                    &count,
                    &objectName
                )
            }
        }
        if objectName != MACH_PORT_NULL {
            mach_port_deallocate(mach_task_self_, objectName)
        }
        XCTAssertEqual(result, KERN_SUCCESS)
        return info.protection
    }

    /// Returns the raw byte representation of a pointer value.
    private func pointerBytes(_ pointer: UnsafeRawPointer) -> [UInt8] {
        var stored = pointer
        return withUnsafeBytes(of: &stored) { bytes in
            Array(bytes)
        }
    }

    /// Returns the host VM page size through Darwin's function-based API.
    ///
    /// Swift 6 treats the imported `vm_page_size` symbol as shared mutable
    /// state, so the tests query the same process-level value without reading
    /// that global directly.
    private func hostPageSize() -> vm_size_t {
        vm_size_t(getpagesize())
    }

    /// Verifies pointer writes into a read-only page and protection restoration.
    func testProtectedPointerWriteMutatesReadOnlyPageAndRestoresProtection() throws {
        var address: vm_address_t = 0
        let size = hostPageSize()
        XCTAssertEqual(vm_allocate(mach_task_self_, &address, size, VM_FLAGS_ANYWHERE), KERN_SUCCESS)
        defer {
            vm_deallocate(mach_task_self_, address, size)
        }

        let slot = try XCTUnwrap(UnsafeMutableRawPointer(bitPattern: UInt(address)))
        slot.storeBytes(of: Optional<UnsafeRawPointer>.none, as: Optional<UnsafeRawPointer>.self)

        XCTAssertEqual(RorkHookProtectMemory(address, size, VM_PROT_READ), KERN_SUCCESS)
        XCTAssertEqual(try protection(for: address) & VM_PROT_READ, VM_PROT_READ)
        XCTAssertEqual(try protection(for: address) & VM_PROT_WRITE, 0)

        var value = 31337
        let valuePointer = withUnsafePointer(to: &value) { pointer in
            UnsafeRawPointer(pointer)
        }

        XCTAssertTrue(
            RorkHookStoreProtectedPointer(
                slot,
                valuePointer,
                VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY
            )
        )
        XCTAssertEqual(slot.load(as: Optional<UnsafeRawPointer>.self), valuePointer)
        XCTAssertEqual(try protection(for: address) & VM_PROT_READ, VM_PROT_READ)
        XCTAssertEqual(try protection(for: address) & VM_PROT_WRITE, 0)
    }

    /// Verifies pointer writes whose storage crosses two protected pages.
    func testProtectedPointerWriteAcrossPageBoundaryRestoresBothPages() throws {
        var address: vm_address_t = 0
        let pageSize = hostPageSize()
        let size = pageSize * 2
        XCTAssertEqual(vm_allocate(mach_task_self_, &address, size, VM_FLAGS_ANYWHERE), KERN_SUCCESS)
        defer {
            vm_deallocate(mach_task_self_, address, size)
        }

        let pointerSize = MemoryLayout<UnsafeRawPointer>.size
        let slotAddress = address + vm_address_t(pageSize) - vm_address_t(pointerSize / 2)
        let slot = try XCTUnwrap(UnsafeMutableRawPointer(bitPattern: UInt(slotAddress)))
        slot.initializeMemory(as: UInt8.self, repeating: 0, count: pointerSize)

        XCTAssertEqual(RorkHookProtectMemory(address, pageSize, VM_PROT_READ), KERN_SUCCESS)
        XCTAssertEqual(
            RorkHookProtectMemory(
                address + vm_address_t(pageSize),
                pageSize,
                VM_PROT_READ | VM_PROT_EXECUTE
            ),
            KERN_SUCCESS
        )
        XCTAssertEqual(try protection(for: address) & VM_PROT_WRITE, 0)
        XCTAssertEqual(try protection(for: address + vm_address_t(pageSize)) & VM_PROT_WRITE, 0)

        var value = 27182
        let valuePointer = withUnsafePointer(to: &value) { pointer in
            UnsafeRawPointer(pointer)
        }

        XCTAssertTrue(
            RorkHookStoreProtectedPointer(
                slot,
                valuePointer,
                VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY
            )
        )

        let actualBytes = UnsafeRawBufferPointer(
            start: UnsafeRawPointer(slot),
            count: pointerSize
        ).map { $0 }
        XCTAssertEqual(actualBytes, pointerBytes(valuePointer))
        XCTAssertEqual(try protection(for: address) & VM_PROT_WRITE, 0)
        let secondPageProtection =
            try protection(for: address + vm_address_t(pageSize))
        XCTAssertEqual(secondPageProtection & VM_PROT_WRITE, 0)
        XCTAssertEqual(secondPageProtection & VM_PROT_EXECUTE, VM_PROT_EXECUTE)
    }

    /// Verifies that a protection which cannot permit the requested write is
    /// rejected before the page protection or stored pointer changes.
    func testProtectedPointerWriteRejectsNonWritableProtection() throws {
        var address: vm_address_t = 0
        let size = hostPageSize()
        XCTAssertEqual(vm_allocate(mach_task_self_, &address, size, VM_FLAGS_ANYWHERE), KERN_SUCCESS)
        defer {
            vm_deallocate(mach_task_self_, address, size)
        }

        let slot = try XCTUnwrap(UnsafeMutableRawPointer(bitPattern: UInt(address)))
        slot.storeBytes(of: Optional<UnsafeRawPointer>.none, as: Optional<UnsafeRawPointer>.self)
        XCTAssertEqual(RorkHookProtectMemory(address, size, VM_PROT_READ), KERN_SUCCESS)

        var value = 14142
        let valuePointer = withUnsafePointer(to: &value) { pointer in
            UnsafeRawPointer(pointer)
        }

        XCTAssertFalse(RorkHookStoreProtectedPointer(slot, valuePointer, VM_PROT_READ))
        XCTAssertNil(slot.load(as: Optional<UnsafeRawPointer>.self))
        XCTAssertEqual(try protection(for: address) & VM_PROT_WRITE, 0)
    }

    /// Verifies the executable convenience API applies RX protection without
    /// leaving anonymous storage writable.
    func testMakeMemoryExecutableAppliesReadExecuteProtection() throws {
        var address: vm_address_t = 0
        let size = hostPageSize()
        XCTAssertEqual(vm_allocate(mach_task_self_, &address, size, VM_FLAGS_ANYWHERE), KERN_SUCCESS)
        defer {
            vm_deallocate(mach_task_self_, address, size)
        }

        XCTAssertEqual(RorkHookMakeMemoryExecutable(address, size), KERN_SUCCESS)
        let actual = try protection(for: address)
        XCTAssertEqual(actual & VM_PROT_READ, VM_PROT_READ)
        XCTAssertEqual(actual & VM_PROT_EXECUTE, VM_PROT_EXECUTE)
        XCTAssertEqual(actual & VM_PROT_WRITE, 0)
    }

    /// Verifies a Mach query for an unmapped address is not confused with the
    /// next mapped region in the process.
    func testMemoryRegionQueryRejectsUnmappedAddress() {
        XCTAssertTrue(RorkHookTestSupportRejectsUnmappedMemoryRegion())
    }

    /// Verifies a TPRO-enforced process never writes until the calling thread's
    /// write window has been confirmed active.
    func testProtectedPointerWriteRequiresTPROWindow() {
        XCTAssertTrue(
            RorkHookTestSupportProtectedPointerWriteRequiresTPROWindow()
        )
    }

    /// Verifies hardware capability alone does not classify an ordinary process
    /// as TPRO-enforced when the process security configuration is available.
    func testProcessTPRODetectionUsesKnownProcessConfiguration() {
        XCTAssertFalse(
            RorkHookTestSupportProcessUsesTPRO(
                true,
                true,
                false,
                true
            )
        )
        XCTAssertTrue(
            RorkHookTestSupportProcessUsesTPRO(
                true,
                true,
                true,
                false
            )
        )
    }

    /// Verifies older systems require a successful write-window probe before
    /// treating a hardware-capable process as TPRO-enforced.
    func testProcessTPRODetectionUsesLegacyWriteWindowProbe() {
        XCTAssertFalse(
            RorkHookTestSupportProcessUsesTPRO(
                true,
                false,
                false,
                false
            )
        )
        XCTAssertTrue(
            RorkHookTestSupportProcessUsesTPRO(
                true,
                false,
                false,
                true
            )
        )
    }

    /// Verifies process evidence cannot enable TPRO on unsupported hardware.
    func testProcessTPRODetectionRequiresHardwareSupport() {
        XCTAssertFalse(
            RorkHookTestSupportProcessUsesTPRO(
                false,
                true,
                true,
                true
            )
        )
    }

    /// Verifies protected writes reject unmapped storage before attempting a
    /// TPRO fallback or dereferencing the slot.
    func testProtectedPointerWriteRejectsUnmappedSlot() throws {
        let slot = try XCTUnwrap(UnsafeMutableRawPointer(bitPattern: 1))

        XCTAssertFalse(
            RorkHookStoreProtectedPointer(
                slot,
                nil,
                VM_PROT_READ | VM_PROT_WRITE
            )
        )
    }

    /// Verifies pointer-sized range arithmetic cannot wrap at the end of the
    /// native address space.
    func testProtectedPointerWriteRejectsOverflowingSlotRange() throws {
        let slot = try XCTUnwrap(
            UnsafeMutableRawPointer(
                bitPattern: UInt.max - UInt(MemoryLayout<UnsafeRawPointer>.size / 2)
            )
        )

        XCTAssertFalse(
            RorkHookStoreProtectedPointer(
                slot,
                nil,
                VM_PROT_READ | VM_PROT_WRITE
            )
        )
    }
}
