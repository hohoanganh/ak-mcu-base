#include <string.h>

#include "settings.h"
#include "crc.h"
#include "hal.h"

#define SETTINGS_MAGIC		(0x4B53)	/* "SK" */

typedef struct {
	uint16_t magic;
	uint8_t  version;
	uint8_t  len;
	uint8_t  data[SETTINGS_DATA_MAX];
	uint16_t crc16;				/* over everything above */
} settings_rec_t;

typedef char settings_rec_size_check[(sizeof(settings_rec_t) == SETTINGS_NVM_SIZE) ? 1 : -1];
typedef char settings_fit_check[(SETTINGS_NVM_OFFSET + SETTINGS_NVM_SIZE <= HAL_NVM_SIZE) ? 1 : -1];

static uint16_t rec_crc(const settings_rec_t* r) {
	return crc16_update(CRC16_INIT, r, sizeof(*r) - 2);
}

static uint8_t rec_read(settings_rec_t* r) {
	return hal_nvm_read(SETTINGS_NVM_OFFSET, r, sizeof(*r)) == 0 &&
		   r->magic == SETTINGS_MAGIC && r->crc16 == rec_crc(r);
}

uint8_t settings_load(uint8_t version, void* data, uint8_t len, const void* defaults) {
	settings_rec_t r;

	if (!data || len == 0 || len > SETTINGS_DATA_MAX) {
		return 0;
	}
	if (rec_read(&r) && r.version == version && r.len == len) {
		memcpy(data, r.data, len);
		return 1;
	}
	if (defaults) {
		memcpy(data, defaults, len);
	}
	else {
		memset(data, 0, len);
	}
	return 0;
}

int settings_save(uint8_t version, const void* data, uint8_t len) {
	settings_rec_t r, cur;

	if (!data || len == 0 || len > SETTINGS_DATA_MAX) {
		return -1;
	}
	memset(&r, 0, sizeof(r));
	r.magic = SETTINGS_MAGIC;
	r.version = version;
	r.len = len;
	memcpy(r.data, data, len);
	r.crc16 = rec_crc(&r);

	if (hal_nvm_read(SETTINGS_NVM_OFFSET, &cur, sizeof(cur)) == 0 && memcmp(&cur, &r, sizeof(r)) == 0) {
		return 0;				/* already there: spare the NVM */
	}
	if (hal_nvm_write(SETTINGS_NVM_OFFSET, &r, sizeof(r)) != 0) {
		return -2;
	}
	if (hal_nvm_read(SETTINGS_NVM_OFFSET, &cur, sizeof(cur)) != 0 || memcmp(&cur, &r, sizeof(r)) != 0) {
		return -3;
	}
	return 0;
}

void settings_clear(void) {
	settings_rec_t blank;

	memset(&blank, 0, sizeof(blank));
	hal_nvm_write(SETTINGS_NVM_OFFSET, &blank, sizeof(blank));
}
