#include <common/assert.h>
#include <common/hardware/pci.h>
#include <common/log.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <memory/heap.h>

list_t g_pci_host_bridges = LIST_INIT;
list_t g_pci_devices = LIST_INIT;
size_t g_pci_ecam_region_count = 0;
pci_ecam_region_t* g_pci_ecam_regions = nullptr;
bool g_pci_acpi_initialized = false;

bool pci_device_find(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, pci_device_t** out_device) {
    LIST_FOR_EACH(&g_pci_devices, device_list_node) {
        pci_device_t* pci_device = CONTAINER_OF(device_list_node, pci_device_t, device_list_node);
        if(pci_device->access.segment == segment && pci_device->access.bus == bus && pci_device->access.device == device && pci_device->access.function == function) {
            *out_device = pci_device;
            return true;
        }
    };
    return false;
}

uint8_t pci_device_read_u8(pci_device_access_t* access, uint16_t offset) {
    uint32_t data = pci_device_read_u32(access, offset & 0xfffc);
    return ((data >> ((offset & 3) * 8)) & 0xff);
}

uint16_t pci_device_read_u16(pci_device_access_t* access, uint16_t offset) {
    uint32_t data = pci_device_read_u32(access, offset & 0xfffc);
    return ((data >> ((offset & 2) * 8)) & 0xffff);
}

void pci_device_write_u8(pci_device_access_t* access, uint16_t offset, uint8_t value) {
    uint32_t current = pci_device_read_u32(access, offset & 0xfffc);
    const uint32_t shift = (offset & 3) * 8;

    current &= ~(0xff << shift);
    current |= ((uint32_t) value << shift);

    pci_device_write_u32(access, offset & 0xfffc, current);
}

void pci_device_write_u16(pci_device_access_t* access, uint16_t offset, uint16_t value) {
    uint32_t current = pci_device_read_u32(access, offset & 0xfffc);
    const uint32_t shift = (offset & 2) * 8;

    current &= ~(0xffff << shift);
    current |= ((uint32_t) value << shift);

    pci_device_write_u32(access, offset & 0xfffc, current);
}

#define PCI_REGISTER_VENDOR_DEVICE 0x00

#define PCI_REGISTER_STATUS_COMMAND 0x04
#define PCI_STATUS_CAPABILITIES_LIST 0x10

#define PCI_REGISTER_CLASS_REVISION 0x08

#define PCI_REGISTER_MULTIFUNCTION_HEADER_TYPE 0x0C

#define PCI_REGISTER_CAPABILITY_POINTER 0x34

#define PCI_REGISTER_TYPE1_SECONDARY_BUS 0x18

static inline uint16_t pcie_status(pci_device_t* pci_device) {
    return (pci_device_read_u32(&pci_device->access, PCI_REGISTER_STATUS_COMMAND) >> 16) & 0xffff;
}

static inline int pcie_capability_pointer(pci_device_t* pci_device) {
    if(!(pcie_status(pci_device) & PCI_STATUS_CAPABILITIES_LIST)) { return 0; }
    return pci_device_read_u32(&pci_device->access, PCI_REGISTER_CAPABILITY_POINTER) & 0xfc;
}

static void parse_capabilities(pci_device_t* pci_device) {
    uint16_t cap_ptr = pcie_capability_pointer(pci_device);
    while(cap_ptr) {
        uint16_t cap = pci_device_read_u16(&pci_device->access, cap_ptr);
        uint8_t cap_id = cap & 0xff;
        uint8_t next_cap_ptr = (cap >> 8) & 0xff;

        LOG_STRC("\tPCIe capability at %02x: id=%02x next=%02x\n", cap_ptr, cap_id, next_cap_ptr);
        switch(cap_id) {
            case 0x1: LOG_STRC("\t\tPCI Power Management Interface\n"); break;
            case 0x4: LOG_STRC("\t\tSlot Identification\n"); break;
            case 0x5:
                pci_device->device_info.msi_type = PCI_MSI_TYPE_MSI;
                LOG_STRC("\t\tMSI Capability\n");
                break;
            case 0x6:  LOG_STRC("\t\tCompactPCI Hot Swap\n"); break;
            case 0x7:  LOG_STRC("\t\tPCI-X Capability\n"); break;
            case 0x8:  LOG_STRC("\t\tHyperTransport Capability\n"); break;
            case 0x9:  LOG_STRC("\t\tVendor Specific Capability\n"); break;
            case 0x10: {
                LOG_STRC("\t\tPCI Express Capability:\n");
                pci_device->device_info.pcie = true;

                uint16_t reg = pci_device_read_u16(&pci_device->access, cap_ptr + 2);
                LOG_STRC("\t\t\tversion %x\n", reg & 0xf);

                uint16_t device_type = (reg >> 4) & 0xf;
                LOG_STRC("\t\t\tdevice Type %i\n", device_type);

                /*
                 * 0x4 0100b Root Port of PCI Express Root Complex
                 * 0x6 0110b Downstream Port of PCI Express Switch
                 * 0x8 1000b PCI/PCI-X to PCI Express Bridge
                 */
                pci_device->device_info.downstream = device_type == 0x4 || device_type == 0x6; // || dd == 0x8
            } break;
            case 0x11:
                pci_device->device_info.msi_type = PCI_MSI_TYPE_MSIX;
                LOG_STRC("\t\tMSI-X Capability\n");
                break;
            default: LOG_STRC("\t\tUnknown (%02X)\n", cap_id); break;
        }

        if(cap_ptr == next_cap_ptr) {
            LOG_WARN("PCI: Capability pointer loop detected at %02x\n", cap_ptr);
            break;
        }

        cap_ptr = next_cap_ptr;
    }
}


static bool fill_device_info(pci_device_t* pci_device) {
    uint32_t vendor_device = pci_device_read_u32(&pci_device->access, PCI_REGISTER_VENDOR_DEVICE);
    pci_device->device_info.vendor = vendor_device & 0xffff;
    pci_device->device_info.device = vendor_device >> 16;
    if(pci_device->device_info.vendor == 0 || pci_device->device_info.vendor == 0xffff) { return false; }
    if(pci_device->device_info.device == 0 || pci_device->device_info.device == 0xffff) { return false; }

    uint32_t class_revision = pci_device_read_u32(&pci_device->access, PCI_REGISTER_CLASS_REVISION);

    pci_device->device_info.class_code = (class_revision >> 24) & 0xff;
    pci_device->device_info.subclass_code = (class_revision >> 16) & 0xff;
    pci_device->device_info.prog_if = (class_revision >> 8) & 0xff;
    pci_device->device_info.revision_id = (class_revision >> 0) & 0xff;

    uint32_t header_multifunction = pci_device_read_u32(&pci_device->access, PCI_REGISTER_MULTIFUNCTION_HEADER_TYPE);
    pci_device->device_info.multifunction = (header_multifunction >> 16) & 0x80;
    pci_device->device_info.header_type = (header_multifunction >> 16) & 0x7f;
    pci_device->device_info.is_bridge = false;

    if(pci_device->device_info.header_type == 0x01) {
        if(pci_device->device_info.class_code != 0x06 || pci_device->device_info.subclass_code != 0x04) {
            LOG_WARN(
                "PCI: Device %04x:%02x:%02x.%u has header type 0x01 but is not a PCI-to-PCI bridge (class=%02x:%02x)\n",
                pci_device->access.segment,
                pci_device->access.bus,
                pci_device->access.device,
                pci_device->access.function,
                pci_device->device_info.class_code,
                pci_device->device_info.subclass_code
            );
            return true;
        }

        pci_device->device_info.is_bridge = true;
        pci_device->device_info.bridge_info.secondary_bus = pci_device_read_u32(&pci_device->access, PCI_REGISTER_TYPE1_SECONDARY_BUS) >> 8;
    }


    return true;
}

bool pci_create_device(pci_host_bridge_t* pci_host_bridge, pci_device_t* parent_bridge, uint8_t bus, uint8_t device, uint8_t function, pci_device_t** out_device) {
    if(bus < pci_host_bridge->start_bus_number || bus > pci_host_bridge->end_bus_number) return false;
    pci_device_t* pci_device = heap_alloc(sizeof(pci_device_t));
    if(!pci_device) { return false; }

    memory_zero(pci_device, sizeof(pci_device_t));

    pci_device->access.segment = pci_host_bridge->segment;
    pci_device->access.bus = bus;
    pci_device->access.device = device;
    pci_device->access.function = function;
    pci_device->host_bridge = pci_host_bridge;
    pci_device->parent_bridge = parent_bridge;

    if(!pci_host_bridge->ecam) {
        assert(pci_host_bridge->segment == 0); // Legacy PCI only supports segment 0
        pci_device->access.ecam_window = nullptr;
    } else {
        pci_device->access.ecam_window = (uint32_t*) (pci_host_bridge->ecam->mmio_base + pci_ecam_offset(bus, device, function));
    }

    if(!fill_device_info(pci_device)) {
        heap_free(pci_device, sizeof(pci_device_t));
        return false;
    }

    parse_capabilities(pci_device);

    *out_device = pci_device;
    return true;
}
