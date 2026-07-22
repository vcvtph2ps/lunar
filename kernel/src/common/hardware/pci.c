#include <common/hardware/pci.h>

uint32_t pci_address(pci_device_handle_t* handle, uint8_t offset) {
    return (1ul << 31) | (((uint32_t) handle->bus) << 16) | (((uint32_t) handle->device) << 11) | (((uint32_t) handle->function) << 8) | (offset & 0xfc);
}
