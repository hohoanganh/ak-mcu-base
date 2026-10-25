/**
 ******************************************************************************
 * @brief:  STM32L151 startup (shared by boot and app; boot is built with
 *          -DAK_BOOTLOADER).
 *
 *  Boot never calls the app directly: it sets a flag in .noinit RAM and
 *  resets; reset_handler sees the flag and jumps before touching any
 *  peripheral -> the app always starts from a clean chip state.
 ******************************************************************************
**/

#include <stdint.h>

#include "stm32l1xx.h"
#include "core_cm3.h"

#include "port_cfg.h"
#include "port_stm32.h"
#include "hal.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint8_t _heap_start;

extern int main(void);
extern void SystemInit(void);
extern void hal_init(void);

void reset_handler(void);
void default_handler(void);
void hard_fault_handler(void);
void systick_handler(void);
void usart1_irq_handler(void);

void nmi_handler(void)			__attribute__((weak, alias("default_handler")));
void svc_handler(void)			__attribute__((weak, alias("default_handler")));
void debug_mon_handler(void)	__attribute__((weak, alias("default_handler")));
void pendsv_handler(void)		__attribute__((weak, alias("default_handler")));
void usart2_irq_handler(void)	__attribute__((weak, alias("default_handler")));	/* rs485.c, if built */
void exti4_irq_handler(void)	__attribute__((weak, alias("default_handler")));	/* kit.c SPI sniffer, if built */
void tim6_irq_handler(void)		__attribute__((weak, alias("default_handler")));	/* fc_bench.c, if built */

port_noinit_t port_noinit __attribute__((section(".noinit")));

__attribute__((section(".isr_vector"), used))
void (* const isr_vector[])(void) = {
	(void (*)(void))(&_estack),
	reset_handler,
	nmi_handler,
	hard_fault_handler,
	hard_fault_handler,	/* MemManage (escalates to HardFault when disabled) */
	hard_fault_handler,	/* BusFault */
	hard_fault_handler,	/* UsageFault */
	0, 0, 0, 0,
	svc_handler,
	debug_mon_handler,
	0,
	pendsv_handler,
	systick_handler,

	/* IRQ 0..44 (STM32L1xx MD) */
	default_handler,	/*  0 WWDG */
	default_handler,	/*  1 PVD */
	default_handler,	/*  2 TAMPER_STAMP */
	default_handler,	/*  3 RTC_WKUP */
	default_handler,	/*  4 FLASH */
	default_handler,	/*  5 RCC */
	default_handler,	/*  6 EXTI0 */
	default_handler,	/*  7 EXTI1 */
	default_handler,	/*  8 EXTI2 */
	default_handler,	/*  9 EXTI3 */
	exti4_irq_handler,	/* 10 EXTI4 */
	default_handler,	/* 11 DMA1_Channel1 */
	default_handler,	/* 12 DMA1_Channel2 */
	default_handler,	/* 13 DMA1_Channel3 */
	default_handler,	/* 14 DMA1_Channel4 */
	default_handler,	/* 15 DMA1_Channel5 */
	default_handler,	/* 16 DMA1_Channel6 */
	default_handler,	/* 17 DMA1_Channel7 */
	default_handler,	/* 18 ADC1 */
	default_handler,	/* 19 USB_HP */
	default_handler,	/* 20 USB_LP */
	default_handler,	/* 21 DAC */
	default_handler,	/* 22 COMP */
	default_handler,	/* 23 EXTI9_5 */
	default_handler,	/* 24 LCD */
	default_handler,	/* 25 TIM9 */
	default_handler,	/* 26 TIM10 */
	default_handler,	/* 27 TIM11 */
	default_handler,	/* 28 TIM2 */
	default_handler,	/* 29 TIM3 */
	default_handler,	/* 30 TIM4 */
	default_handler,	/* 31 I2C1_EV */
	default_handler,	/* 32 I2C1_ER */
	default_handler,	/* 33 I2C2_EV */
	default_handler,	/* 34 I2C2_ER */
	default_handler,	/* 35 SPI1 */
	default_handler,	/* 36 SPI2 */
	usart1_irq_handler,	/* 37 USART1 */
	usart2_irq_handler,	/* 38 USART2 */
	default_handler,	/* 39 USART3 */
	default_handler,	/* 40 EXTI15_10 */
	default_handler,	/* 41 RTC_Alarm */
	default_handler,	/* 42 USB_FS_WKUP */
	tim6_irq_handler,	/* 43 TIM6 */
	default_handler,	/* 44 TIM7 */
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

	if (csr & RCC_CSR_PORRSTF) {
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

	SystemInit();

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
