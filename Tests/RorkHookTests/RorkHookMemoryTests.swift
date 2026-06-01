import RorkHook
import XCTest

/// Tests for host-runnable memory protection and protected pointer writes.
final class RorkHookMemoryTests: XCTestCase {

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

    private func pointerBytes(_ pointer: UnsafeRawPointer) -> [UInt8] {
        var stored = pointer
        return withUnsafeBytes(of: &stored) { bytes in
            Array(bytes)
        }
    }

    func testProtectedPointerWriteMutatesReadOnlyPageAndRestoresProtection() throws {
        var address: vm_address_t = 0
        let size = vm_size_t(vm_page_size)
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

    func testProtectedPointerWriteAcrossPageBoundaryRestoresBothPages() throws {
        var address: vm_address_t = 0
        let size = vm_size_t(vm_page_size * 2)
        XCTAssertEqual(vm_allocate(mach_task_self_, &address, size, VM_FLAGS_ANYWHERE), KERN_SUCCESS)
        defer {
            vm_deallocate(mach_task_self_, address, size)
        }

        let pointerSize = MemoryLayout<UnsafeRawPointer>.size
        let slotAddress = address + vm_address_t(vm_page_size) - vm_address_t(pointerSize / 2)
        let slot = try XCTUnwrap(UnsafeMutableRawPointer(bitPattern: UInt(slotAddress)))
        slot.initializeMemory(as: UInt8.self, repeating: 0, count: pointerSize)

        XCTAssertEqual(RorkHookProtectMemory(address, size, VM_PROT_READ), KERN_SUCCESS)
        XCTAssertEqual(try protection(for: address) & VM_PROT_WRITE, 0)
        XCTAssertEqual(try protection(for: address + vm_address_t(vm_page_size)) & VM_PROT_WRITE, 0)

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
        XCTAssertEqual(try protection(for: address + vm_address_t(vm_page_size)) & VM_PROT_WRITE, 0)
    }
}
