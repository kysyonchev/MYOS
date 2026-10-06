ENTRY(start)

SECTIONS
{
    . = 1M;

    .multiboot :
    {
        *(.multiboot)
    }

    .text :
    {
        *(.text)
    }

    .data :
    {
        *(.data)
    }

    .rodata :
    {
        *(.rodata)
    }

    .bss :
    {
        *(.bss)
    }
}