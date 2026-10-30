/**
 ******************************************************************************
 * @brief:  HAL - minimal hardware interface required by the base.
 *
 * Every port (port/<name>/) implements ALL functions below. Code above the HAL
 * (kernel, services, boot, app) never includes chip headers.
 *
 * New chip = new port/<chip>/ implementing this file + hal_flash.h + ak_port.h,
 * plus startup code and linker scripts. See docs/porting.md.
 ******************************************************************************
**/

#ifndef __HAL_H__
#define __HAL_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#include "hal_flash.h"

/*----------------------------------------------------------------------------
 * System
 *--------------------------------------------------------------------------*/
#define HAL_RESET_REASON_UNKNOWN	(0)
#define HAL_RESET_REASON_POWER_ON	(1)
#define HAL_RESET_REASON_PIN		(2)
#define HAL_RESET_REASON_SOFTWARE	(3)
#define HAL_RESET_REASON_WATCHDOG	(4)

/* Clock, 1 ms systick, console, LED. Called by port startup before main(). */
extern void hal_init(void);

extern uint32_t hal_millis(void);
extern void hal_delay_ms(uint32_t ms);
extern uint8_t hal_reset_reason(void);
extern void hal_reset(void) __attribute__((noreturn));

/* Board name; must match board[] in the image header (mkimage.py --board).
 * Prevents installing an image built for another board. */
extern const char* hal_board_name(void);

/* Chip identity (STM32: DBGMCU DEV_ID), 0 when unknown or not readable. */
extern uint32_t hal_chip_id(void);

/* Bootloader only: start the application whose vector table is at vector_addr. */
extern void hal_jump_to_app(uint32_t vector_addr) __attribute__((noreturn));

/* Sanity check of a vector table (SP in RAM, reset handler inside APP, Thumb
 * bit...). The port defines what "sane" means. */
extern uint8_t hal_vector_ok(uint32_t initial_sp, uint32_t reset_handler);

/*----------------------------------------------------------------------------
 * Crash facts of the PREVIOUS run. The port keeps them in RAM that survives a
 * reset; nothing is written to NVM inside a fault handler. services/sys/
 * crash_log stores them at the next start.
 *--------------------------------------------------------------------------*/
#define HAL_CRASH_NONE				(0)
#define HAL_CRASH_HARDFAULT			(1)
#define HAL_CRASH_FATAL				(2)

typedef struct {
	uint8_t  kind;		/* HAL_CRASH_* */
	uint8_t  code;		/* FATAL code */
	uint32_t pc;
	uint32_t lr;
	uint32_t info;		/* HARDFAULT: fault status register. FATAL: first 4 chars of the tag */
} hal_crash_t;

/* Returns 1 and fills *out if the previous run ended in a HardFault or a
 * FATAL. Reading clears it. */
extern uint8_t hal_crash_take(hal_crash_t* out);

/* Task id / signal the kernel was handling when the previous run ended
 * (AK_TASK_IDLE_ID if it was idle or the information is not available). */
extern void hal_last_dispatch(uint8_t* task_id, uint8_t* sig);

/* Bytes of RAM between the heap and the stack that were never used since
 * start (stack high-water mark). 0 if the port cannot tell. */
extern uint32_t hal_stack_unused(void);

/*----------------------------------------------------------------------------
 * Console (UART). RX is interrupt driven into a ring buffer; getc never blocks.
 *--------------------------------------------------------------------------*/
extern void hal_console_putc(uint8_t c);
extern int  hal_console_getc(void);		/* -1 if no byte available */
extern void hal_console_flush(void);	/* wait until TX is complete (before reset) */

/*----------------------------------------------------------------------------
 * Status LED
 *--------------------------------------------------------------------------*/
extern void hal_led_set(uint8_t on);
extern void hal_led_toggle(void);

/*----------------------------------------------------------------------------
 * Watchdog. Cannot be stopped once started (like STM32 IWDG).
 *--------------------------------------------------------------------------*/
extern void hal_wdt_start(uint32_t timeout_ms);
extern void hal_wdt_kick(void);

/*----------------------------------------------------------------------------
 * Small byte-writable NVM, retained across reset/power loss; holds boot_ctrl.
 * STM32L1: data EEPROM. Chips without EEPROM: a dedicated flash page.
 *--------------------------------------------------------------------------*/
#define HAL_NVM_SIZE				(256)
extern int hal_nvm_read(uint32_t offset, void* buf, uint32_t len);
extern int hal_nvm_write(uint32_t offset, const void* buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_H__ */
