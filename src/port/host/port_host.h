/**
 ******************************************************************************
 * @brief:  Host port (Linux/macOS): runs kernel + boot + app on a PC for
 *          tests and simulation. Flash/NVM emulated in RAM, optionally
 *          persisted to a file. Test hooks: virtual time, power-cut injection.
 ******************************************************************************
**/

#ifndef __PORT_HOST_H__
#define __PORT_HOST_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <setjmp.h>

#include "hal_flash.h"

/* Emulated flash map (same as STM32L151CB so images are interchangeable) */
#define HOST_FLASH_BASE			(0x08000000UL)
#define HOST_FLASH_SIZE			(128UL * 1024UL)
#define HOST_PAGE_SIZE			(256UL)
#define HOST_RAM_START			(0x20000000UL)
#define HOST_RAM_END			(0x20004000UL)
#define HOST_BOARD_NAME			"ak-host"

/* Emulated external SPI NOR: staging + backup (same as stm32l151 external mode) */
#define HOST_EXT_STAGING_ADDR	(0x00080000UL)
#define HOST_EXT_STAGING_SIZE	(0x1D000UL)
#define HOST_EXT_BACKUP_ADDR	(0x000A0000UL)
#define HOST_EXT_BACKUP_SIZE	(0x1D000UL)
#define HOST_EXT_SECTOR			(4096UL)
#define HOST_EXT_MEM_SIZE		(HOST_EXT_BACKUP_ADDR + HOST_EXT_BACKUP_SIZE - HOST_EXT_STAGING_ADDR)

/* Reset all port state (flash erased, NVM blank). */
extern void host_reset_state(uint8_t erased_val);

/* Partition layout: 1 = staging on emulated SPI NOR (default, like the
 * board), 0 = staging in internal flash. Resets all state. */
extern void host_set_layout(uint8_t external_staging);
extern uint8_t host_layout_external(void);

/* Time source: 0 = real clock (simulation), 1 = virtual time (tests). */
extern void host_use_virtual_time(uint8_t on);
extern void host_advance_ms(uint32_t ms);

/* FATAL: longjmp(*host_fatal_jmp, 1) if set, else exit(1). */
extern jmp_buf* host_fatal_jmp;
extern const char* host_fatal_str;
extern uint8_t host_fatal_code;

/* Power-cut injection: longjmp(*jmp, 1) after n more flash erase/write ops.
 * n = 0 disables. */
extern void host_flash_power_cut(uint32_t n, jmp_buf* jmp);
extern uint32_t host_flash_ops(void);

/* Raw access to emulated memory (tests place images directly). */
extern uint8_t* host_flash_mem(void);
extern uint8_t* host_part_mem(flash_part_t part);	/* start of a partition */
extern uint8_t* host_nvm_mem(void);

/* Save / load flash + NVM to a file (firmware persists across sim runs). */
extern int host_flash_load(const char* path);
extern int host_flash_save(const char* path);

/* Console: stdin/stdout, or in-memory RX/TX queues for tests. */
extern void host_console_use_stdio(uint8_t on);
extern void host_console_inject(const uint8_t* data, uint32_t len);
extern uint32_t host_console_take_tx(uint8_t* out, uint32_t max);
/* In-memory console: FATAL after millions of reads without input catches a
 * test that forgot to end the loader (default on). 0 = an idle console is fine. */
extern void host_console_rx_guard(uint8_t on);
/* Heartbeat LED as the firmware last set it. */
extern uint8_t host_led(void);

/* Reset / jump-to-app emulation (installed by sim_main or tests). */
extern void (*host_reset_handler)(void);
extern void (*host_jump_handler)(uint32_t vector_addr);

/* Crash facts "left by the previous run" (see hal.h), set by tests. */
extern void host_crash_inject(uint8_t kind, uint8_t code, uint32_t pc, uint32_t lr, uint32_t info);
extern void host_set_reset_reason(uint8_t reason);
extern void host_set_last_dispatch(uint8_t task_id, uint8_t sig);
/* What the kernel reported through ak_port_note_dispatch(). */
extern uint8_t host_cur_task(void);
extern uint8_t host_cur_sig(void);

/* Emulated RS485 port (hal_rs485.h): inject = bytes arriving from the bus,
 * take_tx = bytes the firmware sent. */
extern void host_rs485_inject(const uint8_t* data, uint32_t len);
extern uint32_t host_rs485_take_tx(uint8_t* out, uint32_t max);
extern uint32_t host_rs485_baud(void);

/* Periodic service: feeds elapsed ms to hal_tick_hook(). */
extern void host_service(void);

#ifdef __cplusplus
}
#endif

#endif /* __PORT_HOST_H__ */
