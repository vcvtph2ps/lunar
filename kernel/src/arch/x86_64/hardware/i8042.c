#include <arch/x86_64/hardware/i8042.h>
#include <arch/x86_64/io.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/log.h>
#include <lib/string.h>
#include <stdint.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>

#define I8042_DATA_ACK 0xFA

#define I8042_PORT_TIMEOUT 100000

#define I8042_PORT_CMD_STATUS 0x64
#define I8042_PORT_DATA 0x60

#define I8042_COMMAND_TO_PORT2 0xD4

#define I8042_COMMAND_SELF_TEST 0xAA
#define I8042_DATA_SELF_TEST_PASSED 0x55

#define I8042_COMMAND_DISABLE_PORT1 0xAD
#define I8042_COMMAND_ENABLE_PORT1 0xAE
#define I8042_COMMAND_TEST_PORT1 0xAB

#define I8042_COMMAND_DISABLE_PORT2 0xA7
#define I8042_COMMAND_ENABLE_PORT2 0xA8
#define I8042_COMMAND_TEST_PORT2 0xA9

#define I8042_COMMAND_READ_CONFIG_BYTE 0x20
#define I8042_COMMAND_WRITE_CONFIG_BYTE 0x60

#define I8042_STATUS_RDYREAD (1 << 0)
#define I8042_STATUS_RDYWRITE (1 << 1)

#define I8042_CONFIG_BYTE_IRQ1 (1 << 0)
#define I8042_CONFIG_BYTE_IRQ2 (1 << 1)
#define I8042_CONFIG_BYTE_CLOCK1 (1 << 4)
#define I8042_CONFIG_BYTE_CLOCK2 (1 << 5)
#define I8042_CONFIG_BYTE_TRANSLATION (1 << 6)

#define I8042_KEYBOARD_DEVICE_ID 0xab83

typedef struct {
    bool available;
    bool exists;
    uint16_t device_id;
} i8042_port;

i8042_port g_i8042_ports[2] = { 0 };

static bool wait_write() {
    int timeout = I8042_PORT_TIMEOUT;
    while(--timeout) {
        if(!(arch_io_port_read_u8(I8042_PORT_CMD_STATUS) & I8042_STATUS_RDYWRITE))
            return true;
        arch_spin_hint();
    }
    return false;
}

static bool wait_read() {
    int timeout = I8042_PORT_TIMEOUT;
    while(--timeout) {
        if(arch_io_port_read_u8(I8042_PORT_CMD_STATUS) & I8042_STATUS_RDYREAD)
            return true;
        arch_spin_hint();
    }
    return false;
}

static bool i8042_write(uint16_t port, uint8_t value) {
    if(wait_write()) {
        arch_io_port_write_u8(port, value);
        return true;
    }
    LOG_WARN("i8042 write failed: controller failed to respond\n");
    return false;
}

static uint16_t i8042_read(uint16_t port) {
    if(wait_read())
        return arch_io_port_read_u8(port) & 0xff;

    LOG_WARN("i8042 read failed: controller failed to respond\n");
    return ARCH_I8042_READ_FAILED;
}

uint16_t arch_i8042_port_read(bool wait) {
    if(!wait || (wait && wait_read()))
        return arch_io_port_read_u8(I8042_PORT_DATA);

    LOG_WARN("i8042 read failed: port failed to respond\n");
    return ARCH_I8042_READ_FAILED;
}

bool arch_i8042_port_write(arch_i8042_port_t port, uint8_t value) {
    if(port == ARCH_I8042_PORT_TWO) {
        if(!wait_write()) {
            LOG_WARN("i8042 write failed: port failed to respond\n");
            return false;
        }
        arch_io_port_write_u8(I8042_PORT_CMD_STATUS, I8042_COMMAND_TO_PORT2);
    }
    if(!wait_write()) {
        LOG_WARN("i8042 write failed: port failed to respond\n");
        return false;
    }
    arch_io_port_write_u8(I8042_PORT_DATA, value);
    return true;
}


#define I8042_FAIL_WRITE(PORT, VALUE) \
    if(!i8042_write((PORT), (VALUE))) \
        return false;

#define I8042_FAIL_READ(PORT)            \
    ({                                   \
        uint16_t x = i8042_read((PORT)); \
        if(x == ARCH_I8042_READ_FAILED)  \
            return false;                \
        (uint8_t) x & 0xff;              \
    })

#define I8042_FAIL_PORT_WRITE(PORT, VALUE)      \
    if(!arch_i8042_port_write((PORT), (VALUE))) \
        return false;

#define I8042_FAIL_PORT_READ(PORT)                 \
    ({                                             \
        uint16_t x = arch_i8042_port_read((PORT)); \
        if(x == ARCH_I8042_READ_FAILED)            \
            return false;                          \
        (uint8_t) x & 0xff;                        \
    })


bool arch_i8042_set_translation(bool enable) {
    I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, 0x20);
    uint8_t configuration_byte = i8042_read(I8042_PORT_DATA);

    if(enable)
        configuration_byte |= I8042_CONFIG_BYTE_TRANSLATION;
    else
        configuration_byte &= ~I8042_CONFIG_BYTE_TRANSLATION;

    I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, 0x60);
    I8042_FAIL_WRITE(I8042_PORT_DATA, configuration_byte);
    return true;
}

bool arch_i8042_port_enable(arch_i8042_port_t port) {
    return i8042_write(I8042_PORT_CMD_STATUS, port == ARCH_I8042_PORT_ONE ? I8042_COMMAND_ENABLE_PORT1 : I8042_COMMAND_ENABLE_PORT2);
}

static inline bool port_initialize(arch_i8042_port_t port) {
    uint8_t ack;
    /// Reset device
    I8042_FAIL_PORT_WRITE(port, 0xFF);

    ack = I8042_FAIL_READ(I8042_PORT_DATA);
    if(ack != I8042_DATA_ACK)
        return false;

    uint8_t reset = I8042_FAIL_READ(I8042_PORT_DATA);

    /// Flush any extra bytes sent during reset
    while(arch_io_port_read_u8(I8042_PORT_CMD_STATUS) & I8042_STATUS_RDYREAD) {
        arch_io_port_read_u8(I8042_PORT_DATA);
        arch_spin_hint();
    }

    if(reset != 0xAA)
        return false;

    /// Disable Scanning command
    I8042_FAIL_PORT_WRITE(port, 0xF5);

    ack = I8042_FAIL_READ(I8042_PORT_DATA);
    if(ack != I8042_DATA_ACK)
        return false;

    /// Identify
    I8042_FAIL_PORT_WRITE(port, 0xF2);

    ack = I8042_FAIL_READ(I8042_PORT_DATA);
    if(ack != I8042_DATA_ACK)
        return false;

    g_i8042_ports[port].device_id = ((uint16_t) I8042_FAIL_READ(I8042_PORT_DATA)) << 8;
    g_i8042_ports[port].device_id |= (uint16_t) I8042_FAIL_READ(I8042_PORT_DATA);

    /// Enable Scanning command
    I8042_FAIL_PORT_WRITE(port, 0xF4);

    ack = I8042_FAIL_READ(I8042_PORT_DATA);
    if(ack != I8042_DATA_ACK)
        return false; /// NOLINT
    return true;
}

void arch_ps2kbd_init(arch_i8042_port_t port);

static void port_initialize_drivers(arch_i8042_port_t port) {
    if(g_i8042_ports[port].device_id == I8042_KEYBOARD_DEVICE_ID) {
        arch_ps2kbd_init(port);
    }
}

bool arch_i8042_init() {
    uacpi_table tbl;
    uacpi_table_find_by_signature(ACPI_FADT_SIGNATURE, &tbl);
    if(tbl.hdr != nullptr) {
        struct acpi_fadt* fadt = (struct acpi_fadt*) tbl.hdr;
        if((fadt->iapc_boot_arch & ACPI_IA_PC_8042) == 0) {
            LOG_WARN("no i8042 controller is present\n");
            return false;
        }
    } else {
        LOG_WARN("Failed to find FADT, assuming i8042 is present\n");
    }

    LOG_INFO("Initializing controller\n");
    memory_zero(g_i8042_ports, sizeof(g_i8042_ports));

    /// Reset the controller and devices
    I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_ENABLE_PORT1);
    I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_ENABLE_PORT2);

    arch_io_port_read_u8(I8042_PORT_DATA);

    I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_READ_CONFIG_BYTE);

    uint8_t configuration_byte = I8042_FAIL_READ(I8042_PORT_DATA);
    configuration_byte &= ~(I8042_CONFIG_BYTE_IRQ1 | I8042_CONFIG_BYTE_IRQ2 | I8042_CONFIG_BYTE_TRANSLATION);
    I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_WRITE_CONFIG_BYTE);
    I8042_FAIL_WRITE(I8042_PORT_DATA, configuration_byte);

    /// Self test to make sure the controller works
    I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_SELF_TEST);
    uint8_t self_test = I8042_FAIL_READ(I8042_PORT_DATA);
    if(self_test != I8042_DATA_SELF_TEST_PASSED) {
        LOG_FAIL("controller failed self test\n");
        return false;
    }

    I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_WRITE_CONFIG_BYTE);
    I8042_FAIL_WRITE(I8042_PORT_DATA, configuration_byte);

    /// Check for dual channel support
    I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_ENABLE_PORT2);
    I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_READ_CONFIG_BYTE);
    configuration_byte = I8042_FAIL_READ(I8042_PORT_DATA);
    bool dual_channel = (configuration_byte & I8042_CONFIG_BYTE_CLOCK2) == 0;
    if(dual_channel) {
        I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_DISABLE_PORT2);
        LOG_INFO("controller has dual channel support\n");
    } else {
        LOG_WARN("controller does not have dual channel support\n");
    }

    /// Test ports
    I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_TEST_PORT1);
    g_i8042_ports[0].exists = true;
    g_i8042_ports[0].available = I8042_FAIL_READ(I8042_PORT_DATA) == 0;
    if(dual_channel) {
        I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_TEST_PORT2);
        g_i8042_ports[1].exists = true;
        g_i8042_ports[1].available = I8042_FAIL_READ(I8042_PORT_DATA) == 0;
    } else {
        g_i8042_ports[1].exists = false;
        g_i8042_ports[1].available = false;
    }

    /// Enable IRQs
    I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_READ_CONFIG_BYTE);
    configuration_byte = I8042_FAIL_READ(I8042_PORT_DATA);
    if(g_i8042_ports[0].available)
        configuration_byte |= I8042_CONFIG_BYTE_IRQ1;
    if(g_i8042_ports[1].available)
        configuration_byte |= I8042_CONFIG_BYTE_IRQ2;
    I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_WRITE_CONFIG_BYTE);
    I8042_FAIL_WRITE(I8042_PORT_DATA, configuration_byte);

    /// Initialize port one
    if(g_i8042_ports[0].available) {
        I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_ENABLE_PORT1);
        if(!port_initialize(ARCH_I8042_PORT_ONE))
            g_i8042_ports[0].available = false;
        I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_DISABLE_PORT1);
    }

    /// Initialize port two
    if(g_i8042_ports[1].available) {
        I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_ENABLE_PORT2);
        if(!port_initialize(ARCH_I8042_PORT_TWO)) {
            LOG_FAIL("failed to initialize port 2\n");
            g_i8042_ports[1].available = false;
        }
        I8042_FAIL_WRITE(I8042_PORT_CMD_STATUS, I8042_COMMAND_DISABLE_PORT2);
    }

    if(g_i8042_ports[0].available) {
        LOG_OKAY("successfully initialized port 1 with device id 0x%04x\n", g_i8042_ports[0].device_id);
    } else {
        LOG_FAIL("failed to initialize port 1\n");
    }

    if(g_i8042_ports[1].available) {
        LOG_OKAY("successfully initialized port 2 with device id 0x%04x\n", g_i8042_ports[1].device_id);
    } else {
        if(g_i8042_ports[1].exists) {
            LOG_FAIL("failed to initialize port 2\n");
        } else {
            LOG_WARN("port 2 does not exist\n");
        }
    }

    /// Initialize drivers for the port(s)
    if(g_i8042_ports[0].available)
        port_initialize_drivers(ARCH_I8042_PORT_ONE);
    if(g_i8042_ports[1].available)
        port_initialize_drivers(ARCH_I8042_PORT_TWO);

    /// Flush any leftover bytes in the buffer
    while(arch_io_port_read_u8(I8042_PORT_CMD_STATUS) & I8042_STATUS_RDYREAD) {
        arch_io_port_read_u8(I8042_PORT_DATA);
        arch_spin_hint();
    }

    return true;
}
