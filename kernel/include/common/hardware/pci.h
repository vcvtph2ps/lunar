#pragma once
#include <lib/list.h>
#include <lib/types.h>
#include <stdint.h>

typedef struct {
    uint16_t segment;
    uint64_t phys_base;
    uint64_t mmio_base;
    uint8_t start_bus;
    uint8_t end_bus;
} pci_ecam_region_t;

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t pin;

    bool active_low;
    bool level_trigger;

    uint32_t gsi;

    list_node_t list_node;
} pci_group_interrupt_entry_t;

typedef struct {
    uint16_t segment;
    uint8_t start_bus_number;
    uint8_t end_bus_number;

    pci_ecam_region_t* ecam;

    list_t interrupt_entries;

    list_node_t host_bridge_list_node;
} pci_host_bridge_t;

typedef enum {
    PCI_MSI_TYPE_NONE,
    PCI_MSI_TYPE_MSI,
    PCI_MSI_TYPE_MSIX,
} pci_msi_type_t;

typedef struct {
    uint16_t vendor;
    uint16_t device;

    uint8_t class_code;
    uint8_t subclass_code;
    uint8_t prog_if;
    uint8_t revision_id;

    bool downstream;
    uint8_t header_type;

    bool multifunction;
    bool pcie;
    pci_msi_type_t msi_type;

    bool is_bridge;

    union {
        struct {
            uint8_t secondary_bus;
        } bridge_info;
    };
} pci_device_info_t;

typedef struct pci_device pci_device_t;

typedef struct {
    uint16_t segment;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint32_t* ecam_window;
} pci_device_access_t;

static inline uint64_t pci_ecam_offset(uint8_t bus, uint8_t device, uint8_t function) {
    return ((uint64_t) bus << 20) | ((uint64_t) device << 15) | ((uint64_t) function << 12);
}

struct pci_device {
    pci_device_access_t access;

    pci_device_info_t device_info;

    list_node_t host_bridge_node;
    list_node_t device_list_node;

    pci_host_bridge_t* host_bridge;
    pci_device_t* parent_bridge;
};

/**
 * @brief Find a PCI device by its segment, bus, device, and function numbers.
 * @note This function requires ACPI to be initialized, and will only find devices that are enumerated by ACPI
 */
bool pci_device_find(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, pci_device_t** out_device);

uint8_t pci_device_read_u8(pci_device_access_t* access, uint16_t offset);
uint16_t pci_device_read_u16(pci_device_access_t* access, uint16_t offset);
uint32_t pci_device_read_u32(pci_device_access_t* access, uint16_t offset);

void pci_device_write_u8(pci_device_access_t* access, uint16_t offset, uint8_t value);
void pci_device_write_u16(pci_device_access_t* access, uint16_t offset, uint16_t value);
void pci_device_write_u32(pci_device_access_t* access, uint16_t offset, uint32_t value);

/**
 * @brief Perform early PCI initialization
 */
void pci_early_init();

/**
 * @brief Find the ECAM region for a given pci segment and bus number
 * @param segment the pci segment number
 * @param bus the pci bus number
 * @return A pointer a pci_ecam_region_t, or nullptr if not found
 */
pci_ecam_region_t* pci_find_ecam_region(uint16_t segment, uint8_t bus);

/**
 * @brief Initialize PCI subsystem
 */
bool pci_init();

/**
 * @brief Create a PCI device structure for a given bus, device, and function on a specific host bridge.
 * @param pci_host_bridge The PCI host bridge to create the device on.
 * @param bus The bus number of the device.
 * @param device The device number of the device.
 * @param function The function number of the device.
 * @param out_device Pointer to a pci_device_t structure that will be filled with the device information.
 * @return true if the device was successfully created, false otherwise.
 */
bool pci_create_device(pci_host_bridge_t* pci_host_bridge, pci_device_t* parent_bridge, uint8_t bus, uint8_t device, uint8_t function, pci_device_t** out_device);
