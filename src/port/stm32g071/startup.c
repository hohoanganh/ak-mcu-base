/**
 ******************************************************************************
 * @brief:  STM32G071 startup (shared by boot and app; boot is built with
 *          -DAK_BOOTLOADER). Same scheme as the STM32L151 port: boot never
 *          calls the app directly, it sets a flag in .noinit RAM and resets;
 *          reset_handler sees the flag and jumps before touching any
 *          peripheral, so the app always starts from a clean chip.
 ******************************************************************************
**/

#include <stdint.h>

#include "stm32g0xx.h"

#include "port_cfg.h"
#include "port_stm32.h"
#include "hal.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint8_t _heap_start;

extern int main(void);
extern void hal_init(void);

void reset_handler(void);
void default_handler(void);
void hard_fault_handler(void);
void systick_handler(void);
void usart2_irq_handler(void);

void nmi_handler(void)			__attribute__((weak, alias("default_handler")));
void svc_handler(void)			__attribute__((weak, alias("default_handler")));
void pendsv_handler(void)		__attribute__((weak, alias("default_handler")));
void usart1_irq_handler(void)	__attribute__((weak, alias("default_handler")));

port_noinit_t port_noinit __attribute__((section(".noinit")));

__attribute__((section(".isr_vector"), used))
void (* const isr_vector[])(void) = {
	(void (*)(void))(&_estack),
	reset_handler,
	nmi_handler,
	hard_fault_handler,
	0, 0, 0, 0, 0, 0, 0,	/* no MemManage / BusFault / UsageFault on M0+ */
	svc_handler,
	0, 0,
	pendsv_handler,
	systick_handler,

	/* IRQ 0..31 (STM32G071) */
	default_handler,	/*  0 WWDG */
	default_handler,	/*  1 PVD */
	default_handler,	/*  2 RTC_TAMP */
	default_handler,	/*  3 FLASH */
	default_handler,	/*  4 RCC */
	default_handler,	/*  5 EXTI0_1 */
	default_handler,	/*  6 EXTI2_3 */
	default_handler,	/*  7 EXTI4_15 */
	default_handler,	/*  8 UCPD1_2 */
	default_handler,	/*  9 DMA1_Channel1 */
	default_handler,	/* 10 DMA1_Channel2_3 */
	default_handler,	/* 11 DMA1_Ch4_7_DMAMUX */
	default_handler,	/* 12 ADC1_COMP */
	default_handler,	/* 13 TIM1_BRK_UP_TRG_COM */
	default_handler,	/* 14 TIM1_CC */
	default_handler,	/* 15 TIM2 */
	default_handler,	/* 16 TIM3 */
	default_handler,	/* 17 TIM6_DAC_LPTIM1 */
	default_handler,	/* 18 TIM7_LPTIM2 */
	default_handler,	/* 19 TIM14 */
	default_handler,	/* 20 TIM15 */
	default_handler,	/* 21 TIM16 */
	default_handler,	/* 22 TIM17 */
	default_handler,	/* 23 I2C1 */
	default_handler,	/* 24 I2C2 */
	default_handler,	/* 25 SPI1 */
	default_handler,	/* 26 SPI2 */
	usart1_irq_handler,	/* 27 USART1 */
	usart2_irq_handler,	/* 28 USART2 */
	default_handler,	/* 29 USART3_4_LPUART1 */
	default_handler,	/* 30 CEC */
	default_handler,	/* 31 AES_RNG */
};

#if defined(AK_BOOTLOADER)
static void __attribute__((noreturn, naked)) start_app(uint32_t sp, uint32_t pc) {
	__asm volatile (
		"msr msp, r0	\n"
		"bx  r1			\n"
	);
}
#endif

static uint8_t read_reset_reason(void) {
	uint8_t r = HAL_RESET_REASON_UNKNOWN;
	uint32_t csr = RCC->CSR;

	if (csr & RCC_CSR_PWRRSTF) {
		r = HAL_RESET_REASON_POWER_ON;
	}
	else if (csr & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) {
		r = HAL_RESET_REASON_WATCHDOG;
	}
	else if (csr & RCC_CSR_SFTRSTF) {
		r = HAL_RESET_REASON_SOFTWARE;
	}
	else if (csr & RCC_CSR_PINRSTF) {
		r = HAL_RESET_REASON_PIN;
	}
	RCC->CSR |= RCC_CSR_RMVF;
	return r;
}

void reset_handler(void) {
	uint32_t* src;
	uint32_t* dst;

#if defined(AK_BOOTLOADER)
	/* jump requested by hal_jump_to_app(): do it now, chip is still clean */
	if (port_noinit.jump_magic == PORT_JUMP_MAGIC) {
		uint32_t vt = port_noinit.jump_addr;
		port_noinit.jump_magic = 0;
		if (vt >= PORT_APP_ADDR && vt < PORT_APP_ADDR + PORT_APP_SIZE) {
			SCB->VTOR = vt;
			start_app(((uint32_t*)vt)[0], ((uint32_t*)vt)[1]);
		}
	}

	/* real reset cause, handed to the app via .noinit */
	port_noinit.reset_reason = read_reset_reason();
	port_noinit.reason_magic = PORT_REASON_MAGIC;
#else
	if (port_noinit.reason_magic != PORT_REASON_MAGIC) {
		/* app started without boot (debugger) */
		port_noinit.reset_reason = read_reset_reason();
		port_noinit.reason_magic = PORT_REASON_MAGIC;
	}
#endif

	/* our own vector table, wherever this image sits (boot, or app started by
	 * a debugger); HSI16 is the system clock after reset, nothing to set up */
	SCB->VTOR = (uint32_t)isr_vector;

	for (src = &_sidata, dst = &_sdata; dst < &_edata;) {
		*dst++ = *src++;
	}
	for (dst = &_sbss; dst < &_ebss;) {
		*dst++ = 0;
	}

	/* stack watermark: mark the free RAM (heap + stack area) up to a margin
	 * below the current stack pointer, see hal_stack_unused() */
	for (dst = (uint32_t*)&_heap_start, src = (uint32_t*)__get_MSP() - 16; dst < src;) {
		*dst++ = PORT_STACK_FILL;
	}

	hal_init();
	main();

	for (;;) {
	}
}

void default_handler(void) {
	for (;;) {
	}
}
