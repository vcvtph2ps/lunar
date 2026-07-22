#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char* name;
    bool global;
    size_t size;
    uintptr_t address;
} ksym_kernel_symbol_t;

void ksym_load(void* symbol_data);
bool ksym_is_loaded();
bool ksym_lookup_by_address(uintptr_t address, ksym_kernel_symbol_t* symbol);
bool ksym_lookup_by_name(const char* name, ksym_kernel_symbol_t* symbol);
