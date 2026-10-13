/* STM32G071 port internals (same .noinit layout as the STM32L151 port) (not used outside port/). */
#ifndef __PORT_STM32_H__
#define __PORT_STM32_H__

#include <stdint.h>

#define PORT_JUMP_MAGIC			(0x4A4D5021UL)	/* boot -> app */
#define PORT_REASON_MAGIC		(0x52535421UL)	/* reset_reason valid */
#define PORT_FAULT_MAGIC		(0x46415521UL)	/* previous run hit a HardFault */
#define PORT_FATAL_MAGIC		(0x46544C21UL)	/* previous run ended in FATAL() */
#define PORT_RUN_MAGIC			(0x52554E21UL)	/* run_task_sig valid */
#define PORT_STACK_FILL			(0xA5A5A5A5UL)	/* stack watermark pattern */

/* .noinit RAM (start of SRAM, same address in boot and app): survives soft reset. */
typedef struct {
	uint32_t jump_magic;
	uint32_t jump_addr;
	uint32_t reason_magic;
	uint32_t reset_reason;
	uint32_t fault_magic;
	uint32_t fault_pc;
	uint32_t fault_lr;
	uint32_t fault_cfsr;
	/* appended in 1.2.0 (older bootloaders keep their .data here: with them the
	 * fields below are lost across a reset, nothing else breaks) */
	uint32_t fatal_magic;
	uint32_t fatal_tag;			/* first 4 chars of the FATAL tag */
	uint32_t fatal_code;
	uint32_t run_magic;
	uint32_t run_task_sig;		/* (task id << 8) | signal being handled */
} port_noinit_t;

extern port_noinit_t port_noinit;

#endif
