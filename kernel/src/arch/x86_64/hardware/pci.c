#include <arch/x86_64/io.h>
#include <common/hardware/pci.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

uint8_t pci_arch_read8(pci_device_handle_t* handle, uint8_t offset) {
    uint32_t address = pci_address(handle, offset);
    arch_io_port_write_u32(PCI_CONFIG_ADDRESS, address);
    uint32_t data = arch_io_port_read_u32(PCI_CONFIG_DATA);
    return ((data >> ((offset & 3) * 8)) & 0xff);
}

uint16_t pci_arch_read16(pci_device_handle_t* handle, uint8_t offset) {
    uint32_t address = pci_address(handle, offset);
    arch_io_port_write_u32(PCI_CONFIG_ADDRESS, address);
    uint32_t data = arch_io_port_read_u32(PCI_CONFIG_DATA);
    return ((data >> ((offset & 2) * 8)) & 0xffff);
}

uint32_t pci_arch_read32(pci_device_handle_t* handle, uint8_t offset) {
    uint32_t address = pci_address(handle, offset);
    arch_io_port_write_u32(PCI_CONFIG_ADDRESS, address);
    uint32_t data = arch_io_port_read_u32(PCI_CONFIG_DATA);
    return data;
}

void pci_arch_write8(pci_device_handle_t* handle, uint8_t offset, uint8_t value) {
    uint32_t address = pci_address(handle, offset);
    arch_io_port_write_u32(PCI_CONFIG_ADDRESS, address);

    uint32_t current = arch_io_port_read_u32(PCI_CONFIG_DATA);
    const uint32_t shift = (offset & 3) * 8;

    current &= ~(0xff << shift);
    current |= ((uint32_t) value << shift);

    arch_io_port_write_u32(PCI_CONFIG_DATA, current);
}

void pci_arch_write16(pci_device_handle_t* handle, uint8_t offset, uint16_t value) {
    uint32_t address = pci_address(handle, offset);
    arch_io_port_write_u32(PCI_CONFIG_ADDRESS, address);

    uint32_t current = arch_io_port_read_u32(PCI_CONFIG_DATA);
    const uint32_t shift = (offset & 2) * 8;

    current &= ~(0xffff << shift);
    current |= ((uint32_t) value << shift);

    arch_io_port_write_u32(PCI_CONFIG_DATA, current);
}

void pci_arch_write32(pci_device_handle_t* handle, uint8_t offset, uint32_t value) {
    uint32_t address = pci_address(handle, offset);
    arch_io_port_write_u32(PCI_CONFIG_ADDRESS, address);
    arch_io_port_write_u32(PCI_CONFIG_DATA, value);
}
