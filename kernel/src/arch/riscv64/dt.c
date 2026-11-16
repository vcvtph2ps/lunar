#include <arch/riscv64/dt.h>
#include <common/init.h>
#include <common/libfdt.h>
#include <common/log.h>
#include <memory/ptm.h>

const void* arch_dt_blob(void) {
    if(g_init_boot_info == nullptr || g_init_boot_info->dtb_physical == 0) {
        return nullptr;
    }

    const void* fdt = PTM_TO_HHDM(g_init_boot_info->dtb_physical);

    int rc = fdt_check_header(fdt);
    if(rc != 0) {
        LOG_WARN("invalid FDT header at 0x%lx (err=%d)\n", (unsigned long) g_init_boot_info->dtb_physical, rc);
        return nullptr;
    }

    return fdt;
}

int arch_dt_node_offset_by_compatible_list(const void* fdt, int start_offset, const char* const compatibles[]) {
    if(compatibles[0] == nullptr) {
        return -FDT_ERR_BADVALUE;
    }

    int offset = start_offset;
    while(true) {
        int node = fdt_next_node(fdt, offset, nullptr);
        if(node < 0) {
            return node;
        }

        for(size_t i = 0; compatibles[i] != nullptr; i++) {
            if(fdt_node_check_compatible(fdt, node, compatibles[i]) == 0) {
                return node;
            }
        }

        offset = node;
    }
}

bool arch_dt_get_reg(const void* fdt, int node_offset, uint64_t* address, uint64_t* size) {
    int parent = fdt_parent_offset(fdt, node_offset);
    if(parent < 0) {
        parent = node_offset;
    }

    int address_cells = fdt_address_cells(fdt, parent);
    int size_cells = fdt_size_cells(fdt, parent);
    if(address_cells < 0 || size_cells < 0 || address_cells > 2 || size_cells > 2) {
        return false;
    }

    int len = 0;
    const fdt32_t* reg = (const fdt32_t*) fdt_getprop(fdt, node_offset, "reg", &len);
    if(reg == nullptr) {
        return false;
    }

    size_t word_count = (size_t) address_cells + (size_t) size_cells;
    if(word_count == 0 || len < (int) (word_count * sizeof(fdt32_t))) {
        return false;
    }

    uint64_t parsed_address = 0;
    for(int i = 0; i < address_cells; i++) {
        parsed_address = (parsed_address << 32) | fdt32_to_cpu(reg[i]);
    }

    uint64_t parsed_size = 0;
    for(int i = 0; i < size_cells; i++) {
        parsed_size = (parsed_size << 32) | fdt32_to_cpu(reg[address_cells + i]);
    }

    *address = parsed_address;
    if(size != nullptr) {
        *size = parsed_size;
    }
    return true;
}

bool arch_dt_get_interrupt(const void* fdt, int node_offset, uint32_t* interrupt) {
    int len = 0;
    const fdt32_t* prop = (const fdt32_t*) fdt_getprop(fdt, node_offset, "interrupts", &len);
    if(prop == nullptr || len < (int) sizeof(fdt32_t)) {
        return false;
    }

    *interrupt = fdt32_to_cpu(prop[0]);
    return true;
}
