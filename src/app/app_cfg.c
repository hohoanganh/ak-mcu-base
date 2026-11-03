/**
 ******************************************************************************
 * @brief:  Settings of the sample app (services/sys/settings): the Modbus
 *          address, baud rate and character format, and the master's answer
 *          timeout, which a product sets in the field instead of at build
 *          time. Shell "cfg", and holding registers 32..34 of the slave. A
 *          change takes effect at the next reset: the RS485 port is set up
 *          once, at start.
 *
 *  A project adds its own fields to app_cfg_t and bumps APP_CFG_VERSION:
 *  a record of the old layout in NVM is then ignored (defaults), nothing
 *  else to migrate.
 ******************************************************************************
**/

#include <string.h>

#include "ak.h"
#include "ak_log.h"
#include "settings.h"
#include "hal_rs485.h"

#include "app.h"

#define TAG "CFG"

app_cfg_t app_cfg;
static uint8_t cfg_from_nvm;		/* 1: loaded from NVM, 0: defaults */
static uint8_t cfg_dirty;			/* changed since load / save */

static const app_cfg_t cfg_defaults = {
	APP_MB_UNIT_ID,
	APP_MB_FORMAT,
	0,						/* APP_MB_RESPONSE_TIMEOUT_MS */
	APP_MB_BAUD,
};

#define TIMEOUT_MIN_MS		(20)
#define TIMEOUT_MAX_MS		(5000)

static const char* const format_names[] = { "8N1", "8E1", "8O1", "8N2" };

const char* app_cfg_format_str(uint8_t format) {
	return format <= HAL_RS485_8N2 ? format_names[format] : "?";
}

uint16_t app_cfg_timeout_ms(void) {
	return app_cfg.mb_timeout_ms ? app_cfg.mb_timeout_ms : APP_MB_RESPONSE_TIMEOUT_MS;
}

static const uint32_t bauds[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };

uint8_t app_cfg_baud_ok(uint32_t baud) {
	for (uint8_t i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++) {
		if (bauds[i] == baud) {
			return 1;
		}
	}
	return 0;
}

uint8_t app_cfg_valid(const app_cfg_t* c) {
	return c->mb_unit >= 1 && c->mb_unit <= 247 && app_cfg_baud_ok(c->mb_baud) && c->mb_format <= HAL_RS485_8N2 &&
		   (c->mb_timeout_ms == 0 || (c->mb_timeout_ms >= TIMEOUT_MIN_MS && c->mb_timeout_ms <= TIMEOUT_MAX_MS));
}

void app_cfg_load(void) {
	cfg_from_nvm = settings_load(APP_CFG_VERSION, &app_cfg, sizeof(app_cfg), &cfg_defaults);
	cfg_dirty = 0;
	if (cfg_from_nvm && !app_cfg_valid(&app_cfg)) {
		LOG_W(TAG, "settings in NVM are out of range: using the defaults\n");
		app_cfg = cfg_defaults;
		cfg_from_nvm = 0;
	}
	LOG_I(TAG, "modbus unit %d, %u %s, timeout %u ms (%s)\n", app_cfg.mb_unit, app_cfg.mb_baud,
		  app_cfg_format_str(app_cfg.mb_format), app_cfg_timeout_ms(), cfg_from_nvm ? "saved" : "defaults");
}

int app_cfg_save(void) {
	int r;

	if (!app_cfg_valid(&app_cfg)) {
		return -1;
	}
	r = settings_save(APP_CFG_VERSION, &app_cfg, sizeof(app_cfg));
	if (r == 0) {
		cfg_from_nvm = 1;
		cfg_dirty = 0;
	}
	return r;
}

void app_cfg_set(const app_cfg_t* c) {
	app_cfg = *c;
	cfg_dirty = 1;
}

/* decimal number; 0 if there is none */
static uint32_t num(const char* s) {
	uint32_t v = 0;

	while (*s == ' ') {
		s++;
	}
	while (*s >= '0' && *s <= '9') {
		v = v * 10 + (uint32_t)(*s++ - '0');
	}
	return v;
}

void cmd_cfg(const char* args) {
	app_cfg_t c = app_cfg;
	uint32_t v;

	if (*args == 0) {
		xprintf("modbus unit %d, baud %u %s, timeout %u ms (%s%s)\n", app_cfg.mb_unit, app_cfg.mb_baud,
				app_cfg_format_str(app_cfg.mb_format), app_cfg_timeout_ms(),
				cfg_from_nvm ? "saved" : "defaults", cfg_dirty ? ", changed: 'cfg save'" : "");
		return;
	}
	if (strncmp(args, "unit ", 5) == 0) {
		v = num(args + 5);
		if (v < 1 || v > 247) {
			xprintf("unit 1..247\n");
			return;
		}
		c.mb_unit = (uint8_t)v;
	}
	else if (strncmp(args, "baud ", 5) == 0) {
		v = num(args + 5);
		if (!app_cfg_baud_ok(v)) {
			xprintf("baud: 1200 2400 4800 9600 19200 38400 57600 115200\n");
			return;
		}
		c.mb_baud = v;
	}
	else if (strncmp(args, "format ", 7) == 0) {
		uint8_t f;

		for (f = 0; f <= HAL_RS485_8N2; f++) {
			if (strcmp(args + 7, format_names[f]) == 0) {
				break;
			}
		}
		if (f > HAL_RS485_8N2) {
			xprintf("format: 8N1 8E1 8O1 8N2\n");
			return;
		}
		c.mb_format = f;
	}
	else if (strncmp(args, "timeout ", 8) == 0) {
		v = num(args + 8);
		if (v < TIMEOUT_MIN_MS || v > TIMEOUT_MAX_MS) {
			xprintf("timeout %d..%d ms\n", TIMEOUT_MIN_MS, TIMEOUT_MAX_MS);
			return;
		}
		c.mb_timeout_ms = (uint16_t)v;
	}
	else if (strcmp(args, "save") == 0) {
		int r = app_cfg_save();

		xprintf(r == 0 ? "saved, takes effect after 'reboot'\n" : "save failed (%d)\n", r);
		return;
	}
	else if (strcmp(args, "reset") == 0) {
		app_cfg_set(&cfg_defaults);
		xprintf("defaults: unit %d, baud %u %s; 'cfg save' to keep them\n", app_cfg.mb_unit, app_cfg.mb_baud,
				app_cfg_format_str(app_cfg.mb_format));
		return;
	}
	else {
		xprintf("usage: cfg | cfg unit <1..247> | cfg baud <rate> | cfg format 8N1|8E1|8O1|8N2 |"
				" cfg timeout <ms> | cfg save | cfg reset\n");
		return;
	}
	app_cfg_set(&c);
	xprintf("unit %d, baud %u %s, timeout %u ms (not saved yet: 'cfg save')\n", app_cfg.mb_unit, app_cfg.mb_baud,
			app_cfg_format_str(app_cfg.mb_format), app_cfg_timeout_ms());
}
