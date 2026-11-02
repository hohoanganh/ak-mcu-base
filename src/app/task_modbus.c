/**
 ******************************************************************************
 * @brief:  Modbus on the RS485 port. The role is chosen at build time:
 *
 *   APP_MODBUS_SLAVE   the board answers a register map and takes firmware
 *                      updates over RS485 (block 0xF000, tools/ak_mb.py)
 *   APP_MODBUS_MASTER  the board asks other devices; shell: mb read / mb write
 *                      (the answer is printed from a later message: the
 *                      master never waits inside a handler)
 *   APP_MODBUS_GATEWAY Modbus TCP server (W5500 on J6, hal_tcp) forwarding to
 *                      the RS485 slaves as RTU master; shell as the master
 *
 *  Slave register map of this sample (holding, FC 03 / 06 / 16):
 *    0  R  app version, (major << 8) | minor
 *    1  R  app version, patch
 *    2  R  uptime in seconds (wraps at 65535)
 *    3  R  crash log: number of records
 *    16..19  R/W  scratch registers, kept until reset
 *    32  R/W  this unit's address (1..247)      } saved to NVM, in force at
 *    33  R/W  RS485 baud rate / 100 (96 = 9600) } the next reset (app_cfg)
 ******************************************************************************
**/

#include <string.h>

#include "ak.h"
#include "task.h"

#include "hal.h"
#include "ak_log.h"
#include "crash_log.h"

#include "app.h"
#include "task_list.h"

#if (defined(APP_MODBUS_SLAVE) + defined(APP_MODBUS_MASTER) + defined(APP_MODBUS_GATEWAY)) > 1
#error "one RS485 port: build with one of APP_MODBUS_SLAVE, APP_MODBUS_MASTER, APP_MODBUS_GATEWAY"
#endif

#define TAG "MB"

/*----------------------------------------------------------------------------
 * slave
 *--------------------------------------------------------------------------*/
#if defined(APP_MODBUS_SLAVE)

#include "mb_slave.h"
#include "hal_rs485.h"
#if defined(APP_KIT_DEMO)
#include "plot.h"
#endif

static uint16_t info_regs[4];
static uint16_t user_regs[4];
static uint16_t cfg_regs[2];
/* what the port runs with (app_cfg may already hold a newer, saved value) */
static uint8_t run_unit;
static uint32_t run_baud;

static const mb_reg_block_t holding_blocks[] = {
	{ 0,  4, info_regs, MB_REG_RO },
	{ 16, 4, user_regs, MB_REG_RW },
	{ 32, 2, cfg_regs,  MB_REG_RW },
};

static void cfg_regs_show(void) {
	cfg_regs[0] = app_cfg.mb_unit;
	cfg_regs[1] = (uint16_t)(app_cfg.mb_baud / 100U);
}

/* the master wrote the settings block: keep it if it makes sense */
static void cfg_regs_written(void) {
	app_cfg_t c = app_cfg;

	c.mb_unit = (uint8_t)cfg_regs[0];
	c.mb_baud = (uint32_t)cfg_regs[1] * 100U;
	if (cfg_regs[0] > 247 || !app_cfg_valid(&c)) {
		LOG_W(TAG, "settings written over Modbus rejected: unit %u, baud %u\n", cfg_regs[0], c.mb_baud);
		cfg_regs_show();
		return;
	}
	app_cfg_set(&c);
	if (app_cfg_save() == 0) {
		LOG_I(TAG, "settings saved over Modbus: unit %d, %u baud, in force after reset\n", c.mb_unit, c.mb_baud);
	}
	else {
		LOG_E(TAG, "settings save failed\n");
	}
}

static void on_write(uint16_t address, uint16_t quantity) {
	if (address >= 32) {
		cfg_regs_written();
		return;
	}
#if defined(APP_KIT_DEMO)
	/* register 16 feeds the scope screen; no log line, a master may write it many times a second */
	if (address == 16) {
		plot_add((int16_t)user_regs[0]);
		if (quantity == 1) {
			return;
		}
	}
#endif
	LOG_I(TAG, "master wrote %d register(s) at %d\n", quantity, address);
}

static const mb_slave_map_t reg_map = {
	holding_blocks, sizeof(holding_blocks) / sizeof(holding_blocks[0]),
	0, 0,
	on_write,
};

/* A verified image is in STAGING. The answer to COMMIT leaves in the
 * background once the poll returns; task_poll_modbus() lets task_fw install
 * and reset only after it is out on the wire. */
static uint8_t install_pending;

static void on_ota_commit(void) {
	install_pending = 1;
}

void app_modbus_init(void) {
	const fw_version_t* v = app_version();

	info_regs[0] = (uint16_t)((v->major << 8) | v->minor);
	info_regs[1] = v->patch;
	cfg_regs_show();
	run_unit = app_cfg.mb_unit;
	run_baud = app_cfg.mb_baud;

	if (mb_slave_init(run_unit, run_baud, &reg_map) != 0) {
		LOG_E(TAG, "slave init failed\n");
		return;
	}
	mb_slave_enable_ota(on_ota_commit);
	LOG_I(TAG, "slave unit %d, %u 8N1\n", run_unit, run_baud);
}

void task_poll_modbus(void) {
	uint16_t uptime = (uint16_t)(hal_millis() / 1000U);

	/* once a second: the crash log count scans the whole NVM ring (8 CRCs),
	 * far too much for a poll that runs every pass of the kernel loop */
	if (uptime != info_regs[2]) {
		info_regs[2] = uptime;
		info_regs[3] = crash_log_count();
	}
	mb_slave_poll();
	if (install_pending && !hal_rs485_tx_busy()) {
		install_pending = 0;
		task_post_pure_msg(TASK_FW_ID, FW_SIG_INSTALL);
	}
}

void cmd_mb(const char* args) {
	(void)args;
	xprintf("modbus slave unit %d, %u 8N1: %u byte(s) received, %u request(s) answered\n",
			run_unit, run_baud, hal_rs485_rx_bytes(), mb_slave_request_count());
}

/*----------------------------------------------------------------------------
 * master, and the gateway (a master with a Modbus TCP server in front)
 *--------------------------------------------------------------------------*/
#elif defined(APP_MODBUS_MASTER) || defined(APP_MODBUS_GATEWAY)

#include "mb_master.h"
#if defined(APP_MODBUS_GATEWAY)
#include "mb_gateway.h"
#include "hal_tcp.h"

static const hal_net_cfg_t net_cfg = { APP_NET_MAC, APP_NET_IP, APP_NET_MASK, APP_NET_GW };
static uint8_t gateway_up;
#endif

void app_modbus_init(void) {
	if (mb_master_init(app_cfg.mb_baud, APP_MB_RESPONSE_TIMEOUT_MS) != MB_OK) {
		LOG_E(TAG, "master init failed\n");
		return;
	}
	LOG_I(TAG, "master, %u 8N1, response timeout %d ms\n", app_cfg.mb_baud, APP_MB_RESPONSE_TIMEOUT_MS);
#if defined(APP_MODBUS_GATEWAY)
	if (hal_tcp_init(&net_cfg) != 0) {
		LOG_E(TAG, "no Ethernet chip: gateway off, RS485 master still works\n");
		return;
	}
	if (mb_gateway_init(APP_MB_TCP_PORT) != 0) {
		LOG_E(TAG, "gateway init failed\n");
		return;
	}
	gateway_up = 1;
	LOG_I(TAG, "gateway: Modbus TCP %d.%d.%d.%d:%d -> RS485\n", net_cfg.ip[0], net_cfg.ip[1],
		  net_cfg.ip[2], net_cfg.ip[3], APP_MB_TCP_PORT);
#endif
}

void task_poll_modbus(void) {
	mb_master_poll();
#if defined(APP_MODBUS_GATEWAY)
	if (gateway_up) {
		mb_gateway_poll();
	}
#endif
}

/* the shell command in flight: what to print when its answer is in */
static uint32_t cmd_address, cmd_count;
static uint8_t cmd_is_write;

static void cmd_done(void) {
	task_post_pure_msg(TASK_CONSOLE_ID, CONSOLE_SIG_MB_DONE);
}

void cmd_mb_done(void) {
	const mb_result_t* r = mb_master_result();
	uint16_t regs[16];
	int n;

	if (r->err != MB_OK) {
		xprintf("%s unit %u reg %u: %s\n", cmd_is_write ? "write" : "read", r->unit, cmd_address, mb_err_str(r->err));
		return;
	}
	if (cmd_is_write) {
		xprintf("write unit %u reg %u: ok\n", r->unit, cmd_address);
		return;
	}
	n = mb_result_regs(r, regs, 16);
	if (n != (int)cmd_count) {
		xprintf("read unit %u reg %u: %s\n", r->unit, cmd_address, mb_err_str(MB_ERR_BAD_RESPONSE));
		return;
	}
	for (int i = 0; i < n; i++) {
		xprintf("  %5u: %5u  0x%04X\n", cmd_address + (uint32_t)i, regs[i], regs[i]);
	}
}

/* decimal or 0x hex; returns 0 if *p is not a number */
static uint8_t next_num(const char** p, uint32_t* out) {
	const char* s = *p;
	uint32_t v = 0;
	uint8_t digits = 0;

	while (*s == ' ') {
		s++;
	}
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		for (s += 2; ; s++, digits++) {
			char ch = *s;
			if (ch >= '0' && ch <= '9')			v = (v << 4) | (uint32_t)(ch - '0');
			else if (ch >= 'a' && ch <= 'f')	v = (v << 4) | (uint32_t)(ch - 'a' + 10);
			else if (ch >= 'A' && ch <= 'F')	v = (v << 4) | (uint32_t)(ch - 'A' + 10);
			else break;
		}
	}
	else {
		for (; *s >= '0' && *s <= '9'; s++, digits++) {
			v = v * 10 + (uint32_t)(*s - '0');
		}
	}
	*p = s;
	*out = v;
	return digits != 0;
}

void cmd_mb(const char* args) {
	uint8_t pdu[MB_PDU_MAX];
	uint8_t n;
	uint32_t unit, address, x = 1;
	uint8_t is_write;
	int err;

	if (strncmp(args, "read ", 5) == 0) {
		is_write = 0;
	}
	else if (strncmp(args, "write ", 6) == 0) {
		is_write = 1;
	}
	else {
#if defined(APP_MODBUS_GATEWAY)
		xprintf("gateway %s, link %s, client %s, %u request(s), %u failed\n",
				gateway_up ? "on" : "off (no chip)", hal_tcp_link_up() ? "up" : "down",
				hal_tcp_connected() ? "connected" : "none", mb_gateway_request_count(), mb_gateway_error_count());
#endif
		xprintf("usage: mb read <unit> <addr> [count 1..16] | mb write <unit> <addr> <value>\n");
		return;
	}
	args += is_write ? 6 : 5;
	if (!next_num(&args, &unit) || !next_num(&args, &address) || unit > 247 || address > 0xFFFF) {
		xprintf("bad unit / address\n");
		return;
	}

	if (is_write) {
		if (!next_num(&args, &x) || x > 0xFFFF) {
			xprintf("bad value\n");
			return;
		}
		n = mb_pdu_write_single(pdu, (uint16_t)address, (uint16_t)x);
	}
	else {
		next_num(&args, &x);
		if (x < 1 || x > 16) {
			x = 1;
		}
		n = mb_pdu_read(pdu, 3, (uint16_t)address, (uint16_t)x);
	}
	cmd_address = address;
	cmd_count = x;
	cmd_is_write = is_write;
	/* the answer arrives as CONSOLE_SIG_MB_DONE -> cmd_mb_done() */
	err = mb_master_send((uint8_t)unit, pdu, n, cmd_done);
	if (err != MB_OK) {
		xprintf("%s unit %u reg %u: %s\n", is_write ? "write" : "read", unit, address, mb_err_str(err));
	}
}

/*----------------------------------------------------------------------------
 * no Modbus in this build
 *--------------------------------------------------------------------------*/
#else

void app_modbus_init(void) {
}

void task_poll_modbus(void) {
}

#endif
