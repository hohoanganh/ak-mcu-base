/* Shared section layout for boot.ld / app.ld (included inside SECTIONS). */

.isr_vector : {
	. = ALIGN(256);			/* Cortex-M3 VTOR: align to table size (256 B) */
	_isr_vector = .;
	KEEP(*(.isr_vector))
	. = ALIGN(4);
} > FLASH

.text : {
	*(.text)
	*(.text*)
	*(.rodata)
	*(.rodata*)
	*(.glue_7)
	*(.glue_7t)
	KEEP(*(.init))
	KEEP(*(.fini))
	. = ALIGN(4);
	_etext = .;
} > FLASH

.ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
.ARM.exidx : {
	__exidx_start = .;
	*(.ARM.exidx*)
	__exidx_end = .;
} > FLASH

.preinit_array : {
	PROVIDE_HIDDEN(__preinit_array_start = .);
	KEEP(*(.preinit_array*))
	PROVIDE_HIDDEN(__preinit_array_end = .);
} > FLASH
.init_array : {
	PROVIDE_HIDDEN(__init_array_start = .);
	KEEP(*(SORT(.init_array.*)))
	KEEP(*(.init_array*))
	PROVIDE_HIDDEN(__init_array_end = .);
} > FLASH
.fini_array : {
	PROVIDE_HIDDEN(__fini_array_start = .);
	KEEP(*(SORT(.fini_array.*)))
	KEEP(*(.fini_array*))
	PROVIDE_HIDDEN(__fini_array_end = .);
} > FLASH

/* Survives soft reset; same address in boot and app (start of SRAM). */
.noinit (NOLOAD) : {
	. = ALIGN(4);
	KEEP(*(.noinit))
	. = ALIGN(4);
} > RAM

_sidata = LOADADDR(.data);
.data : {
	. = ALIGN(4);
	_sdata = .;
	*(.ramfunc)			/* code that must run from RAM (flash half-page write) */
	*(.ramfunc*)
	*(.data)
	*(.data*)
	. = ALIGN(4);
	_edata = .;
} > RAM AT > FLASH

/* "AT > RAM" on the NOLOAD sections: without it the linker carries the
 * flash load region of .data on, and an ELF loader that honours load
 * addresses (Renode) would pour zeros over the flash after .data. */
.bss (NOLOAD) : {
	. = ALIGN(4);
	_sbss = .;
	*(.bss)
	*(.bss*)
	*(COMMON)
	. = ALIGN(4);
	_ebss = .;
} > RAM AT > RAM

/* Heap = remaining RAM minus minimum stack. RAM overflow -> link error. */
._heap (NOLOAD) : {
	. = ALIGN(8);
	_heap_start = .;
	. = ORIGIN(RAM) + LENGTH(RAM) - _min_stack;
	_heap_end = .;
} > RAM AT > RAM
