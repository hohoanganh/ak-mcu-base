/* Application, STM32L151CB: APP partition right after the bootloader
 * (__boot_size: 12K, 16K with signed images). Region covers the rest of
 * internal flash; the real partition size comes from the build system
 * (--defsym __app_part_size=...) and is checked below: 116K with external SPI
 * staging, 58K with internal staging (112K / 56K when signed).
 * [APP + 0x000] .fw_header (256 B image header, patched by tools/mkimage.py)
 * [APP + 0x100] vector table
 * Keep in sync with port_cfg.h. */
ENTRY(reset_handler)

MEMORY {
	FLASH (rx)	: ORIGIN = 0x08000000 + __boot_size, LENGTH = 128K - __boot_size
	RAM (rwx)	: ORIGIN = 0x20000000, LENGTH = __ram_size	/* 16K, or 32K on L151CB-A (port_cfg.h) */
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_min_stack = 2K;

SECTIONS {
	.fw_header : {
		KEEP(*(.fw_header))
	} > FLASH

	INCLUDE sections.ld
}

ASSERT(ADDR(.fw_header) == ORIGIN(FLASH), ".fw_header must be first in APP")
ASSERT(SIZEOF(.fw_header) == 256, ".fw_header must be 256 bytes")
ASSERT(_isr_vector == ORIGIN(FLASH) + 256, "vector table must follow the header")
ASSERT(ADDR(.noinit) == ORIGIN(RAM), ".noinit must start at RAM origin (shared with boot)")
ASSERT(LOADADDR(.data) + SIZEOF(.data) - ORIGIN(FLASH) <= __app_part_size, "app does not fit in the APP partition")
