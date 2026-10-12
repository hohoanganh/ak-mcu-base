/**
 ******************************************************************************
 * @brief:  Modbus on the RS485 port. The role is chosen at build time:
 *
 *   APP_MODBUS_SLAVE   the board answers a register map and takes firmware
 *                      updates over RS485 (block 0xF000, tools/ak_mb.py)
 *   APP_MODBUS_MASTER  the board asks other devices; shell: mb read / mb write
 *
 *  Slave register map of this sample (holding, FC 03 / 06 / 16):
 *    0  R  app version, (major << 8) | minor
 *    1  R  app version, patch
 *    2  R  uptime in seconds (wraps at 65535)
 *    3  R  crash log: number of records
 *    16..19  R/W  scratch registers, kept until reset
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

#if defined(APP_MODBUS_SLAVE) && defined(APP_MODBUS_MASTER)
#error "one RS485 port: build with APP_MODBUS_SLAVE or APP_MODBUS_MASTER, not both"
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

static const mb_reg_block_t holding_blocks[] = {
	{ 0,  4, info_regs, MB_REG_RO },
	{ 16, 4, user_regs, MB_REG_RW },
};

static void on_write(uint16_t address, uint16_t quantity) {
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

/* A verified image is in STAGING. The answer to COMMIT goes out when the
 * poll returns; task_fw then asks the bootloader to install and resets. */
static void on_ota_commit(void) {
	task_post_pure_msg(TASK_FW_ID, FW_SIG_INSTALL);
}

void app_modbus_init(void) {
	const fw_version_t* v = app_version();

	info_regs[0] = (uint16_t)((v->major << 8) | v->minor);
	info_regs[1] = v->patch;

	if (mb_slave_init(APP_MB_UNIT_ID, APP_MB_BAUD, &reg_map) != 0) {
		LOG_E(TAG, "slave init failed\n");
		return;
	}
	mb_slave_enable_ota(on_ota_commit);
	LOG_I(TAG, "slave unit %d, %d 8N1\n", APP_MB_UNIT_ID, APP_MB_BAUD);
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
}

void cmd_mb(const char* args) {
	(void)args;
	xprintf("modbus slave unit %d, %d 8N1: %u byte(s) received, %u request(s) answered\n",
			APP_MB_UNIT_ID, APP_MB_BAUD, hal_rs485_rx_bytes(), mb_slave_request_count());
}

/*----------------------------------------------------------------------------
 * master
 *--------------------------------------------------------------------------*/
#elif defined(APP_MODBUS_MASTER)

#include "mb_master.h"

void app_modbus_init(void) {
	if (mb_master_init(APP_MB_BAUD, APP_MB_RESPONSE_TIMEOUT_MS) != MB_OK) {
		LOG_E(TAG, "master init failed\n");
		return;
	}
	LOG_I(TAG, "master, %d 8N1, response timeout %d ms\n", APP_MB_BAUD, APP_MB_RESPONSE_TIMEOUT_MS);
}

void task_poll_modbus(void) {
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
	uint16_t regs[16];
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
		err = mb_master_write_single((uint8_t)unit, (uint16_t)address, (uint16_t)x);
		xprintf("write unit %u reg %u = %u: %s\n", unit, address, x, mb_err_str(err));
		return;
	}

	next_num(&args, &x);
	if (x < 1 || x > 16) {
		x = 1;
	}
	err = mb_master_read_holding((uint8_t)unit, (uint16_t)address, (uint16_t)x, regs);
	if (err != MB_OK) {
		xprintf("read unit %u reg %u: %s\n", unit, address, mb_err_str(err));
		return;
	}
	for (uint32_t i = 0; i < x; i++) {
		xprintf("  %5u: %5u  0x%04X\n", address + i, regs[i], regs[i]);
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
