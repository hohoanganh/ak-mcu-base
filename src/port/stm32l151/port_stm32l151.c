/**
 ******************************************************************************
 * @brief:  ak_port.h + hal.h + hal_flash.h for STM32L151CB (SPL).
 ******************************************************************************
**/

#include <stddef.h>
#include <string.h>
#include <errno.h>

#include "stm32l1xx.h"
#include "stm32l1xx_conf.h"
#include "core_cm3.h"

#include "ak_port.h"
#include "hal.h"
#include "port_cfg.h"
#include "port_stm32.h"
#if PORT_STAGING_EXTERNAL
#include "spi_nor.h"
#endif

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
 * console USART1 (PA9/PA10): RX interrupt -> ring.
 * TX: app = ring + TXE interrupt (a log line does not hold the task for its
 * whole transmit time), bootloader = polled (single loop, smaller).
 *--------------------------------------------------------------------------*/
static volatile uint8_t rx_buf[PORT_CONSOLE_RX_BUF];
static volatile uint16_t rx_head, rx_tail;

#if !defined(AK_BOOTLOADER)
static volatile uint8_t tx_buf[PORT_CONSOLE_TX_BUF];
static volatile uint16_t tx_head, tx_tail;
#endif

void usart1_irq_handler(void) {
	if (USART1->SR & (USART_SR_RXNE | USART_SR_ORE)) {
		uint8_t c = (uint8_t)USART1->DR;	/* reading DR clears RXNE and ORE */
		uint16_t next = (uint16_t)((rx_head + 1) & (PORT_CONSOLE_RX_BUF - 1));
		if (next != rx_tail) {
			rx_buf[rx_head] = c;
			rx_head = next;
		}
	}

#if !defined(AK_BOOTLOADER)
	/* masked: hal_console_putc() from a higher priority ISR also touches
	 * the ring and CR1 */
	__disable_irq();
	if ((USART1->CR1 & USART_CR1_TXEIE) && (USART1->SR & USART_SR_TXE)) {
		if (tx_tail != tx_head) {
			USART1->DR = tx_buf[tx_tail];
			tx_tail = (uint16_t)((tx_tail + 1) & (PORT_CONSOLE_TX_BUF - 1));
		}
		else {
			USART1->CR1 &= (uint16_t)~USART_CR1_TXEIE;
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

#if defined(AK_BOOTLOADER)
void hal_console_putc(uint8_t c) {
	while (!(USART1->SR & USART_SR_TXE)) {
	}
	USART1->DR = c;
}

void hal_console_flush(void) {
	while (!(USART1->SR & USART_SR_TC)) {
	}
}
#else
/* Send the oldest queued byte without the interrupt. Interrupts masked. */
static void tx_drain_one(void) {
	while (!(USART1->SR & USART_SR_TXE)) {
	}
	USART1->DR = tx_buf[tx_tail];
	tx_tail = (uint16_t)((tx_tail + 1) & (PORT_CONSOLE_TX_BUF - 1));
}

/* Never drops a byte. Ring full: a task waits for the TXE interrupt to make
 * room; with interrupts masked or inside an ISR (FATAL, critical section)
 * the oldest byte is sent polled instead, so it works in every context. */
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
	USART1->CR1 |= USART_CR1_TXEIE;
	__set_PRIMASK(primask);
}

/* Called before reset / in FATAL: push everything out polled. */
void hal_console_flush(void) {
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	while (tx_tail != tx_head) {
		tx_drain_one();
	}
	while (!(USART1->SR & USART_SR_TC)) {
	}
	__set_PRIMASK(primask);
}
#endif

static void console_init(void) {
	GPIO_InitTypeDef gpio;
	USART_InitTypeDef uart;
	NVIC_InitTypeDef nvic;

	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

	GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);

	GPIO_StructInit(&gpio);
	gpio.GPIO_Mode = GPIO_Mode_AF;
	gpio.GPIO_Speed = GPIO_Speed_2MHz;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_PuPd = GPIO_PuPd_UP;
	gpio.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;
	GPIO_Init(GPIOA, &gpio);

	USART_StructInit(&uart);
	uart.USART_BaudRate = PORT_CONSOLE_BAUD;
	uart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART1, &uart);

	nvic.NVIC_IRQChannel = USART1_IRQn;
	nvic.NVIC_IRQChannelPreemptionPriority = 2;
	nvic.NVIC_IRQChannelSubPriority = 0;
	nvic.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvic);

	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
	USART_Cmd(USART1, ENABLE);
}

/*----------------------------------------------------------------------------
 * system
 *--------------------------------------------------------------------------*/
static void staging_init(void);

#define PORT_TASK_IDLE		(0xEF)		/* AK_TASK_IDLE_ID */
#if !defined(AK_BOOTLOADER)
static uint8_t prev_task = PORT_TASK_IDLE;
static uint8_t prev_sig;
#endif

static void port_xputc(uint8_t c) {
	hal_console_putc(c);
}

void hal_init(void) {
	GPIO_InitTypeDef gpio;

	SystemCoreClockUpdate();
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

	/* LED */
	RCC_AHBPeriphClockCmd(PORT_LED_CLK, ENABLE);
	GPIO_StructInit(&gpio);
	gpio.GPIO_Pin = PORT_LED_PIN;
	gpio.GPIO_Mode = GPIO_Mode_OUT;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
	gpio.GPIO_Speed = GPIO_Speed_400KHz;
	hal_led_set(0);						/* the level for "off" before the pin drives */
	GPIO_Init(PORT_LED_PORT, &gpio);

	console_init();
	xfunc_out = port_xputc;

	/* SysTick 1 ms, lower priority than UART */
	SysTick_Config(SystemCoreClock / 1000U);
	NVIC_SetPriority(SysTick_IRQn, 3);
	__enable_irq();

	staging_init();

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
	for (volatile uint32_t i = 0; i < 2000000UL; i++) {
	}
	NVIC_SystemReset();
	for (;;) {
	}
}

/* HardFault: save PC/LR in .noinit, report on next boot, reset.
 * used: only referenced from the asm below, LTO would drop it. */
__attribute__((used)) void hard_fault_c(uint32_t* frame) {
	port_noinit.fault_pc = frame[6];
	port_noinit.fault_lr = frame[5];
	port_noinit.fault_cfsr = SCB->CFSR;
	port_noinit.fault_magic = PORT_FAULT_MAGIC;
	NVIC_SystemReset();
	for (;;) {
	}
}

__attribute__((naked)) void hard_fault_handler(void) {
	__asm volatile (
		"tst lr, #4			\n"
		"ite eq				\n"
		"mrseq r0, msp		\n"
		"mrsne r0, psp		\n"
		"b hard_fault_c		\n"
	);
}

/*----------------------------------------------------------------------------
 * LED / IWDG
 *--------------------------------------------------------------------------*/
void hal_led_set(uint8_t on) {
	if (PORT_LED_ACTIVE_LOW ? !on : on) {
		GPIO_SetBits(PORT_LED_PORT, PORT_LED_PIN);
	}
	else {
		GPIO_ResetBits(PORT_LED_PORT, PORT_LED_PIN);
	}
}

void hal_led_toggle(void) {
	GPIO_ToggleBits(PORT_LED_PORT, PORT_LED_PIN);
}

void hal_wdt_start(uint32_t timeout_ms) {
	/* LSI ~37 kHz / 256 = ~144 Hz; max reload 4095 (~28 s) */
	uint32_t reload = (timeout_ms * 144U) / 1000U;

	if (reload > 0xFFF) {
		reload = 0xFFF;
	}
	if (reload == 0) {
		reload = 1;
	}
	IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
	IWDG_SetPrescaler(IWDG_Prescaler_256);
	IWDG_SetReload((uint16_t)reload);
	IWDG_ReloadCounter();
	IWDG_Enable();
}

void hal_wdt_kick(void) {
	IWDG_ReloadCounter();
}

/*----------------------------------------------------------------------------
 * NVM = data EEPROM (byte writes, only changed bytes -> endurance)
 *--------------------------------------------------------------------------*/
int hal_nvm_read(uint32_t offset, void* buf, uint32_t len) {
	if (offset + len > HAL_NVM_SIZE) {
		return -1;
	}
	memcpy(buf, (const void*)(PORT_EEPROM_ADDR + offset), len);
	return 0;
}

int hal_nvm_write(uint32_t offset, const void* buf, uint32_t len) {
	const uint8_t* d = (const uint8_t*)buf;
	int ret = 0;

	if (offset + len > HAL_NVM_SIZE) {
		return -1;
	}

	DATA_EEPROM_Unlock();
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
					FLASH_FLAG_SIZERR | FLASH_FLAG_OPTVERR);
	for (uint32_t i = 0; i < len; i++) {
		uint32_t a = PORT_EEPROM_ADDR + offset + i;
		if (*(volatile uint8_t*)a != d[i]) {
			if (DATA_EEPROM_ProgramByte(a, d[i]) != FLASH_COMPLETE) {
				ret = -2;
				break;
			}
		}
	}
	DATA_EEPROM_Lock();
	return ret;
}

/*----------------------------------------------------------------------------
 * internal flash (program memory)
 *--------------------------------------------------------------------------*/
static flash_part_info_t parts[FLASH_PART_NUM] = {
	{ PORT_BOOT_ADDR,		PORT_BOOT_SIZE,		PORT_FLASH_PAGE, 4, 0x00 },
	{ PORT_APP_ADDR,		PORT_APP_SIZE,		PORT_FLASH_PAGE, 4, 0x00 },
#if PORT_STAGING_EXTERNAL
	/* sizes stay 0 until the chip is detected (OTA then reports "too big") */
	{ PORT_STAGING_ADDR,	0,					SPI_NOR_SECTOR_SIZE, 1, 0xFF },
	{ PORT_BACKUP_ADDR,		0,					SPI_NOR_SECTOR_SIZE, 1, 0xFF },
#else
	{ PORT_STAGING_ADDR,	PORT_STAGING_SIZE,	PORT_FLASH_PAGE, 4, 0x00 },
	{ PORT_BACKUP_ADDR,		PORT_BACKUP_SIZE,	PORT_FLASH_PAGE, 4, 0x00 },	/* size 0: no rollback */
#endif
};

#if PORT_STAGING_EXTERNAL
static uint8_t part_is_external(flash_part_t part) {
	return part == FLASH_PART_STAGING || part == FLASH_PART_BACKUP;
}
#endif

/* External mode: detect the SPI NOR and enable STAGING (and BACKUP) only if
 * the chip is present and large enough. Internal mode: nothing to do. */
static void staging_init(void) {
#if PORT_STAGING_EXTERNAL
	uint32_t size = spi_nor_init();

	if (size >= PORT_STAGING_ADDR + PORT_STAGING_SIZE) {
		parts[FLASH_PART_STAGING].size = PORT_STAGING_SIZE;
	}
	else {
		xprintf("[port] SPI NOR not found (JEDEC 0x%06X), OTA staging disabled\n", spi_nor_jedec_id());
	}
	if (size >= PORT_BACKUP_ADDR + PORT_BACKUP_SIZE) {
		parts[FLASH_PART_BACKUP].size = PORT_BACKUP_SIZE;
	}
	else if (parts[FLASH_PART_STAGING].size) {
		xprintf("[port] SPI NOR too small for a backup image, no rollback\n");
	}
#endif
}

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
	return part == FLASH_PART_APP || part == FLASH_PART_STAGING || part == FLASH_PART_BACKUP;
#else
	return part == FLASH_PART_STAGING;
#endif
}

static void flash_unlock(void) {
	FLASH_Unlock();
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
					FLASH_FLAG_SIZERR | FLASH_FLAG_OPTVERR);
}

int hal_flash_erase(flash_part_t part, uint32_t off, uint32_t len) {
	int ret = HAL_FLASH_OK;

	if (!part_writable(part) || !range_ok(part, off, len, parts[part].erase_size)) {
		return HAL_FLASH_ERR_ARG;
	}

#if PORT_STAGING_EXTERNAL
	if (part_is_external(part)) {
		for (uint32_t a = parts[part].addr + off; a < parts[part].addr + off + len; a += SPI_NOR_SECTOR_SIZE) {
			if (spi_nor_erase_sector(a) != SPI_NOR_OK) {
				return HAL_FLASH_ERR_HW;
			}
		}
		return HAL_FLASH_OK;
	}
#endif

	flash_unlock();
	for (uint32_t a = parts[part].addr + off; a < parts[part].addr + off + len; a += PORT_FLASH_PAGE) {
		if (FLASH_ErasePage(a) != FLASH_COMPLETE) {
			ret = HAL_FLASH_ERR_HW;
			break;
		}
	}
	FLASH_Lock();
	return ret;
}

/* Half-page programming: 32 words in one tprog instead of 32 x tprog.
 * Constraints (RM0038 / SPL stm32l1xx_flash_ramfunc.c):
 *  - must execute from SRAM: no flash read of any kind during the write,
 *    so interrupts are disabled (vectors/ISRs live in flash) and the source
 *    buffer must be in RAM;
 *  - address aligned to 128 B, all 32 words inside the same half page.
 * Runs from .data (copied to RAM by reset_handler). long_call: flash -> RAM
 * is out of BL range. Touches only FLASH registers and its RAM arguments. */
#define HALF_PAGE_BYTES		(128U)
#define HALF_PAGE_WORDS		(HALF_PAGE_BYTES / 4U)
#define FLASH_SR_ERR_MASK	(FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR)

__attribute__((section(".ramfunc"), noinline, long_call))
static uint32_t ram_program_half_page(uint32_t addr, const uint32_t* buf) {
	volatile uint32_t* dst = (volatile uint32_t*)addr;
	uint32_t sr;

	while (FLASH->SR & FLASH_SR_BSY) {
	}

	FLASH->PECR |= FLASH_PECR_FPRG;
	FLASH->PECR |= FLASH_PECR_PROG;

	for (uint32_t i = 0; i < HALF_PAGE_WORDS; i++) {
		dst[i] = buf[i];
	}

	while (FLASH->SR & FLASH_SR_BSY) {
	}

	FLASH->PECR &= ~FLASH_PECR_PROG;
	FLASH->PECR &= ~FLASH_PECR_FPRG;

	sr = FLASH->SR;
	FLASH->SR = sr & FLASH_SR_ERR_MASK;		/* write-1-to-clear */
	return sr & FLASH_SR_ERR_MASK;
}

static int program_half_page(uint32_t addr, const uint8_t* src) {
	uint32_t buf[HALF_PAGE_WORDS];		/* RAM copy, also fixes alignment */
	uint32_t primask;
	uint32_t err;

	memcpy(buf, src, HALF_PAGE_BYTES);

	primask = __get_PRIMASK();
	__disable_irq();
	err = ram_program_half_page(addr, buf);
	__set_PRIMASK(primask);

	if (err || memcmp((const void*)addr, buf, HALF_PAGE_BYTES) != 0) {
		return HAL_FLASH_ERR_HW;
	}
	return HAL_FLASH_OK;
}

static int program_word(uint32_t addr, const uint8_t* src) {
	uint32_t w;

	memcpy(&w, src, 4);
	if (FLASH_FastProgramWord(addr, w) != FLASH_COMPLETE || *(volatile uint32_t*)addr != w) {
		return HAL_FLASH_ERR_HW;
	}
	return HAL_FLASH_OK;
}

/* Aligned 128 B blocks use half-page programming, the unaligned head/tail
 * falls back to single words. fw_update (128 B chunks) and boot_install
 * (128 B copy buffer) always hit the fast path. */
int hal_flash_write(flash_part_t part, uint32_t off, const void* data, uint32_t len) {
	const uint8_t* d = (const uint8_t*)data;
	uint32_t a;
	int ret = HAL_FLASH_OK;

	if (!part_writable(part) || !range_ok(part, off, len, parts[part].write_size)) {
		return HAL_FLASH_ERR_ARG;
	}

#if PORT_STAGING_EXTERNAL
	if (part_is_external(part)) {
		return spi_nor_write(parts[part].addr + off, data, len) == SPI_NOR_OK ? HAL_FLASH_OK : HAL_FLASH_ERR_HW;
	}
#endif

	a = parts[part].addr + off;
	flash_unlock();
	while (len && ret == HAL_FLASH_OK) {
		if ((a % HALF_PAGE_BYTES) == 0 && len >= HALF_PAGE_BYTES) {
			ret = program_half_page(a, d);
			a += HALF_PAGE_BYTES;
			d += HALF_PAGE_BYTES;
			len -= HALF_PAGE_BYTES;
		}
		else {
			ret = program_word(a, d);
			a += 4;
			d += 4;
			len -= 4;
		}
	}
	FLASH_Lock();
	return ret;
}

int hal_flash_read(flash_part_t part, uint32_t off, void* buf, uint32_t len) {
	if (!range_ok(part, off, len, 1)) {
		return HAL_FLASH_ERR_ARG;
	}
#if PORT_STAGING_EXTERNAL
	if (part_is_external(part)) {
		return spi_nor_read(parts[part].addr + off, buf, len) == SPI_NOR_OK ? HAL_FLASH_OK : HAL_FLASH_ERR_HW;
	}
#endif
	memcpy(buf, (const void*)(parts[part].addr + off), len);
	return HAL_FLASH_OK;
}

/*----------------------------------------------------------------------------
 * newlib: heap for malloc (dynamic messages), bounded
 *--------------------------------------------------------------------------*/
extern uint8_t _heap_start, _heap_end;

static uint8_t* heap_brk = &_heap_start;

/* used: only referenced from libc (not an LTO object); without it LTO may
 * drop the function before the library asks for it */
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
