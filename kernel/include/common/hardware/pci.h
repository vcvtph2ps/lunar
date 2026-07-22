#pragma once
#include <stdint.h>

typedef struct {
    uint16_t segment;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
} pci_device_handle_t;

uint32_t pci_address(pci_device_handle_t* handle, uint8_t offset);

uint8_t pci_arch_read8(pci_device_handle_t* handle, uint8_t offset);
uint16_t pci_arch_read16(pci_device_handle_t* handle, uint8_t offset);
uint32_t pci_arch_read32(pci_device_handle_t* handle, uint8_t offset);

void pci_arch_write8(pci_device_handle_t* handle, uint8_t offset, uint8_t value);
void pci_arch_write16(pci_device_handle_t* handle, uint8_t offset, uint16_t value);
void pci_arch_write32(pci_device_handle_t* handle, uint8_t offset, uint32_t value);
