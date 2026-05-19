ENTRY(riscv_worker_entry)

WORKER_LOAD_ADDR = 0x20060000;

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = WORKER_LOAD_ADDR;

    .text : ALIGN(4)
    {
        KEEP(*(.text.worker_stub))
        *(.text*)
        *(.rodata*)
    } :text

    .data : ALIGN(4)
    {
        *(.data*)
    } :data

    .bss : ALIGN(4)
    {
        *(.bss*)
        *(COMMON)
    } :data
}
