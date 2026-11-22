#include <common/arch.h>
#include <common/framebuffer.h>
#include <common/fs/devfs.h>
#include <common/fs/vfs.h>
#include <common/hardware/uart16550.h>
#include <common/init.h>
#include <common/log.h>
#include <common/userspace/tty.h>

static void tty_uart_write(void* ctx, uint8_t c) {
    (void) ctx;
    if(g_uart16550_default) {
        uart16550_send(g_uart16550_default, (char) c);
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

    if(g_uart16550_default) {
        g_uart16550_default->recv_ctx = tty;
        g_uart16550_default->on_recv = tty_recv_generic;
    }

    tty->write_ctx = nullptr;
    tty->on_write = tty_uart_write;

    tty->flush_ctx = nullptr;
    tty->on_flush = tty_uart_flush;

    vfs_result_t result = devfs_bind("tty", (devfs_device_info_t) { .type = VFS_NODE_TYPE_CHAR_DEVICE, .major = 5, .minor = 0 }, &g_tty_devfs_ops, tty);
    if(result != VFS_RESULT_OK) {
        arch_panic("init_tty: failed to bind /dev/tty (%d)\n", result);
    }

    LOG_OKAY("tty: bound /dev/tty\n");
}
