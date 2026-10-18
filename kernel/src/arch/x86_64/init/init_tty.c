#include <arch/x86_64/hardware/16550uart.h>
#include <common/arch.h>
#include <common/framebuffer.h>
#include <common/fs/devfs.h>
#include <common/fs/vfs.h>
#include <common/init.h>
#include <common/log.h>
#include <common/userspace/tty.h>

static void tty_uart_write(void* ctx, uint8_t c) {
    (void) ctx;
    if(g_arch_16550uart_default_uart) {
        arch_16550uart_send(g_arch_16550uart_default_uart, (char) c);
    }
    framebuffer_put_char(c);
}

static void tty_uart_flush(void* ctx) {
    (void) ctx;
    framebuffer_flush();
}

void init_stage_tty(uint32_t core_id) {
    if(!INIT_CORE_IS_BSP(core_id)) {
        return;
    }

    tty_t* tty = tty_create();
    tty_set_default(tty);

    g_arch_16550uart_default_uart->recv_ctx = tty;
    g_arch_16550uart_default_uart->on_recv = tty_recv_generic;

    tty->write_ctx = nullptr;
    tty->on_write = tty_uart_write;

    tty->flush_ctx = nullptr;
    tty->on_flush = tty_uart_flush;

    vfs_result_t result = devfs_bind("tty", &g_tty_devfs_ops, tty);
    if(result != VFS_RESULT_OK) {
        arch_panic("init_tty: failed to bind /dev/tty (%d)\n", result);
    }

    LOG_OKAY("tty: bound /dev/tty\n");
}
