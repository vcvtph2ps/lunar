#include <arch/x86_64/io.h>
#include <common/assert.h>
#include <common/hardware/pci.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static uint32_t pci_legacy_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return (1ul << 31) | (((uint32_t) bus) << 16) | (((uint32_t) device) << 11) | (((uint32_t) function) << 8) | (offset);
}

uint32_t pci_device_read_u32(pci_device_access_t* access, uint16_t offset) {
    assert(access != nullptr);
    if(access->ecam_window == nullptr && access->segment != 0) { arch_panic("PCI: Attempted to read from device with no ECAM window\n"); }

    if(access->ecam_window) {
        return access->ecam_window[offset / 4];
    } else {
        uint32_t address = pci_legacy_address(access->bus, access->device, access->function, offset);
        arch_io_port_write_u32(PCI_CONFIG_ADDRESS, address);
        return arch_io_port_read_u32(PCI_CONFIG_DATA);
    }
}

void pci_device_write_u32(pci_device_access_t* access, uint16_t offset, uint32_t value) {
    assert(access != nullptr);
    if(access->ecam_window == nullptr && access->segment != 0) { arch_panic("PCI: Attempted to write to device with no ECAM window\n"); }

    if(access->ecam_window) {
        access->ecam_window[offset / 4] = value;
    } else {
        uint32_t address = pci_legacy_address(access->bus, access->device, access->function, offset);
        arch_io_port_write_u32(PCI_CONFIG_ADDRESS, address);
        arch_io_port_write_u32(PCI_CONFIG_DATA, value);
    }
}
