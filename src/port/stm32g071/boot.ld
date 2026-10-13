/* Bootloader, STM32G071RB: __boot_size (12K) @ 0x08000000, from the build
 * system (--defsym __boot_size=...). Keep in sync with port_cfg.h. */
ENTRY(reset_handler)

MEMORY {
	FLASH (rx)	: ORIGIN = 0x08000000, LENGTH = __boot_size
	RAM (rwx)	: ORIGIN = 0x20000000, LENGTH = 36K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_min_stack = 2K;

SECTIONS {
	INCLUDE sections.ld
}

ASSERT(ADDR(.noinit) == ORIGIN(RAM), ".noinit must start at RAM origin (shared with app)")
