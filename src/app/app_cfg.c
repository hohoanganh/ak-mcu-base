/**
 ******************************************************************************
 * @brief:  Settings of the sample app (services/sys/settings): the Modbus
 *          address and baud rate, which a product sets in the field instead
 *          of at build time. Shell "cfg", and holding registers 32..33 of
 *          the slave. A change takes effect at the next reset: the RS485
 *          port is set up once, at start.
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

#include "app.h"

#define TAG "CFG"

app_cfg_t app_cfg;
static uint8_t cfg_from_nvm;		/* 1: loaded from NVM, 0: defaults */
static uint8_t cfg_dirty;			/* changed since load / save */

static const app_cfg_t cfg_defaults = {
	APP_MB_UNIT_ID,
	{ 0, 0, 0 },
	APP_MB_BAUD,
};

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
	return c->mb_unit >= 1 && c->mb_unit <= 247 && app_cfg_baud_ok(c->mb_baud);
}

void app_cfg_load(void) {
	cfg_from_nvm = settings_load(APP_CFG_VERSION, &app_cfg, sizeof(app_cfg), &cfg_defaults);
	cfg_dirty = 0;
	if (cfg_from_nvm && !app_cfg_valid(&app_cfg)) {
		LOG_W(TAG, "settings in NVM are out of range: using the defaults\n");
		app_cfg = cfg_defaults;
		cfg_from_nvm = 0;
	}
	LOG_I(TAG, "modbus unit %d, %u baud (%s)\n", app_cfg.mb_unit, app_cfg.mb_baud,
		  cfg_from_nvm ? "saved" : "defaults");
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
		xprintf("modbus unit %d, baud %u (%s%s)\n", app_cfg.mb_unit, app_cfg.mb_baud,
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
	else if (strcmp(args, "save") == 0) {
		int r = app_cfg_save();

		xprintf(r == 0 ? "saved, takes effect after 'reboot'\n" : "save failed (%d)\n", r);
		return;
	}
	else if (strcmp(args, "reset") == 0) {
		app_cfg_set(&cfg_defaults);
		xprintf("defaults: unit %d, baud %u; 'cfg save' to keep them\n", app_cfg.mb_unit, app_cfg.mb_baud);
		return;
	}
	else {
		xprintf("usage: cfg | cfg unit <1..247> | cfg baud <rate> | cfg save | cfg reset\n");
		return;
	}
	app_cfg_set(&c);
	xprintf("unit %d, baud %u (not saved yet: 'cfg save')\n", app_cfg.mb_unit, app_cfg.mb_baud);
}
