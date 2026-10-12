#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "ak_port.h"
#include "hal.h"
#include "hal_rs485.h"
#include "hal_tcp.h"
#include "port_host.h"

/*----------------------------------------------------------------------------
 * Partitions: same maps as port stm32l151
 *   internal staging: boot 12K | app 58K | staging 58K (all internal flash)
 *   external staging: boot 12K | app 116K, staging 116K + backup 116K on
 *                     emulated SPI NOR (4K sectors, byte writes, erased 0xFF)
 *--------------------------------------------------------------------------*/
static const flash_part_info_t parts_internal[FLASH_PART_NUM] = {
	/* addr                           size        erase           write  erased */
	{ HOST_FLASH_BASE + 0x00000UL,   0x03000UL,  HOST_PAGE_SIZE, 4,     0xFF },
	{ HOST_FLASH_BASE + 0x03000UL,   0x0E800UL,  HOST_PAGE_SIZE, 4,     0xFF },
	{ HOST_FLASH_BASE + 0x11800UL,   0x0E800UL,  HOST_PAGE_SIZE, 4,     0xFF },
	{ 0,                             0,          HOST_PAGE_SIZE, 4,     0xFF },	/* no backup: no rollback */
};
static const flash_part_info_t parts_external[FLASH_PART_NUM] = {
	{ HOST_FLASH_BASE + 0x00000UL,   0x03000UL,  HOST_PAGE_SIZE, 4,     0xFF },
	{ HOST_FLASH_BASE + 0x03000UL,   0x1D000UL,  HOST_PAGE_SIZE, 4,     0xFF },
	{ HOST_EXT_STAGING_ADDR,         HOST_EXT_STAGING_SIZE, HOST_EXT_SECTOR, 1, 0xFF },
	{ HOST_EXT_BACKUP_ADDR,          HOST_EXT_BACKUP_SIZE,  HOST_EXT_SECTOR, 1, 0xFF },
};
static flash_part_info_t parts_rt[FLASH_PART_NUM];
static uint8_t layout_external = 1;

static uint8_t flash_mem[HOST_FLASH_SIZE];
static uint8_t ext_mem[HOST_EXT_MEM_SIZE];
static uint8_t nvm_mem[HAL_NVM_SIZE];

static uint32_t flash_ops;
static uint32_t power_cut_at;
static jmp_buf* power_cut_jmp;

jmp_buf* host_fatal_jmp;
const char* host_fatal_str;
uint8_t host_fatal_code;

void (*host_reset_handler)(void);
void (*host_jump_handler)(uint32_t vector_addr);

static uint8_t virtual_time;
static uint32_t virtual_ms;
static uint32_t last_service_ms;

static uint8_t use_stdio;
static uint8_t rx_q[8192];
static uint32_t rx_head, rx_tail;
static uint8_t tx_q[65536];
static uint32_t tx_len;

static uint32_t crit_nest;

static uint8_t reset_reason = HAL_RESET_REASON_SOFTWARE;
static hal_crash_t crash_prev;
static uint8_t prev_task = 0xEF, prev_sig;
static uint8_t cur_task = 0xEF, cur_sig;

static void host_xputc(uint8_t c) {
	hal_console_putc(c);
}

static void tcp_reset_state(void);

void host_reset_state(uint8_t erased_val) {
	memcpy(parts_rt, layout_external ? parts_external : parts_internal, sizeof(parts_rt));
	for (int i = 0; i < FLASH_PART_NUM; i++) {
		/* erased_val models the MCU flash; SPI NOR always erases to 0xFF */
		if (!(layout_external && (i == FLASH_PART_STAGING || i == FLASH_PART_BACKUP))) {
			parts_rt[i].erased_val = erased_val;
		}
	}
	memset(flash_mem, erased_val, sizeof(flash_mem));
	memset(ext_mem, 0xFF, sizeof(ext_mem));
	memset(nvm_mem, 0, sizeof(nvm_mem));
	flash_ops = 0;
	power_cut_at = 0;
	power_cut_jmp = 0;
	rx_head = rx_tail = 0;
	tx_len = 0;
	tcp_reset_state();
	crit_nest = 0;
	virtual_ms = 0;
	last_service_ms = 0;
	reset_reason = HAL_RESET_REASON_SOFTWARE;
	memset(&crash_prev, 0, sizeof(crash_prev));
	prev_task = cur_task = 0xEF;
	prev_sig = cur_sig = 0;
}

/*----------------------------------------------------------------------------
 * ak_port
 *--------------------------------------------------------------------------*/
void ak_port_enter_critical(void) {
	crit_nest++;
}

void ak_port_exit_critical(void) {
	if (crit_nest == 0) {
		ak_port_fatal("CRIT", 0x01);
	}
	crit_nest--;
}

uint32_t ak_port_millis(void) {
	return hal_millis();
}

void ak_port_fatal(const char* s, uint8_t c) {
	host_fatal_str = s;
	host_fatal_code = c;
	crit_nest = 0;
	if (host_fatal_jmp) {
		longjmp(*host_fatal_jmp, 1);
	}
	fprintf(stderr, "\n[FATAL] %s 0x%02X\n", s, c);
	exit(1);
}

void ak_port_idle(void) {
	host_service();
	if (!virtual_time) {
		usleep(1000);
	}
}

__attribute__((weak)) void hal_tick_hook(uint32_t elapsed_ms) {
	(void)elapsed_ms;
}

void host_service(void) {
	uint32_t now = hal_millis();
	uint32_t elapsed = now - last_service_ms;

	if (elapsed) {
		last_service_ms = now;
		hal_tick_hook(elapsed);
	}
}

/*----------------------------------------------------------------------------
 * he thong
 *--------------------------------------------------------------------------*/
void hal_init(void) {
	if (parts_rt[FLASH_PART_APP].size == 0) {
		host_reset_state(0xFF);
	}
	xfunc_out = host_xputc;
	last_service_ms = hal_millis();
}

void host_use_virtual_time(uint8_t on) {
	virtual_time = on;
}

void host_advance_ms(uint32_t ms) {
	virtual_ms += ms;
}

uint32_t hal_millis(void) {
	struct timespec ts;
	static uint64_t t0;
	uint64_t now;

	if (virtual_time) {
		return virtual_ms;
	}
	clock_gettime(CLOCK_MONOTONIC, &ts);
	now = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
	if (t0 == 0) {
		t0 = now;
	}
	return (uint32_t)(now - t0);
}

void hal_delay_ms(uint32_t ms) {
	if (virtual_time) {
		virtual_ms += ms;
	}
	else {
		usleep(ms * 1000U);
	}
}

uint8_t hal_reset_reason(void) {
	return reset_reason;
}

void host_set_reset_reason(uint8_t reason) {
	reset_reason = reason;
}

void host_crash_inject(uint8_t kind, uint8_t code, uint32_t pc, uint32_t lr, uint32_t info) {
	crash_prev.kind = kind;
	crash_prev.code = code;
	crash_prev.pc = pc;
	crash_prev.lr = lr;
	crash_prev.info = info;
}

void host_set_last_dispatch(uint8_t task_id, uint8_t sig) {
	prev_task = task_id;
	prev_sig = sig;
}

uint8_t hal_crash_take(hal_crash_t* out) {
	*out = crash_prev;
	crash_prev.kind = HAL_CRASH_NONE;
	return out->kind != HAL_CRASH_NONE;
}

void hal_last_dispatch(uint8_t* task_id, uint8_t* sig) {
	*task_id = prev_task;
	*sig = prev_sig;
}

void ak_port_note_dispatch(uint8_t task_id, uint8_t sig) {
	cur_task = task_id;
	cur_sig = sig;
}

uint8_t host_cur_task(void) {
	return cur_task;
}

uint8_t host_cur_sig(void) {
	return cur_sig;
}

uint32_t hal_stack_unused(void) {
	return 0;
}

/*----------------------------------------------------------------------------
 * RS485 (in-memory queues)
 *--------------------------------------------------------------------------*/
static uint8_t rs_rx[2048];
static uint32_t rs_rx_head, rs_rx_tail;
static uint8_t rs_tx[2048];
static uint32_t rs_tx_len;
static uint32_t rs_baud;
static uint32_t rs_rx_bytes;

void hal_rs485_init(uint32_t baudrate) {
	rs_baud = baudrate;
	rs_rx_head = rs_rx_tail = 0;
	rs_tx_len = 0;
	rs_rx_bytes = 0;
}

uint32_t hal_rs485_rx_bytes(void) {
	return rs_rx_bytes;
}

int hal_rs485_getc(void) {
	if (rs_rx_tail == rs_rx_head) {
		return -1;
	}
	return rs_rx[rs_rx_tail++ % sizeof(rs_rx)];
}

void hal_rs485_write(const uint8_t* data, uint32_t len) {
	while (len-- && rs_tx_len < sizeof(rs_tx)) {
		rs_tx[rs_tx_len++] = *data++;
	}
}

void host_rs485_inject(const uint8_t* data, uint32_t len) {
	while (len--) {
		rs_rx[rs_rx_head++ % sizeof(rs_rx)] = *data++;
		rs_rx_bytes++;
	}
}

uint32_t host_rs485_take_tx(uint8_t* out, uint32_t max) {
	uint32_t n = rs_tx_len < max ? rs_tx_len : max;

	memcpy(out, rs_tx, n);
	memmove(rs_tx, rs_tx + n, rs_tx_len - n);
	rs_tx_len -= n;
	return n;
}

uint32_t host_rs485_baud(void) {
	return rs_baud;
}

/*----------------------------------------------------------------------------
 * TCP (in-memory queues, like the RS485 port)
 *--------------------------------------------------------------------------*/
static uint8_t tcp_present = 1;
static uint8_t tcp_inited, tcp_connected;
static uint16_t tcp_port;
static uint8_t tcp_rx[2048];
static uint32_t tcp_rx_head, tcp_rx_tail;
static uint8_t tcp_tx[2048];
static uint32_t tcp_tx_len;

void host_tcp_set_present(uint8_t present) {
	tcp_present = present;
}

static void tcp_reset_state(void) {
	tcp_inited = tcp_connected = 0;
	tcp_port = 0;
	tcp_rx_head = tcp_rx_tail = 0;
	tcp_tx_len = 0;
}

int hal_tcp_init(const hal_net_cfg_t* cfg) {
	(void)cfg;
	tcp_inited = tcp_present;
	tcp_connected = 0;
	tcp_port = 0;
	return tcp_present ? 0 : -1;
}

uint8_t hal_tcp_link_up(void) {
	return tcp_inited;
}

int hal_tcp_listen(uint16_t port) {
	if (!tcp_inited) {
		return -1;
	}
	tcp_port = port;
	return 0;
}

uint8_t hal_tcp_connected(void) {
	return tcp_connected;
}

int hal_tcp_read(uint8_t* buf, uint32_t max) {
	uint32_t n = 0;

	if (!tcp_connected) {
		return -1;
	}
	while (n < max && tcp_rx_tail != tcp_rx_head) {
		buf[n++] = tcp_rx[tcp_rx_tail++ % sizeof(tcp_rx)];
	}
	return (int)n;
}

int hal_tcp_write(const uint8_t* data, uint32_t len) {
	if (!tcp_connected) {
		return -1;
	}
	for (uint32_t i = 0; i < len && tcp_tx_len < sizeof(tcp_tx); i++) {
		tcp_tx[tcp_tx_len++] = data[i];
	}
	return (int)len;
}

void hal_tcp_close(void) {
	tcp_connected = 0;
	tcp_rx_head = tcp_rx_tail = 0;
	tcp_tx_len = 0;
}

uint16_t host_tcp_port(void) {
	return tcp_port;
}

int host_tcp_connect(void) {
	if (!tcp_port || tcp_connected) {
		return -1;
	}
	tcp_connected = 1;
	tcp_rx_head = tcp_rx_tail = 0;
	tcp_tx_len = 0;
	return 0;
}

void host_tcp_disconnect(void) {
	tcp_connected = 0;
}

void host_tcp_inject(const uint8_t* data, uint32_t len) {
	while (len--) {
		tcp_rx[tcp_rx_head++ % sizeof(tcp_rx)] = *data++;
	}
}

uint32_t host_tcp_take_tx(uint8_t* out, uint32_t max) {
	uint32_t n = tcp_tx_len < max ? tcp_tx_len : max;

	memcpy(out, tcp_tx, n);
	memmove(tcp_tx, tcp_tx + n, tcp_tx_len - n);
	tcp_tx_len -= n;
	return n;
}

void hal_reset(void) {
	hal_console_flush();
	if (host_reset_handler) {
		host_reset_handler();
	}
	fprintf(stderr, "[host] reset\n");
	exit(0);
}

const char* hal_board_name(void) {
	return HOST_BOARD_NAME;
}

void hal_jump_to_app(uint32_t vector_addr) {
	if (host_jump_handler) {
		host_jump_handler(vector_addr);
	}
	fprintf(stderr, "[host] jump to 0x%08X (no app in this build)\n", vector_addr);
	exit(0);
}

uint8_t hal_vector_ok(uint32_t initial_sp, uint32_t reset_handler) {
	const flash_part_info_t* app = &parts_rt[FLASH_PART_APP];

	return initial_sp > HOST_RAM_START && initial_sp <= HOST_RAM_END && (initial_sp & 3U) == 0 &&
		   (reset_handler & 1U) &&
		   reset_handler > app->addr && reset_handler < app->addr + app->size;
}

/*----------------------------------------------------------------------------
 * console
 *--------------------------------------------------------------------------*/
void host_console_use_stdio(uint8_t on) {
	use_stdio = on;
	if (on) {
		int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
		fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);
		setvbuf(stdout, NULL, _IONBF, 0);
	}
}

static uint8_t rx_guard = 1;

void host_console_rx_guard(uint8_t on) {
	rx_guard = on;
}

void host_console_inject(const uint8_t* data, uint32_t len) {
	while (len--) {
		rx_q[rx_head % sizeof(rx_q)] = *data++;
		rx_head++;
	}
}

uint32_t host_console_take_tx(uint8_t* out, uint32_t max) {
	uint32_t n = tx_len < max ? tx_len : max;

	if (out) {
		memcpy(out, tx_q, n);
	}
	memmove(tx_q, tx_q + n, tx_len - n);
	tx_len -= n;
	return n;
}

void hal_console_putc(uint8_t c) {
	if (use_stdio) {
		fputc(c, stdout);
	}
	else if (tx_len < sizeof(tx_q)) {
		tx_q[tx_len++] = c;
	}
}

int hal_console_getc(void) {
	if (use_stdio) {
		uint8_t c;
		ssize_t n = read(STDIN_FILENO, &c, 1);
		if (n == 1) {
			return c;
		}
		if (n == 0) {
			/* stdin closed (controller exited) */
			fprintf(stderr, "[host] stdin closed\n");
			exit(0);
		}
		return -1;
	}
	if (rx_tail == rx_head) {
		/* a test forgot the terminating command and the loader would spin
		 * forever: fail instead of hanging */
		static uint32_t empty_reads;
		if (rx_guard && ++empty_reads > 5000000UL) {
			empty_reads = 0;
			ak_port_fatal("HOSTRX", 0x01);
		}
		return -1;
	}
	return rx_q[(rx_tail++) % sizeof(rx_q)];
}

void hal_console_flush(void) {
	if (use_stdio) {
		fflush(stdout);
	}
}

/*----------------------------------------------------------------------------
 * LED / watchdog
 *--------------------------------------------------------------------------*/
static uint8_t led_state;

uint8_t host_led(void) {
	return led_state;
}

void hal_led_set(uint8_t on) {
	led_state = on ? 1 : 0;
}

void hal_led_toggle(void) {
	led_state ^= 1;
}

void hal_wdt_start(uint32_t timeout_ms) {
	(void)timeout_ms;
}

void hal_wdt_kick(void) {
}

/*----------------------------------------------------------------------------
 * NVM
 *--------------------------------------------------------------------------*/
uint8_t* host_nvm_mem(void) {
	return nvm_mem;
}

int hal_nvm_read(uint32_t offset, void* buf, uint32_t len) {
	if (offset + len > HAL_NVM_SIZE) {
		return -1;
	}
	memcpy(buf, nvm_mem + offset, len);
	return 0;
}

int hal_nvm_write(uint32_t offset, const void* buf, uint32_t len) {
	if (offset + len > HAL_NVM_SIZE) {
		return -1;
	}
	memcpy(nvm_mem + offset, buf, len);
	return 0;
}

/*----------------------------------------------------------------------------
 * flash
 *--------------------------------------------------------------------------*/
uint8_t* host_flash_mem(void) {
	return flash_mem;
}

void host_set_layout(uint8_t external_staging) {
	layout_external = external_staging ? 1 : 0;
	host_reset_state(0xFF);
}

uint8_t host_layout_external(void) {
	return layout_external;
}

uint32_t host_flash_ops(void) {
	return flash_ops;
}

void host_flash_power_cut(uint32_t n, jmp_buf* jmp) {
	power_cut_at = n ? flash_ops + n : 0;
	power_cut_jmp = jmp;
}

static void flash_op_tick(void) {
	flash_ops++;
	if (power_cut_at && flash_ops >= power_cut_at && power_cut_jmp) {
		jmp_buf* j = power_cut_jmp;
		power_cut_at = 0;
		power_cut_jmp = 0;
		crit_nest = 0;
		longjmp(*j, 1);
	}
}

const flash_part_info_t* hal_flash_info(flash_part_t part) {
	if (part >= FLASH_PART_NUM) {
		return 0;
	}
	if (parts_rt[FLASH_PART_APP].size == 0) {
		host_reset_state(0xFF);
	}
	return &parts_rt[part];
}

static uint8_t* part_ptr(flash_part_t part, uint32_t off, uint32_t len) {
	const flash_part_info_t* p = hal_flash_info(part);

	if (p == 0 || off > p->size || len > p->size - off) {
		return 0;
	}
	if (layout_external && (part == FLASH_PART_STAGING || part == FLASH_PART_BACKUP)) {
		return ext_mem + (p->addr - HOST_EXT_STAGING_ADDR) + off;
	}
	return flash_mem + (p->addr - HOST_FLASH_BASE) + off;
}

uint8_t* host_part_mem(flash_part_t part) {
	return part_ptr(part, 0, 0);
}

int hal_flash_erase(flash_part_t part, uint32_t off, uint32_t len) {
	const flash_part_info_t* p = hal_flash_info(part);
	uint8_t* m = part_ptr(part, off, len);

	if (m == 0 || (off % p->erase_size) || (len % p->erase_size)) {
		return HAL_FLASH_ERR_ARG;
	}
	for (uint32_t i = 0; i < len; i += p->erase_size) {
		flash_op_tick();
		memset(m + i, p->erased_val, p->erase_size);
	}
	return HAL_FLASH_OK;
}

int hal_flash_write(flash_part_t part, uint32_t off, const void* data, uint32_t len) {
	const flash_part_info_t* p = hal_flash_info(part);
	uint8_t* m = part_ptr(part, off, len);
	const uint8_t* d = (const uint8_t*)data;

	if (m == 0 || (off % p->write_size) || (len % p->write_size)) {
		return HAL_FLASH_ERR_ARG;
	}
	for (uint32_t i = 0; i < len; i += p->write_size) {
		flash_op_tick();
		/* real flash: programming a non-erased cell is an error -> catch early */
		for (uint32_t k = 0; k < p->write_size; k++) {
			if (m[i + k] != p->erased_val) {
				return HAL_FLASH_ERR_HW;
			}
		}
		memcpy(m + i, d + i, p->write_size);
	}
	return HAL_FLASH_OK;
}

int hal_flash_read(flash_part_t part, uint32_t off, void* buf, uint32_t len) {
	uint8_t* m = part_ptr(part, off, len);

	if (m == 0) {
		return HAL_FLASH_ERR_ARG;
	}
	memcpy(buf, m, len);
	return HAL_FLASH_OK;
}

int host_flash_load(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) {
		return -1;
	}
	size_t a = fread(flash_mem, 1, sizeof(flash_mem), f);
	size_t e = fread(ext_mem, 1, sizeof(ext_mem), f);
	size_t b = fread(nvm_mem, 1, sizeof(nvm_mem), f);
	fclose(f);
	return (a == sizeof(flash_mem) && e == sizeof(ext_mem) && b == sizeof(nvm_mem)) ? 0 : -1;
}

int host_flash_save(const char* path) {
	FILE* f = fopen(path, "wb");
	if (!f) {
		return -1;
	}
	fwrite(flash_mem, 1, sizeof(flash_mem), f);
	fwrite(ext_mem, 1, sizeof(ext_mem), f);
	fwrite(nvm_mem, 1, sizeof(nvm_mem), f);
	fclose(f);
	return 0;
}
