#include <common/assert.h>
#include <common/hardware/pci.h>
#include <common/log.h>
#include <lib/list.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <memory/vm.h>
#include <uacpi/acpi.h>
#include <uacpi/resources.h>
#include <uacpi/status.h>
#include <uacpi/tables.h>
#include <uacpi/types.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>

extern list_t g_pci_host_bridges;
extern list_t g_pci_devices;

extern size_t g_pci_ecam_region_count;
extern pci_ecam_region_t* g_pci_ecam_regions;

extern bool g_pci_acpi_initialized;

typedef struct {
    pci_host_bridge_t* host_bridge;
    uacpi_namespace_node* scan_parent_namespace_node;
} acpi_pci_scan_ctx_t;

typedef struct {
    uint64_t addr;
    uacpi_namespace_node* out_node;
} device_search_ctx_t;

static uacpi_iteration_decision pci_enumerate_resources(void* ctx, uacpi_resource* resource) {
    pci_host_bridge_t* bridge = (pci_host_bridge_t*) ctx;
    switch(resource->type) {
        case UACPI_RESOURCE_TYPE_ADDRESS16: {
            uacpi_resource_address16* r = &resource->address16;
            if(r->common.type != UACPI_RANGE_BUS) break;

            bridge->start_bus_number = r->minimum;
            bridge->end_bus_number = r->maximum;
            break;
        }
        case UACPI_RESOURCE_TYPE_ADDRESS32: {
            uacpi_resource_address32* r = &resource->address32;
            if(r->common.type != UACPI_RANGE_BUS) break;

            bridge->start_bus_number = r->minimum;
            bridge->end_bus_number = r->maximum;
            break;
        }
        case UACPI_RESOURCE_TYPE_ADDRESS64: {
            uacpi_resource_address64* r = &resource->address64;
            if(r->common.type != UACPI_RANGE_BUS) break;

            bridge->start_bus_number = r->minimum;
            bridge->end_bus_number = r->maximum;
            break;
        }
        case UACPI_RESOURCE_TYPE_ADDRESS64_EXTENDED: {
            uacpi_resource_address64_extended* r = &resource->address64_extended;
            if(r->common.type != UACPI_RANGE_BUS) break;

            bridge->start_bus_number = r->minimum;
            bridge->end_bus_number = r->maximum;
            break;
        }
        default: LOG_WARN("ACPI: Unknown PCI resource type %u\n", resource->type); break;
    }

    return UACPI_ITERATION_DECISION_CONTINUE;
}
static void determine_bus_number(acpi_pci_scan_ctx_t* scan_ctx) {
    uacpi_status st;

    st = uacpi_for_each_device_resource(scan_ctx->scan_parent_namespace_node, "_CRS", pci_enumerate_resources, scan_ctx->host_bridge);
    if(uacpi_likely_success(st)) { return; }

    scan_ctx->host_bridge->end_bus_number = 0xff;
    LOG_WARN("ACPI: failed to enumerate _CRS for PCI root bridge %s, status=%s\n", uacpi_namespace_node_generate_absolute_path(scan_ctx->scan_parent_namespace_node), uacpi_status_to_string(st));
    uint64_t val = 0;
    st = uacpi_eval_integer(scan_ctx->scan_parent_namespace_node, "_BBN", nullptr, &val);
    if(uacpi_likely_success(st)) {
        scan_ctx->host_bridge->start_bus_number = val;
    } else {
        LOG_WARN("ACPI: failed to evaluate _BBN for PCI root bridge %s, status=%s\n", uacpi_namespace_node_generate_absolute_path(scan_ctx->scan_parent_namespace_node), uacpi_status_to_string(st));
        scan_ctx->host_bridge->start_bus_number = 0;
    }
}
static void parse_interrupt_table(pci_host_bridge_t* pci_host_bridge, uint8_t bus, uacpi_pci_routing_table* pci_routes) {
    for(size_t i = 0; i < pci_routes->num_entries; ++i) {
        uacpi_pci_routing_table_entry* entry = &pci_routes->entries[i];

        pci_group_interrupt_entry_t* interrupt_entry = heap_alloc(sizeof(pci_group_interrupt_entry_t));
        if(!interrupt_entry) {
            LOG_WARN("Failed to allocate memory for PCI interrupt entry\n");
            continue;
        }
        memset(interrupt_entry, 0, sizeof(pci_group_interrupt_entry_t));
        interrupt_entry->bus = bus;
        interrupt_entry->device = entry->address >> 16;
        interrupt_entry->pin = entry->pin;
        interrupt_entry->active_low = true;
        interrupt_entry->level_trigger = true;
        interrupt_entry->gsi = entry->index;

        if(entry->source) {
            if(entry->index != 0) {
                LOG_WARN("Unexpected index: %x\n", entry->index);
                heap_free(interrupt_entry, sizeof(pci_group_interrupt_entry_t));
                continue;
            }

            uacpi_resources* resources;
            uacpi_status ret = uacpi_get_current_resources(entry->source, &resources);
            if(ret != UACPI_STATUS_OK) {
                LOG_WARN("Could not get resources for source: %i\n", ret);
                heap_free(interrupt_entry, sizeof(pci_group_interrupt_entry_t));
                continue;
            }

            switch(resources->entries[0].type) {
                case UACPI_RESOURCE_TYPE_IRQ: {
                    uacpi_resource_irq* irq = &resources->entries[0].irq;
                    if(irq->num_irqs < 1) {
                        LOG_WARN("Unexpected number of IRQs: %i\n", irq->num_irqs);
                        heap_free(interrupt_entry, sizeof(pci_group_interrupt_entry_t));
                        continue;
                    }
                    interrupt_entry->gsi = irq->irqs[0];

                    if(irq->triggering == UACPI_TRIGGERING_EDGE) interrupt_entry->level_trigger = false;
                    if(irq->polarity == UACPI_POLARITY_ACTIVE_HIGH) interrupt_entry->active_low = false;
                } break;
                case UACPI_RESOURCE_TYPE_EXTENDED_IRQ: {
                    uacpi_resource_extended_irq* irq = &resources->entries[0].extended_irq;
                    if(irq->num_irqs < 1) {
                        LOG_WARN("Unexpected number of IRQs: %i\n", irq->num_irqs);
                        heap_free(interrupt_entry, sizeof(pci_group_interrupt_entry_t));
                        continue;
                    }
                    interrupt_entry->gsi = irq->irqs[0];

                    if(irq->triggering == UACPI_TRIGGERING_EDGE) interrupt_entry->level_trigger = false;
                    if(irq->polarity == UACPI_POLARITY_ACTIVE_HIGH) interrupt_entry->active_low = false;
                } break;
                default: LOG_WARN("Unexpected resource type: %i\n", resources->entries[0].type); break;
            }

            uacpi_free_resources(resources);
        }

        // LOG_INFO(
        //     "PCI interrupt: device=%04x:%02x:%02x pin=%x gsi=%u type=%s, %s\n",
        //     pci_host_bridge->segment,
        //     interrupt_entry->bus,
        //     interrupt_entry->device,
        //     interrupt_entry->pin,
        //     interrupt_entry->gsi,
        //     interrupt_entry->active_low ? "Active Low" : "Active High",
        //     interrupt_entry->level_trigger ? "Level" : "Edge"
        // );
        list_push_back(&pci_host_bridge->interrupt_entries, &interrupt_entry->list_node);
    }
}
static uacpi_iteration_decision find_pci_device(void* param, uacpi_namespace_node* node, uint32_t depth) {
    device_search_ctx_t* ctx = param;
    uint64_t addr = 0;
    (void) depth;

    uacpi_status ret = uacpi_eval_integer(node, "_ADR", UACPI_NULL, &addr);
    if(ret != UACPI_STATUS_OK && ret != UACPI_STATUS_NOT_FOUND) return UACPI_ITERATION_DECISION_CONTINUE;

    if(addr == ctx->addr) {
        ctx->out_node = node;
        return UACPI_ITERATION_DECISION_BREAK;
    }

    return UACPI_ITERATION_DECISION_CONTINUE;
}

static void check_bus(acpi_pci_scan_ctx_t* scan_ctx, pci_device_t* parent_bridge, uint8_t bus);

static void check_bridge(acpi_pci_scan_ctx_t* scan_ctx, pci_device_t* pci_bridge) {
    uacpi_namespace_node* bus_node = scan_ctx->scan_parent_namespace_node;
    device_search_ctx_t search_ctx = { .addr = (uint64_t) pci_bridge->access.device << 16 | (uint64_t) pci_bridge->access.function << 8, .out_node = nullptr };
    uacpi_namespace_for_each_child(scan_ctx->scan_parent_namespace_node, find_pci_device, UACPI_NULL, UACPI_OBJECT_DEVICE_BIT, UACPI_MAX_DEPTH_ANY, &search_ctx);

    if(search_ctx.out_node) {
        bus_node = search_ctx.out_node;
        uacpi_pci_routing_table* pci_routes;
        uacpi_status ret = uacpi_get_pci_routing_table(search_ctx.out_node, &pci_routes);
        if(ret != UACPI_STATUS_OK) {
            LOG_WARN("Could not get PCI routing for root bridge bus %0x: %i (%s)\n", pci_bridge->access.bus, ret, uacpi_status_to_string(ret));
        } else {
            parse_interrupt_table(scan_ctx->host_bridge, pci_bridge->device_info.bridge_info.secondary_bus, pci_routes);
            uacpi_free_pci_routing_table(pci_routes);
        }
    }

    acpi_pci_scan_ctx_t* child_scan_ctx = heap_alloc(sizeof(acpi_pci_scan_ctx_t));
    child_scan_ctx->host_bridge = scan_ctx->host_bridge;
    child_scan_ctx->scan_parent_namespace_node = bus_node;
    check_bus(child_scan_ctx, pci_bridge, pci_bridge->device_info.bridge_info.secondary_bus);
    heap_free(child_scan_ctx, sizeof(acpi_pci_scan_ctx_t));
}

static void check_function(acpi_pci_scan_ctx_t* scan_ctx, pci_device_t* pci_function) {
    LOG_STRC(
        "PCI: Found function %04x:%02x:%02x.%u %04x:%04x (%d)\n",
        scan_ctx->host_bridge->segment,
        pci_function->access.bus,
        pci_function->access.device,
        pci_function->access.function,
        pci_function->device_info.vendor,
        pci_function->device_info.device,
        pci_function->device_info.header_type
    );
    LOG_STRC("\tclass %02x:%02x.%02x (rev %02x)\n", pci_function->device_info.class_code, pci_function->device_info.subclass_code, pci_function->device_info.prog_if, pci_function->device_info.revision_id);
    if(pci_function->device_info.is_bridge) { LOG_STRC("\tsecondary bus %u\n", pci_function->device_info.bridge_info.secondary_bus); }

    list_push_back(&g_pci_devices, &pci_function->device_list_node);
    if(pci_function->device_info.is_bridge) { check_bridge(scan_ctx, pci_function); }
}

static void check_device(acpi_pci_scan_ctx_t* scan_ctx, pci_device_t* parent_bridge, uint8_t bus, uint8_t device) {
    pci_device_t* pci_device;
    if(!pci_create_device(scan_ctx->host_bridge, parent_bridge, bus, device, 0, &pci_device)) { return; }

    LOG_STRC("PCI: Checking device %04x:%02x:%02x.0 vendor=%04x device=%04x\n", scan_ctx->host_bridge->segment, bus, device, pci_device->device_info.vendor, pci_device->device_info.device);

    check_function(scan_ctx, pci_device);
    if(pci_device->device_info.multifunction) {
        for(int function = 1; function < 8; ++function) {
            pci_device_t* pci_function;
            if(!pci_create_device(scan_ctx->host_bridge, parent_bridge, bus, device, function, &pci_function)) { continue; }
            check_function(scan_ctx, pci_function);
        }
    }
}

static void check_bus(acpi_pci_scan_ctx_t* scan_ctx, pci_device_t* parent_bridge, uint8_t bus) {
    for(int i = 0; i < 32; ++i) { check_device(scan_ctx, parent_bridge, bus, i); }
}

static void enumerate_pci_bus(acpi_pci_scan_ctx_t* scan_ctx, uint16_t bus) {
    uacpi_pci_routing_table* pci_routes;
    uacpi_status ret = uacpi_get_pci_routing_table(scan_ctx->scan_parent_namespace_node, &pci_routes);
    if(ret != UACPI_STATUS_OK) {
        LOG_WARN("PCI: Could not get routing for root bridge bus %02x: %i (%s)\n", bus, ret, uacpi_status_to_string(ret));
    } else {
        parse_interrupt_table(scan_ctx->host_bridge, bus, pci_routes);
        uacpi_free_pci_routing_table(pci_routes);
    }

    check_bus(scan_ctx, nullptr, bus);
}

pci_ecam_region_t* pci_find_ecam_region(uint16_t segment, uint8_t bus) {
    for(size_t i = 0; i < g_pci_ecam_region_count; ++i) {
        pci_ecam_region_t* region = &g_pci_ecam_regions[i];
        if(region->segment == segment && region->start_bus <= bus && region->end_bus >= bus) { return region; }
    }
    return nullptr;
}

static uacpi_iteration_decision pci_iteration_callback(void* user, uacpi_namespace_node* node, uint32_t depth) {
    (void) user;
    (void) depth;
    pci_host_bridge_t* host_bridge = heap_alloc(sizeof(pci_host_bridge_t));
    if(!host_bridge) {
        LOG_FAIL("Failed to allocate memory for PCI host bridge\n");
        return UACPI_ITERATION_DECISION_CONTINUE;
    }
    memory_set(host_bridge, 0, sizeof(pci_host_bridge_t));
    host_bridge->interrupt_entries = LIST_INIT;

    list_push_back(&g_pci_host_bridges, &host_bridge->host_bridge_list_node);

    uint64_t segment = 0;
    uacpi_eval_integer(node, "_SEG", nullptr, &segment);
    host_bridge->segment = (uint16_t) segment;

    acpi_pci_scan_ctx_t* scan_ctx = heap_alloc(sizeof(acpi_pci_scan_ctx_t));
    scan_ctx->host_bridge = host_bridge;
    scan_ctx->scan_parent_namespace_node = node;
    determine_bus_number(scan_ctx);

    // @todo: check _CBA for ecam base
    host_bridge->ecam = pci_find_ecam_region(host_bridge->segment, host_bridge->start_bus_number);
    if(!host_bridge->ecam && host_bridge->segment != 0) {
        arch_panic("ACPI: No ECAM region found for PCI root bridge %s segment %u bus %u\n", uacpi_namespace_node_generate_absolute_path(node), host_bridge->segment, host_bridge->start_bus_number);
    } else if(!host_bridge->ecam) {
        LOG_WARN("ACPI: No ECAM region found for PCI root bridge %s segment %u bus %u, using legacy PCI access\n", uacpi_namespace_node_generate_absolute_path(node), host_bridge->segment, host_bridge->start_bus_number);
    }

    if(host_bridge->ecam) {
        LOG_STRC("ACPI: Found PCI Root bridge: %s %04x:%02x-%02x 0x%p\n", uacpi_namespace_node_generate_absolute_path(node), host_bridge->segment, host_bridge->start_bus_number, host_bridge->end_bus_number, (void*) host_bridge->ecam->phys_base);
    } else {
        LOG_STRC("ACPI: Found PCI Root bridge: %s %04x:%02x-%02x (legacy access)\n", uacpi_namespace_node_generate_absolute_path(node), host_bridge->segment, host_bridge->start_bus_number, host_bridge->end_bus_number);
    }
    enumerate_pci_bus(scan_ctx, scan_ctx->host_bridge->start_bus_number);
    heap_free(scan_ctx, sizeof(acpi_pci_scan_ctx_t));

    return UACPI_ITERATION_DECISION_CONTINUE;
}

static int pci_compare_address(const pci_device_access_t* a, const pci_device_access_t* b) {
    if(a->segment != b->segment) { return a->segment < b->segment ? -1 : 1; }
    if(a->bus != b->bus) { return a->bus < b->bus ? -1 : 1; }
    if(a->device != b->device) { return a->device < b->device ? -1 : 1; }
    if(a->function != b->function) { return a->function < b->function ? -1 : 1; }
    return 0;
}

static void sort_pci_devices() {
    size_t count = g_pci_devices.count;
    pci_device_t** device_array = heap_alloc(sizeof(pci_device_t*) * count);
    if(!device_array) {
        LOG_FAIL("Failed to allocate memory for PCI device sorting\n");
        return;
    }

    while(g_pci_devices.count > 0) {
        list_node_t* node = list_pop(&g_pci_devices);
        pci_device_t* device = CONTAINER_OF(node, pci_device_t, device_list_node);
        device_array[g_pci_devices.count] = device;
    }

    for(size_t i = 1; i < count; i++) {
        pci_device_t* key = device_array[i];
        size_t j = i;

        while(j > 0 && pci_compare_address(&device_array[j - 1]->access, &key->access) > 0) {
            device_array[j] = device_array[j - 1];
            j--;
        }

        device_array[j] = key;
    }

    for(size_t i = 0; i < count; ++i) { list_push_back(&g_pci_devices, &device_array[i]->device_list_node); }
    heap_free(device_array, sizeof(pci_device_t*) * count);
}

void pci_early_init() {
    uacpi_table mcfg_table;
    uacpi_status status = uacpi_table_find_by_signature("MCFG", &mcfg_table);
    if(uacpi_unlikely_error(status)) {
        LOG_FAIL("ACPI: failed to find MCFG table, status=%s\n", uacpi_status_to_string(status));
        return;
    }

    struct acpi_mcfg* mcfg = (struct acpi_mcfg*) mcfg_table.ptr;
    size_t count = (mcfg->hdr.length - sizeof(struct acpi_mcfg)) / sizeof(struct acpi_mcfg_allocation);
    struct acpi_mcfg_allocation* allocs = (struct acpi_mcfg_allocation*) (mcfg + 1);

    g_pci_ecam_region_count = count;
    g_pci_ecam_regions = heap_alloc(sizeof(pci_ecam_region_t) * count);

    for(size_t i = 0; i < count; ++i) {
        LOG_STRC("MCFG[%zu]: seg=%u buses=%u-%u base=%016lx\n", i, allocs[i].segment, allocs[i].start_bus, allocs[i].end_bus, allocs[i].address);
        pci_ecam_region_t* region = &g_pci_ecam_regions[i];
        region->segment = allocs[i].segment;
        region->phys_base = allocs[i].address;
        region->start_bus = allocs[i].start_bus;
        region->end_bus = allocs[i].end_bus;

        size_t bus_count = region->end_bus - region->start_bus + 1;
        size_t map_size = bus_count * 256 * 8 * PAGE_SIZE_DEFAULT;
        region->mmio_base = (uint64_t) vm_map_direct(g_vm_global_address_space, VM_NO_HINT, ALIGN_UP(map_size, PAGE_SIZE_DEFAULT), VM_PROT_RW, VM_CACHE_DISABLE, region->phys_base, VM_FLAG_MMIO);

        LOG_STRC("PCI: ECAM segment %u mapped: phys=%lx virt=%lx buses=%u-%u\n", region->segment, region->phys_base, region->mmio_base, region->start_bus, region->end_bus);
    }

    uacpi_table_unref(&mcfg_table);
}

bool pci_init() {
    if(g_pci_ecam_region_count == 0) {
        LOG_FAIL("ACPI: No MCFG regions found, cannot enumerate PCI devices\n");
        return false;
    }

    const char* pci_root_ids[] = { "PNP0A03", "PNP0A08", nullptr };
    uacpi_status status = uacpi_find_devices_at(uacpi_namespace_root(), pci_root_ids, pci_iteration_callback, nullptr);
    if(uacpi_unlikely_error(status)) {
        LOG_FAIL("ACPI: failed to enumerate PCI root bridges, status=%s\n", uacpi_status_to_string(status));
        return false;
    }

    sort_pci_devices();

    log_print(LOG_LEVEL_INFO, "\n");
    LOG_INFO("PCI: Found %zu root bridges\n", g_pci_host_bridges.count);
    LIST_FOR_EACH(&g_pci_host_bridges, root_bridge_node) {
        pci_host_bridge_t* bridge = CONTAINER_OF(root_bridge_node, pci_host_bridge_t, host_bridge_list_node);
        LOG_INFO("PCI: Root bridge %04x buses=%u-%u ecam_phys=%lx\n", bridge->segment, bridge->start_bus_number, bridge->end_bus_number, bridge->ecam->phys_base);
    }

    LOG_INFO("PCI: Found %zu devices\n", g_pci_devices.count);

    LIST_FOR_EACH(&g_pci_devices, device_node) {
        pci_device_t* device = CONTAINER_OF(device_node, pci_device_t, device_list_node);
        LOG_INFO(
            "PCI: Device %04x:%02x:%02x.%u %04x:%04x class=%02x:%02x.%02x rev=%02x pcie=%s msi=%s\n",
            device->access.segment,
            device->access.bus,
            device->access.device,
            device->access.function,
            device->device_info.vendor,
            device->device_info.device,
            device->device_info.class_code,
            device->device_info.subclass_code,
            device->device_info.prog_if,
            device->device_info.revision_id,
            device->device_info.pcie ? "yes" : "no",
            device->device_info.msi_type == PCI_MSI_TYPE_NONE ? "none" : (device->device_info.msi_type == PCI_MSI_TYPE_MSI ? "msi" : "msix")
        );
    }

    g_pci_acpi_initialized = true;
    return true;
}
