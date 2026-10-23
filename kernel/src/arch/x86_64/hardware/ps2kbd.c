#include <arch/x86_64/hardware/i8042.h>
#include <arch/x86_64/hardware/ioapic.h>
#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/interrupts/dpc.h>
#include <common/interrupts/interrupt.h>

#include "arch/x86_64/interrupts/interrupt_alloc.h"
#include "common/log.h"
#include "common/userspace/tty.h"

#define SCANCODE_RING_BUFFER_SIZE 256

static uint8_t g_scancode_ring_buffer[SCANCODE_RING_BUFFER_SIZE];
static size_t g_scancode_ring_buffer_head = 0;
static size_t g_scancode_ring_buffer_tail = 0;

#define ESC (0x1B)
#define BS (0x08)

const uint8_t g_lower_ascii_codes[256] = { 0x00, ESC,  '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9', '0',  '-',  '=', BS,  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u',  'i',  'o',  'p',  '[',  ']',  '\n', 0x00, 'a',
                                           's',  'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  '\'', '`', 0x00, '\\', 'z', 'x', 'c',  'v', 'b', 'n', 'm', ',', '.', '/',  0x00, '*',  0x00, ' ',  0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, '7',  '8', '9',  '-',  '4', '5', '6',  '+', '1', '2', '3', '0', '.', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

const uint8_t g_upper_ascii_codes[256] = { 0x00, ESC,  '!',  '@',  '#',  '$',  '%',  '^',  '&',  '*', '(', ')',  '_', '+', BS,  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U',  'I',  'O',  'P',  '{',  '}',  '\n', 0x00, 'A',
                                           'S',  'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  '"', '~', 0x00, '|', 'Z', 'X', 'C',  'V', 'B', 'N', 'M', '<', '>', '?',  0x00, '*',  0x00, ' ',  0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, '7', '8', '9',  '-', '4', '5', '6',  '+', '1', '2', '3', '0', '.', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
#define KEY_LSHIFT_PRESSED 0x2A
#define KEY_LSHIFT_RELEASED 0xAA

#define KEY_RSHIFT_PRESSED 0x36
#define KEY_RSHIFT_RELEASED 0xB6

#define KEY_LCTRL_PRESSED 0x1D
#define KEY_LCTRL_RELEASED 0x9D

static bool g_lshift_pressed = false;
static bool g_rshift_pressed = false;

static bool g_lctrl_pressed = false;
static bool g_rctrl_pressed = false;

static dpc_t* g_ps2kbd_dpc;

static char translate_next_scancode() {
    if(g_scancode_ring_buffer_tail == g_scancode_ring_buffer_head) {
        return 0;
    }
    bool prefixed = false;
    uint8_t scancode = g_scancode_ring_buffer[g_scancode_ring_buffer_tail];
    if(scancode == 0xE0) {
        prefixed = true;
        size_t next_index = (g_scancode_ring_buffer_tail + 1) % SCANCODE_RING_BUFFER_SIZE;
        if(next_index == g_scancode_ring_buffer_head) {
            return 0;
        }
        scancode = g_scancode_ring_buffer[next_index];
        g_scancode_ring_buffer_tail = next_index;
    }

    g_scancode_ring_buffer_tail = (g_scancode_ring_buffer_tail + 1) % SCANCODE_RING_BUFFER_SIZE;

    if(!prefixed && scancode == KEY_LSHIFT_PRESSED) {
        g_lshift_pressed = true;
    } else if(!prefixed && scancode == KEY_LSHIFT_RELEASED) {
        g_lshift_pressed = false;
    } else if(!prefixed && scancode == KEY_RSHIFT_PRESSED) {
        g_rshift_pressed = true;
    } else if(!prefixed && scancode == KEY_RSHIFT_RELEASED) {
        g_rshift_pressed = false;
    } else if(!prefixed && scancode == KEY_LCTRL_PRESSED) {
        g_lctrl_pressed = true;
    } else if(!prefixed && scancode == KEY_LCTRL_RELEASED) {
        g_lctrl_pressed = false;
    } else if(prefixed && scancode == KEY_LCTRL_PRESSED) {
        g_rctrl_pressed = true;
    } else if(prefixed && scancode == KEY_LCTRL_RELEASED) {
        g_rctrl_pressed = false;
    }

    if(g_lctrl_pressed || g_rctrl_pressed) {
        return g_lower_ascii_codes[scancode] & 0x1f;
    } else if(g_lshift_pressed || g_rshift_pressed) {
        return g_upper_ascii_codes[scancode];
    } else {
        return g_lower_ascii_codes[scancode];
    }
}

static void ps2kbd_interrupt_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) frame;
    (void) ctx;

    uint16_t scancode = arch_i8042_port_read(false);
    if(scancode == ARCH_I8042_READ_FAILED) {
        LOG_WARN("ps2kbd: failed to read in irq!");
    }
    g_scancode_ring_buffer[g_scancode_ring_buffer_head] = scancode;
    g_scancode_ring_buffer_head = (g_scancode_ring_buffer_head + 1) % SCANCODE_RING_BUFFER_SIZE;
    dpc_queue(g_ps2kbd_dpc);
}

static void ps2kbd_dpc_handler(void* data) {
    (void) data;
    if(!tty_get_default()) {
        g_scancode_ring_buffer_tail = g_scancode_ring_buffer_head;
        return;
    }

    while(g_scancode_ring_buffer_tail != g_scancode_ring_buffer_head) {
        int translated = translate_next_scancode();
        if(translated == 0)
            break;
        tty_put(tty_get_default(), translated);
    }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
void arch_ps2kbd_init(arch_i8042_port_t port) {
    if(port == ARCH_I8042_PORT_ONE) {
        arch_i8042_set_translation(true);
    }

    uint8_t vector = arch_interrupt_alloc_allocate();
    if(vector == 0) {
        arch_panic("Failed to allocate interrupt vector for PS2 keyboard\n");
    }
    LOG_INFO("interrupt vector @ 0x%x\n", vector);

    g_ps2kbd_dpc = dpc_create(ps2kbd_dpc_handler, nullptr);
    interrupt_set_hardirq_handler(vector, ps2kbd_interrupt_handler, nullptr);

    uint8_t irq = ARCH_I8042_PORT_ONE_IRQ;
    if(port == ARCH_I8042_PORT_TWO)
        irq = ARCH_I8042_PORT_TWO_IRQ;

    arch_ioapic_map_legacy_irq(irq, CPU_LOCAL_READ(lapic_id), false, true, vector);
    arch_i8042_port_enable(port);
}
#pragma clang diagnostic pop
