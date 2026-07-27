#include <arch/x86_64/hardware/ec.h>
#include <arch/x86_64/io.h>
#include <common/arch.h>
#include <common/init.h>
#include <common/log.h>
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <uacpi/namespace.h>
#include <uacpi/opregion.h>
#include <uacpi/resources.h>
#include <uacpi/status.h>
#include <uacpi/types.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>

static list_t g_ec_devices = LIST_INIT;

typedef struct ec_device {
    uint16_t command_port;
    uint16_t data_port;
    spinlock_no_dw_t lock;
    list_node_t list_node;
} ec_device_t;

#define EC_STATUS_IBF (1 << 0)
#define EC_STATUS_OBF (1 << 1)

#define EC_CMD_READ 0x80
#define EC_CMD_WRITE 0x81

#define EC_TIMEOUT_ITERATIONS 1000

static bool ec_wait_ibf_clear(ec_device_t* device) {
    for(uint32_t i = 0; i < EC_TIMEOUT_ITERATIONS; ++i) {
        if((arch_io_port_read_u8(device->command_port) & EC_STATUS_IBF) == 0) { return true; }
        arch_io_wait();
    }
    return false;
}

static bool ec_wait_obf_set(ec_device_t* device) {
    for(uint32_t i = 0; i < EC_TIMEOUT_ITERATIONS; ++i) {
        if((arch_io_port_read_u8(device->command_port) & EC_STATUS_OBF) != 0) { return true; }
        arch_io_wait();
    }
    return false;
}

static bool ec_read(ec_device_t* device, uint8_t addr, uint8_t* out_data) {
    if(!ec_wait_ibf_clear(device)) {
        LOG_WARN("ec: read timeout (ibf) for address 0x%02x\n", addr);
        return false;
    }
    arch_io_port_write_u8(device->command_port, EC_CMD_READ);
    arch_io_port_write_u8(device->data_port, addr);

    if(!ec_wait_obf_set(device)) {
        LOG_WARN("ec: read timeout (obf) for address 0x%02x\n", addr);
        return false;
    }
    *out_data = arch_io_port_read_u8(device->data_port);
    return true;
}

static bool ec_write(ec_device_t* device, uint8_t addr, uint8_t data) {
    if(!ec_wait_ibf_clear(device)) {
        LOG_WARN("ec: write timeout (ibf) for address 0x%02x\n", addr);
        return false;
    }
    arch_io_port_write_u8(device->command_port, EC_CMD_WRITE);
    arch_io_port_write_u8(device->data_port, addr);

    if(!ec_wait_ibf_clear(device)) {
        LOG_WARN("ec: write timeout (ibf data) for address 0x%02x\n", addr);
        return false;
    }
    arch_io_port_write_u8(device->data_port, data);
    return true;
}

static uacpi_iteration_decision ec_parse_crs(void* ctx, uacpi_resource* resource) {
    ec_device_t* ec = (ec_device_t*) ctx;

    if(resource->type != UACPI_RESOURCE_TYPE_IO) { return UACPI_ITERATION_DECISION_CONTINUE; }
    uacpi_resource_io* r = &resource->io;

    if(ec->data_port == 0)
        ec->data_port = r->minimum;
    else if(ec->command_port == 0)
        ec->command_port = r->minimum;

    return UACPI_ITERATION_DECISION_CONTINUE;
}


static uacpi_status ec_region_handler(uacpi_region_op op, uacpi_handle op_data) {
    switch(op) {
        case UACPI_REGION_OP_ATTACH: {
            uacpi_region_attach_data* data = (uacpi_region_attach_data*) op_data;
            data->out_region_context = data->handler_context;
            LOG_INFO("ec: operation region attached (%s)\n", uacpi_namespace_node_generate_absolute_path(data->region_node));
            return UACPI_STATUS_OK;
        }
        case UACPI_REGION_OP_DETACH: {
            LOG_INFO("ec: operation region detached\n");
            return UACPI_STATUS_OK;
        }
        case UACPI_REGION_OP_READ: {
            uacpi_region_rw_data* data = (uacpi_region_rw_data*) op_data;
            ec_device_t* ec = (ec_device_t*) data->region_context;
            uint8_t addr = (uint8_t) data->address;

            spinlock_nodw_lock(&ec->lock);
            uint8_t value = 0;
            bool status = ec_read(ec, addr, &value);
            spinlock_nodw_unlock(&ec->lock);

            if(!status) { arch_panic("ec: read failed for address 0x%02x\n", addr); }

            data->value = value;
            LOG_STRC("ec: read addr=0x%02x -> 0x%02x\n", addr, (uint8_t) value);
            return UACPI_STATUS_OK;
        }
        case UACPI_REGION_OP_WRITE: {
            uacpi_region_rw_data* data = (uacpi_region_rw_data*) op_data;
            ec_device_t* ec = (ec_device_t*) data->region_context;
            uint8_t addr = (uint8_t) data->address;

            LOG_STRC("ec: write addr=0x%02x <- 0x%02x\n", addr, (uint8_t) data->value);

            spinlock_nodw_lock(&ec->lock);
            bool status = ec_write(ec, addr, (uint8_t) data->value);
            spinlock_nodw_unlock(&ec->lock);
            if(!status) { arch_panic("ec: write failed for address 0x%02x\n", addr); }
            return UACPI_STATUS_OK;
        }
        default: return UACPI_STATUS_OK;
    }
}

static bool ec_setup_device(ec_device_t* device, uacpi_namespace_node* node) {
    uint32_t sta = 0;
    uacpi_status status = uacpi_eval_sta(node, &sta);
    if(uacpi_unlikely_error(status)) {
        LOG_FAIL("ec(%s): failed to evaluate _STA method: %s\n", uacpi_namespace_node_generate_absolute_path(node), uacpi_status_to_string(status));
        return false;
    }

    if((sta & 1) == 0) {
        LOG_WARN("ec(%s): device is not present\n", uacpi_namespace_node_generate_absolute_path(node));
        return false;
    }

    status = uacpi_for_each_device_resource(node, "_CRS", ec_parse_crs, device);
    if(uacpi_unlikely_error(status)) {
        LOG_FAIL("ec(%s): failed to parse _CRS\n", uacpi_namespace_node_generate_absolute_path(node));
        return false;
    }

    assert(device->command_port != 0 && device->data_port != 0);
    assert(device->command_port != device->data_port);

    LOG_INFO("ec(%s): command_port=0x%04x, data_port=0x%04x\n", uacpi_namespace_node_generate_absolute_path(node), device->command_port, device->data_port);

    status = uacpi_install_address_space_handler(node, UACPI_ADDRESS_SPACE_EMBEDDED_CONTROLLER, ec_region_handler, device);
    if(uacpi_unlikely_error(status)) {
        LOG_FAIL("ec(%s): failed to install operation region handler: %s\n", uacpi_namespace_node_generate_absolute_path(node), uacpi_status_to_string(status));
        return false;
    }

    return true;
}

static uacpi_iteration_decision ec_find_callback(void* user, uacpi_namespace_node* node, uint32_t depth) {
    (void) user;
    (void) depth;
    LOG_INFO("ec: found device %s\n", uacpi_namespace_node_generate_absolute_path(node));

    ec_device_t* device = (ec_device_t*) heap_alloc(sizeof(ec_device_t));
    if(!device) {
        LOG_FAIL("ec: failed to allocate memory for device\n");
        return UACPI_ITERATION_DECISION_CONTINUE;
    }

    memory_zero(device, sizeof(ec_device_t));

    if(!ec_setup_device(device, node)) {
        LOG_FAIL("ec(%s): failed to setup device\n", uacpi_namespace_node_generate_absolute_path(node));
        heap_free(device, sizeof(ec_device_t));
        return UACPI_ITERATION_DECISION_CONTINUE;
    }

    list_push(&g_ec_devices, &device->list_node);

    return UACPI_ITERATION_DECISION_CONTINUE;
}

void arch_ec_init(uint32_t core_id) {
    if(!INIT_CORE_IS_BSP(core_id)) { return; }

    const char* ec_ids[] = { "PNP0C09", nullptr };
    uacpi_status status = uacpi_find_devices_at(uacpi_namespace_root(), ec_ids, ec_find_callback, nullptr);
    if(uacpi_unlikely_error(status)) {
        LOG_WARN("ec: no embedded controllers found\n");
        return;
    }

    LOG_OKAY("ec: setup %zu controllers\n", g_ec_devices.count);
}
