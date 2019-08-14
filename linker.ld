OUTPUT_FORMAT("binary")

SECTIONS
{
	.text BLOCK(4K) : AT(0x100000)
	{
        *(.start)
		*(.text)
	}

	.rodata BLOCK(4K) : ALIGN(4K)
	{
		*(.rodata)
	}

	.data BLOCK(4K) : ALIGN(4K)
	{
		*(.data)
	}

	.bss BLOCK(4K) : ALIGN(4K)
	{
		*(COMMON)
		*(.bss)
		*(.bootstrap_stack)
	}

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
		*(.note.gnu.build-id)
    }
}

