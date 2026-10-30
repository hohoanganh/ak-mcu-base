/**
 ******************************************************************************
 * @brief:  ak_port.h + hal.h + hal_flash.h for STM32G071RB, straight on the
 *          registers (CMSIS device header only, no HAL / LL).
 *
 *  Differences from the STM32L151 port that matter above the HAL:
 *   - flash is programmed in 64-bit double words (write_size 8) and erased
 *     in 2K pages to 0xFF;
 *   - there is no data EEPROM: hal_nvm is two flash pages used alternately,
 *     each a [seq][~seq][256 B] record, the newer valid one is the NVM. Every
 *     hal_nvm_write() that changes something erases and rewrites one page
 *     (10k cycles per page: fine for boot_ctrl and the crash log, not for
 *     counters);
 *   - no SPI NOR: STAGING in internal flash, no BACKUP partition.
 ******************************************************************************
**/

#include <stddef.h>
#include <string.h>
#include <errno.h>

#include "stm32g0xx.h"

#include "ak_port.h"
#include "hal.h"
#include "port_cfg.h"
#include "port_stm32.h"

/* CMSIS system_stm32g0xx.h wants these; the chip runs on HSI16 as it wakes up */
uint32_t SystemCoreClock = PORT_SYSCLK_HZ;

void SystemInit(void) {
}

void SystemCoreClockUpdate(void) {
	SystemCoreClock = PORT_SYSCLK_HZ;
}

/* every busy wait on a peripheral flag is bounded: a missing peripheral (or
 * an emulator without it) must not hang the firmware */
#define SPIN_MAX			(200000U)

/*----------------------------------------------------------------------------
 * critical section (nestable, ISR safe)
 *--------------------------------------------------------------------------*/
static volatile uint32_t crit_nest;
static volatile uint32_t crit_primask;

void ak_port_enter_critical(void) {
	uint32_t pm = __get_PRIMASK();
	__disable_irq();
	if (crit_nest++ == 0) {
		crit_primask = pm;
	}
}

void ak_port_exit_critical(void) {
	if (crit_nest == 0) {
		ak_port_fatal("CRIT", 0x01);
	}
	if (--crit_nest == 0) {
		__set_PRIMASK(crit_primask);
	}
}

/*----------------------------------------------------------------------------
 * systick 1 ms
 *--------------------------------------------------------------------------*/
static volatile uint32_t millis;

__attribute__((weak)) void hal_tick_hook(uint32_t elapsed_ms) {
	(void)elapsed_ms;
}

void systick_handler(void) {
	millis++;
	hal_tick_hook(1);
}

uint32_t hal_millis(void) {
	return millis;
}

uint32_t ak_port_millis(void) {
	return millis;
}

void hal_delay_ms(uint32_t ms) {
	uint32_t t0 = millis;
	while ((millis - t0) < ms) {
	}
}

void ak_port_idle(void) {
	__WFI();	/* woken by SysTick / UART RX */
}

/*----------------------------------------------------------------------------
 * console USART2 (PA2/PA3): RX interrupt -> ring.
 * TX: app = ring + TXE interrupt, bootloader = polled.
 *--------------------------------------------------------------------------*/
static volatile uint8_t rx_buf[PORT_CONSOLE_RX_BUF];
static volatile uint16_t rx_head, rx_tail;

#if !defined(AK_BOOTLOADER)
static volatile uint8_t tx_buf[PORT_CONSOLE_TX_BUF];
static volatile uint16_t tx_head, tx_tail;
#endif

void usart2_irq_handler(void) {
	if (USART2->ISR & (USART_ISR_RXNE_RXFNE | USART_ISR_ORE)) {
		uint8_t c = (uint8_t)USART2->RDR;	/* reading RDR clears RXNE */
		uint16_t next = (uint16_t)((rx_head + 1) & (PORT_CONSOLE_RX_BUF - 1));

		USART2->ICR = USART_ICR_ORECF;
		if (next != rx_tail) {
			rx_buf[rx_head] = c;
			rx_head = next;
		}
	}

#if !defined(AK_BOOTLOADER)
	/* masked: hal_console_putc() from a higher priority ISR also touches
	 * the ring and CR1 */
	__disable_irq();
	if ((USART2->CR1 & USART_CR1_TXEIE_TXFNFIE) && (USART2->ISR & USART_ISR_TXE_TXFNF)) {
		if (tx_tail != tx_head) {
			USART2->TDR = tx_buf[tx_tail];
			tx_tail = (uint16_t)((tx_tail + 1) & (PORT_CONSOLE_TX_BUF - 1));
		}
		else {
			USART2->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
		}
	}
	__enable_irq();
#endif
}

int hal_console_getc(void) {
	int c;

	if (rx_tail == rx_head) {
		return -1;
	}
	c = rx_buf[rx_tail];
	rx_tail = (uint16_t)((rx_tail + 1) & (PORT_CONSOLE_RX_BUF - 1));
	return c;
}

static void wait_flag(uint32_t flag) {
	uint32_t spin = SPIN_MAX;

	while (!(USART2->ISR & flag) && --spin) {
	}
}

#if defined(AK_BOOTLOADER)
void hal_console_putc(uint8_t c) {
	wait_flag(USART_ISR_TXE_TXFNF);
	USART2->TDR = c;
}

void hal_console_flush(void) {
	wait_flag(USART_ISR_TC);
}
#else
/* Send the oldest queued byte without the interrupt. Interrupts masked. */
static void tx_drain_one(void) {
	wait_flag(USART_ISR_TXE_TXFNF);
	USART2->TDR = tx_buf[tx_tail];
	tx_tail = (uint16_t)((tx_tail + 1) & (PORT_CONSOLE_TX_BUF - 1));
}

/* Never drops a byte: ring full, a task waits for the TXE interrupt to make
 * room; with interrupts masked or inside an ISR the oldest byte is sent
 * polled instead, so it works in every context. */
void hal_console_putc(uint8_t c) {
	uint32_t primask = __get_PRIMASK();
	uint16_t next;

	for (;;) {
		__disable_irq();
		next = (uint16_t)((tx_head + 1) & (PORT_CONSOLE_TX_BUF - 1));
		if (next != tx_tail) {
			break;
		}
		if (primask == 0 && (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) == 0) {
			__enable_irq();		/* let the TXE interrupt drain */
		}
		else {
			tx_drain_one();
		}
	}

	tx_buf[tx_head] = c;
	tx_head = next;
	USART2->CR1 |= USART_CR1_TXEIE_TXFNFIE;
	__set_PRIMASK(primask);
}

/* Called before reset / in FATAL: push everything out polled. */
void hal_console_flush(void) {
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	while (tx_tail != tx_head) {
		tx_drain_one();
	}
	wait_flag(USART_ISR_TC);
	__set_PRIMASK(primask);
}
#endif

static void console_init(void) {
	RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
	RCC->APBENR1 |= RCC_APBENR1_USART2EN;

	/* PA2, PA3: alternate function 1, RX pulled up (idles high when nothing drives it) */
	GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(GPIO_AFRL_AFSEL2_Msk | GPIO_AFRL_AFSEL3_Msk)) |
					(1U << GPIO_AFRL_AFSEL2_Pos) | (1U << GPIO_AFRL_AFSEL3_Pos);
	GPIOA->PUPDR = (GPIOA->PUPDR & ~GPIO_PUPDR_PUPD3_Msk) | (1U << GPIO_PUPDR_PUPD3_Pos);
	GPIOA->MODER = (GPIOA->MODER & ~(GPIO_MODER_MODE2_Msk | GPIO_MODER_MODE3_Msk)) |
				   (2U << GPIO_MODER_MODE2_Pos) | (2U << GPIO_MODER_MODE3_Pos);

	USART2->CR1 = 0;
	USART2->BRR = (SystemCoreClock + PORT_CONSOLE_BAUD / 2U) / PORT_CONSOLE_BAUD;	/* 16x oversampling */
	USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE;
	USART2->CR1 |= USART_CR1_UE;

	NVIC_SetPriority(USART2_IRQn, 2);
	NVIC_EnableIRQ(USART2_IRQn);
}

/*----------------------------------------------------------------------------
 * system
 *--------------------------------------------------------------------------*/
#define PORT_TASK_IDLE		(0xEF)		/* AK_TASK_IDLE_ID */
#if !defined(AK_BOOTLOADER)
static uint8_t prev_task = PORT_TASK_IDLE;
static uint8_t prev_sig;
#endif

static void port_xputc(uint8_t c) {
	hal_console_putc(c);
}

void hal_init(void) {
	SystemCoreClockUpdate();

	/* LED: push-pull output */
	RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
	hal_led_set(0);						/* the level for "off" before the pin drives */
	GPIOA->MODER = (GPIOA->MODER & ~(3U << (PORT_LED_PIN * 2))) | (1U << (PORT_LED_PIN * 2));

	console_init();
	xfunc_out = port_xputc;

	/* SysTick 1 ms, lower priority than UART (2 priority bits: 0..3) */
	SysTick_Config(SystemCoreClock / 1000U);
	NVIC_SetPriority(SysTick_IRQn, 3);
	__enable_irq();

#if !defined(AK_BOOTLOADER)
	/* what the previous run was doing; the bootloader leaves it for the app */
	if (port_noinit.run_magic == PORT_RUN_MAGIC) {
		prev_task = (uint8_t)(port_noinit.run_task_sig >> 8);
		prev_sig = (uint8_t)port_noinit.run_task_sig;
	}
	port_noinit.run_task_sig = (uint32_t)PORT_TASK_IDLE << 8;
	port_noinit.run_magic = PORT_RUN_MAGIC;
#endif
}

/*----------------------------------------------------------------------------
 * crash facts of the previous run (.noinit RAM survives the reset)
 *--------------------------------------------------------------------------*/
void ak_port_note_dispatch(uint8_t task_id, uint8_t sig) {
	port_noinit.run_task_sig = ((uint32_t)task_id << 8) | sig;
}

void hal_last_dispatch(uint8_t* task_id, uint8_t* sig) {
#if !defined(AK_BOOTLOADER)
	*task_id = prev_task;
	*sig = prev_sig;
#else
	*task_id = PORT_TASK_IDLE;
	*sig = 0;
#endif
}

uint8_t hal_crash_take(hal_crash_t* out) {
	memset(out, 0, sizeof(*out));

	if (port_noinit.fault_magic == PORT_FAULT_MAGIC) {
		port_noinit.fault_magic = 0;
		port_noinit.fatal_magic = 0;
		out->kind = HAL_CRASH_HARDFAULT;
		out->pc = port_noinit.fault_pc;
		out->lr = port_noinit.fault_lr;
		out->info = port_noinit.fault_cfsr;
		return 1;
	}
	if (port_noinit.fatal_magic == PORT_FATAL_MAGIC) {
		port_noinit.fatal_magic = 0;
		out->kind = HAL_CRASH_FATAL;
		out->code = (uint8_t)port_noinit.fatal_code;
		out->info = port_noinit.fatal_tag;
		return 1;
	}
	return 0;
}

uint8_t hal_reset_reason(void) {
	return (port_noinit.reason_magic == PORT_REASON_MAGIC) ? (uint8_t)port_noinit.reset_reason
														   : HAL_RESET_REASON_UNKNOWN;
}

void hal_reset(void) {
	hal_console_flush();
	NVIC_SystemReset();
	for (;;) {
	}
}

const char* hal_board_name(void) {
	return PORT_BOARD_NAME;
}

uint32_t hal_chip_id(void) {
	return 0;	/* not read on this port */
}

void hal_jump_to_app(uint32_t vector_addr) {
	port_noinit.jump_addr = vector_addr;
	port_noinit.jump_magic = PORT_JUMP_MAGIC;
	hal_reset();
}

uint8_t hal_vector_ok(uint32_t initial_sp, uint32_t reset_handler) {
	return initial_sp > PORT_RAM_START && initial_sp <= PORT_RAM_END && (initial_sp & 3U) == 0 &&
		   (reset_handler & 1U) &&
		   reset_handler > PORT_APP_ADDR + 256U && reset_handler < PORT_APP_ADDR + PORT_APP_SIZE;
}

void ak_port_fatal(const char* s, uint8_t c) {
	__disable_irq();
	port_noinit.fatal_tag = 0;
	strncpy((char*)&port_noinit.fatal_tag, s, 4);
	port_noinit.fatal_code = c;
	port_noinit.fatal_magic = PORT_FATAL_MAGIC;
	xprintf("\n[FATAL] %s 0x%02X\n", s, c);
	hal_console_flush();
	/* give time to read the log, then reset */
	for (volatile uint32_t i = 0; i < 1000000UL; i++) {
	}
	NVIC_SystemReset();
	for (;;) {
	}
}

/* HardFault: save PC/LR in .noinit, report on next boot, reset. The M0+ has
 * no fault status register: info stays 0.
 * used: only referenced from the asm below, LTO would drop it. */
__attribute__((used)) void hard_fault_c(uint32_t* frame) {
	port_noinit.fault_pc = frame[6];
	port_noinit.fault_lr = frame[5];
	port_noinit.fault_cfsr = 0;
	port_noinit.fault_magic = PORT_FAULT_MAGIC;
	NVIC_SystemReset();
	for (;;) {
	}
}

/* Thumb-1 only: no IT block, no "tst lr, #imm"; the C function may sit
 * anywhere in flash, so it is reached through a register (bx), not a branch */
__attribute__((naked)) void hard_fault_handler(void) {
	__asm volatile (
		"movs r0, #4		\n"
		"mov  r1, lr		\n"
		"tst  r0, r1		\n"
		"beq  1f			\n"
		"mrs  r0, psp		\n"
		"b    2f			\n"
		"1:					\n"
		"mrs  r0, msp		\n"
		"2:					\n"
		"ldr  r2, =hard_fault_c	\n"
		"bx   r2			\n"
		".ltorg				\n"	/* literal pool right here: ldr reaches 1 KB only */
	);
}

/*----------------------------------------------------------------------------
 * LED / IWDG
 *--------------------------------------------------------------------------*/
void hal_led_set(uint8_t on) {
	if (PORT_LED_ACTIVE_LOW ? !on : on) {
		GPIOA->BSRR = (1U << PORT_LED_PIN);
	}
	else {
		GPIOA->BRR = (1U << PORT_LED_PIN);
	}
}

void hal_led_toggle(void) {
	GPIOA->ODR ^= (1U << PORT_LED_PIN);
}

void hal_wdt_start(uint32_t timeout_ms) {
	/* LSI 32 kHz / 256 = 125 Hz; max reload 4095 (~32 s) */
	uint32_t reload = (timeout_ms * 125U) / 1000U;
	uint32_t spin = SPIN_MAX;

	if (reload > 0xFFF) {
		reload = 0xFFF;
	}
	if (reload == 0) {
		reload = 1;
	}
	IWDG->KR = 0xCCCC;				/* start (the LSI is switched on by the watchdog) */
	IWDG->KR = 0x5555;				/* write access to PR / RLR */
	IWDG->PR = 6;					/* /256 */
	IWDG->RLR = reload;
	while ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) && --spin) {
	}
	IWDG->KR = 0xAAAA;
}

void hal_wdt_kick(void) {
	IWDG->KR = 0xAAAA;
}

/*----------------------------------------------------------------------------
 * flash programming (RM0444: unlock, page erase, double-word program)
 *--------------------------------------------------------------------------*/
#define FLASH_SR_ERR_MASK	(FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
							 FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR | \
							 FLASH_SR_RDERR | FLASH_SR_OPTVERR)

static int flash_wait(void) {
	uint32_t spin = SPIN_MAX * 10U;
	uint32_t err;

	while ((FLASH->SR & (FLASH_SR_BSY1 | FLASH_SR_CFGBSY)) && --spin) {
	}
	err = FLASH->SR & FLASH_SR_ERR_MASK;
	FLASH->SR = err | FLASH_SR_EOP;			/* write-1-to-clear */
	return (spin == 0 || err) ? -1 : 0;
}

static void flash_unlock(void) {
	if (FLASH->CR & FLASH_CR_LOCK) {
		FLASH->KEYR = 0x45670123UL;
		FLASH->KEYR = 0xCDEF89ABUL;
	}
	FLASH->SR = FLASH_SR_ERR_MASK | FLASH_SR_EOP;
}

static void flash_lock(void) {
	FLASH->CR |= FLASH_CR_LOCK;
}

static int flash_erase_page(uint32_t addr) {
	uint32_t page = (addr - PORT_BOOT_ADDR) / PORT_FLASH_PAGE;
	int ret;

	if (flash_wait() != 0) {
		return -1;
	}
	FLASH->CR = (FLASH->CR & ~FLASH_CR_PNB_Msk) | (page << FLASH_CR_PNB_Pos) | FLASH_CR_PER;
	FLASH->CR |= FLASH_CR_STRT;
	ret = flash_wait();
	FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_PNB_Msk);
	return ret;
}

/* one double word; src need not be aligned */
static int flash_program_dword(uint32_t addr, const uint8_t* src) {
	uint32_t lo, hi;
	int ret;

	memcpy(&lo, src, 4);
	memcpy(&hi, src + 4, 4);
	FLASH->CR |= FLASH_CR_PG;
	*(volatile uint32_t*)addr = lo;
	__ISB();
	*(volatile uint32_t*)(addr + 4) = hi;
	ret = flash_wait();
	FLASH->CR &= ~FLASH_CR_PG;
	if (ret != 0 || *(volatile uint32_t*)addr != lo || *(volatile uint32_t*)(addr + 4) != hi) {
		return -1;
	}
	return 0;
}

static int flash_program(uint32_t addr, const uint8_t* src, uint32_t len) {
	for (; len; len -= PORT_FLASH_WRITE, addr += PORT_FLASH_WRITE, src += PORT_FLASH_WRITE) {
		if (flash_program_dword(addr, src) != 0) {
			return -1;
		}
	}
	return 0;
}

/*----------------------------------------------------------------------------
 * NVM = two flash pages, [seq][~seq][256 B], the newer valid one counts
 *--------------------------------------------------------------------------*/
#define NVM_PAGES			(2U)
#define NVM_HDR				(8U)
#define NVM_REC				(NVM_HDR + HAL_NVM_SIZE)

typedef char nvm_rec_check[(NVM_REC % PORT_FLASH_WRITE == 0 && NVM_REC <= PORT_FLASH_PAGE) ? 1 : -1];

static uint32_t nvm_page_addr(uint32_t i) {
	return PORT_NVM_ADDR + i * PORT_FLASH_PAGE;
}

static uint8_t nvm_page_valid(uint32_t i, uint32_t* seq) {
	const volatile uint32_t* p = (const volatile uint32_t*)nvm_page_addr(i);

	if (p[0] == 0xFFFFFFFFUL || p[0] != ~p[1]) {
		return 0;
	}
	*seq = p[0];
	return 1;
}

/* index of the page holding the NVM, NVM_PAGES if there is none */
static uint32_t nvm_current(uint32_t* seq_out) {
	uint32_t best = NVM_PAGES, best_seq = 0, seq;

	for (uint32_t i = 0; i < NVM_PAGES; i++) {
		if (nvm_page_valid(i, &seq) && (best == NVM_PAGES || (int32_t)(seq - best_seq) > 0)) {
			best = i;
			best_seq = seq;
		}
	}
	*seq_out = best_seq;
	return best;
}

int hal_nvm_read(uint32_t offset, void* buf, uint32_t len) {
	uint32_t seq;
	uint32_t cur;

	if (offset + len > HAL_NVM_SIZE) {
		return -1;
	}
	cur = nvm_current(&seq);
	if (cur == NVM_PAGES) {
		memset(buf, 0, len);		/* blank NVM reads as zeros, like the L1 EEPROM */
	}
	else {
		memcpy(buf, (const void*)(nvm_page_addr(cur) + NVM_HDR + offset), len);
	}
	return 0;
}

int hal_nvm_write(uint32_t offset, const void* buf, uint32_t len) {
	uint32_t rec[NVM_REC / 4];		/* header + data, double-word aligned */
	uint8_t* data = (uint8_t*)&rec[2];
	uint32_t seq, cur, next;
	int ret = -1;

	if (offset + len > HAL_NVM_SIZE) {
		return -1;
	}
	cur = nvm_current(&seq);
	if (cur == NVM_PAGES) {
		memset(data, 0, HAL_NVM_SIZE);
		seq = 0;
		next = 0;
	}
	else {
		memcpy(data, (const void*)(nvm_page_addr(cur) + NVM_HDR), HAL_NVM_SIZE);
		if (memcmp(data + offset, buf, len) == 0) {
			return 0;				/* nothing changes: spare the page */
		}
		next = (cur + 1) % NVM_PAGES;
	}
	memcpy(data + offset, buf, len);
	rec[0] = seq + 1;
	rec[1] = ~(seq + 1);

	flash_unlock();
	if (flash_erase_page(nvm_page_addr(next)) == 0 &&
			flash_program(nvm_page_addr(next), (const uint8_t*)rec, NVM_REC) == 0) {
		ret = 0;
	}
	flash_lock();
	return ret;
}

/*----------------------------------------------------------------------------
 * internal flash partitions
 *--------------------------------------------------------------------------*/
static const flash_part_info_t parts[FLASH_PART_NUM] = {
	{ PORT_BOOT_ADDR,		PORT_BOOT_SIZE,		PORT_FLASH_PAGE, PORT_FLASH_WRITE, 0xFF },
	{ PORT_APP_ADDR,		PORT_APP_SIZE,		PORT_FLASH_PAGE, PORT_FLASH_WRITE, 0xFF },
	{ PORT_STAGING_ADDR,	PORT_STAGING_SIZE,	PORT_FLASH_PAGE, PORT_FLASH_WRITE, 0xFF },
	{ PORT_BACKUP_ADDR,		PORT_BACKUP_SIZE,	PORT_FLASH_PAGE, PORT_FLASH_WRITE, 0xFF },	/* size 0 */
};

const flash_part_info_t* hal_flash_info(flash_part_t part) {
	return (part < FLASH_PART_NUM) ? &parts[part] : 0;
}

static int range_ok(flash_part_t part, uint32_t off, uint32_t len, uint32_t align) {
	const flash_part_info_t* p = hal_flash_info(part);
	return p && off <= p->size && len <= p->size - off && (off % align) == 0 && (len % align) == 0;
}

/* BOOT is never writable. The app may only write STAGING (a bug cannot
 * brick the running image); only the bootloader writes APP. */
static int part_writable(flash_part_t part) {
#if defined(AK_BOOTLOADER)
	return part == FLASH_PART_APP || part == FLASH_PART_STAGING;
#else
	return part == FLASH_PART_STAGING;
#endif
}

int hal_flash_erase(flash_part_t part, uint32_t off, uint32_t len) {
	int ret = HAL_FLASH_OK;

	if (!part_writable(part) || !range_ok(part, off, len, parts[part].erase_size)) {
		return HAL_FLASH_ERR_ARG;
	}
	flash_unlock();
	for (uint32_t a = parts[part].addr + off; a < parts[part].addr + off + len; a += PORT_FLASH_PAGE) {
		if (flash_erase_page(a) != 0) {
			ret = HAL_FLASH_ERR_HW;
			break;
		}
	}
	flash_lock();
	return ret;
}

int hal_flash_write(flash_part_t part, uint32_t off, const void* data, uint32_t len) {
	int ret;

	if (!part_writable(part) || !range_ok(part, off, len, parts[part].write_size)) {
		return HAL_FLASH_ERR_ARG;
	}
	flash_unlock();
	ret = flash_program(parts[part].addr + off, (const uint8_t*)data, len) == 0 ? HAL_FLASH_OK : HAL_FLASH_ERR_HW;
	flash_lock();
	return ret;
}

int hal_flash_read(flash_part_t part, uint32_t off, void* buf, uint32_t len) {
	if (!range_ok(part, off, len, 1)) {
		return HAL_FLASH_ERR_ARG;
	}
	memcpy(buf, (const void*)(parts[part].addr + off), len);
	return HAL_FLASH_OK;
}

/*----------------------------------------------------------------------------
 * newlib: heap for malloc (dynamic messages), bounded
 *--------------------------------------------------------------------------*/
extern uint8_t _heap_start, _heap_end;

static uint8_t* heap_brk = &_heap_start;

__attribute__((used)) void* _sbrk(ptrdiff_t incr) {
	uint8_t* prev = heap_brk;

	if (heap_brk + incr > &_heap_end) {
		errno = ENOMEM;
		return (void*)-1;
	}
	heap_brk += incr;
	return prev;
}

/* reset_handler filled the RAM above the heap start with PORT_STACK_FILL:
 * count what is still untouched above the current heap top. */
uint32_t hal_stack_unused(void) {
	const uint32_t* p = (const uint32_t*)(((uint32_t)heap_brk + 3U) & ~3U);
	const uint32_t* end = (const uint32_t*)__get_MSP();
	uint32_t n = 0;

	while (p < end && *p == PORT_STACK_FILL) {
		p++;
		n += 4;
	}
	return n;
}
