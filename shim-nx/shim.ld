/* hekate's L4T launcher loads bl33.bin at 0xAA000000 and enters it there. */
ENTRY(_head)
SECTIONS
{
	. = 0xAA000000;
	.text : { *(.text.head) *(.text*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	. = ALIGN(16);
	_bss_start = .;
	.bss : { *(.bss*) *(COMMON) }
	. = ALIGN(16);
	_bss_end = .;
	. += 0x8000;
	. = ALIGN(16);
	_stack_top = .;
	_end = .;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
