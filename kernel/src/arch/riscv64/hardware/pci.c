#include <common/arch.h>
#include <common/hardware/pci.h>

[[noreturn]] uint32_t pci_device_read_u32(pci_device_access_t* access, uint16_t offset) {
    (void) access;
    (void) offset;
    arch_panic("Unimplemented\n");
}

[[noreturn]] void pci_device_write_u32(pci_device_access_t* access, uint16_t offset, uint32_t value) {
    (void) access;
    (void) offset;
    (void) value;
    arch_panic("Unimplemented\n");
}
