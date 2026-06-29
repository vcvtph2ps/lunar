
OUTPUT_FORMAT(elf64-x86-64)

ENTRY(kernel_entry)

PHDRS
{
    prekernel_boot_info PT_LOAD;
    text PT_LOAD;
    rodata PT_LOAD;
    data PT_LOAD;
}

SECTIONS
{
    . = 0xffffffff80000000;
    kernel_start = .;

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    prekernel_boot_info_start = .;
    .prekernel_boot_info : {
        *(.prekernel_boot_info .prekernel_boot_info.*)
    } :prekernel_boot_info
    prekernel_boot_info_end = .;

    text_start = .;
    .text : {
        *(.text .text.*)
    } :text
    text_end = .;


    . = ALIGN(CONSTANT(MAXPAGESIZE));

    rodata_start = .;
    .rodata : {
        *(.rodata .rodata.*)
    } :rodata
    rodata_end = .;

    .note.gnu.build-id : {
        *(.note.gnu.build-id)
    } :rodata
    . = ALIGN(CONSTANT(MAXPAGESIZE));

    data_start = .;
    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data
    data_end = .;
    kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}
