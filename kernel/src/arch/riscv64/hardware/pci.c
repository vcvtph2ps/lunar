#include <common/arch.h>
#include <common/hardware/pci.h>

uint8_t pci_arch_read8(pci_device_handle_t* handle, uint8_t offset) {
    (void) handle;
    (void) offset;
    arch_panic("Unimplemented");
}

uint16_t pci_arch_read16(pci_device_handle_t* handle, uint8_t offset) {
    (void) handle;
    (void) offset;
    arch_panic("Unimplemented");
}

uint32_t pci_arch_read32(pci_device_handle_t* handle, uint8_t offset) {
    (void) handle;
    (void) offset;
    arch_panic("Unimplemented");
}

[[noreturn]] void pci_arch_write8(pci_device_handle_t* handle, uint8_t offset, uint8_t value) {
    (void) handle;
    (void) offset;
    (void) value;
    arch_panic("Unimplemented");
}

[[noreturn]] void pci_arch_write16(pci_device_handle_t* handle, uint8_t offset, uint16_t value) {
    (void) handle;
    (void) offset;
    (void) value;
    arch_panic("Unimplemented");
}

[[noreturn]] void pci_arch_write32(pci_device_handle_t* handle, uint8_t offset, uint32_t value) {
    (void) handle;
    (void) offset;
    (void) value;
    arch_panic("Unimplemented");
}
