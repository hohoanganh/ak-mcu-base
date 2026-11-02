/**
 * services/sys on the host: the settings record in NVM (load, save, defaults,
 * a record of another layout, a record cut by a power loss) and that it stays
 * clear of boot_ctrl and the crash log.
 */
#include <string.h>

#include "hal.h"
#include "settings.h"
#include "crash_log.h"
#include "boot_ctrl.h"
#include "port_host.h"
#include "tiny_test.h"

typedef struct {
	uint8_t  unit;
	uint8_t  pad[3];
	uint32_t baud;
} cfg_v1_t;

typedef struct {
	uint8_t  unit;
	uint8_t  pad[3];
	uint32_t baud;
	uint16_t extra;
} cfg_v2_t;

static const cfg_v1_t def1 = { 1, { 0, 0, 0 }, 9600 };

static void test_blank_nvm_gives_defaults(void) {
	cfg_v1_t c;

	/* NVM erased to 0x00 (STM32L1 EEPROM) and to 0xFF (a flash page elsewhere) */
	host_reset_state(0x00);
	memset(host_nvm_mem(), 0x00, HAL_NVM_SIZE);
	CHECK_EQ(settings_load(1, &c, sizeof(c), &def1), 0);
	CHECK_EQ(c.unit, 1);
	CHECK_EQ(c.baud, 9600);

	memset(host_nvm_mem(), 0xFF, HAL_NVM_SIZE);
	memset(&c, 0x55, sizeof(c));
	CHECK_EQ(settings_load(1, &c, sizeof(c), &def1), 0);
	CHECK_EQ(c.baud, 9600);

	/* no defaults given: zeros */
	CHECK_EQ(settings_load(1, &c, sizeof(c), 0), 0);
	CHECK_EQ(c.unit, 0);
	CHECK_EQ(c.baud, 0);
}

static void test_save_then_load(void) {
	cfg_v1_t c = { 7, { 0, 0, 0 }, 19200 };
	cfg_v1_t r;
	uint8_t before[SETTINGS_NVM_OFFSET];

	host_reset_state(0x00);
	memcpy(before, host_nvm_mem(), sizeof(before));
	CHECK_EQ(settings_save(1, &c, sizeof(c)), 0);
	memset(&r, 0, sizeof(r));
	CHECK_EQ(settings_load(1, &r, sizeof(r), &def1), 1);
	CHECK_EQ(r.unit, 7);
	CHECK_EQ(r.baud, 19200);
	/* nothing below the record was touched */
	CHECK(memcmp(before, host_nvm_mem(), sizeof(before)) == 0);

	/* saving the same again changes nothing; a change does */
	CHECK_EQ(settings_save(1, &c, sizeof(c)), 0);
	c.unit = 9;
	CHECK_EQ(settings_save(1, &c, sizeof(c)), 0);
	CHECK_EQ(settings_load(1, &r, sizeof(r), &def1), 1);
	CHECK_EQ(r.unit, 9);

	settings_clear();
	CHECK_EQ(settings_load(1, &r, sizeof(r), &def1), 0);
	CHECK_EQ(r.unit, 1);
}

static void test_other_layout_is_ignored(void) {
	cfg_v1_t c = { 7, { 0, 0, 0 }, 19200 };
	cfg_v2_t r2;
	cfg_v1_t r1;

	host_reset_state(0x00);
	CHECK_EQ(settings_save(1, &c, sizeof(c)), 0);
	/* a newer firmware with a longer record / another version: defaults */
	CHECK_EQ(settings_load(1, &r2, sizeof(r2), 0), 0);
	CHECK_EQ(settings_load(2, &r1, sizeof(r1), &def1), 0);
	CHECK_EQ(r1.unit, 1);
	/* the old record is still there for the old firmware */
	CHECK_EQ(settings_load(1, &r1, sizeof(r1), &def1), 1);
	CHECK_EQ(r1.unit, 7);
}

static void test_torn_record_gives_defaults(void) {
	cfg_v1_t c = { 7, { 0, 0, 0 }, 19200 };
	cfg_v1_t r;
	uint8_t* nvm = host_nvm_mem();

	host_reset_state(0x00);
	CHECK_EQ(settings_save(1, &c, sizeof(c)), 0);
	/* power loss while writing: one byte of the data differs from the CRC */
	nvm[SETTINGS_NVM_OFFSET + 4] ^= 0x01;
	CHECK_EQ(settings_load(1, &r, sizeof(r), &def1), 0);
	CHECK_EQ(r.unit, 1);
	/* ... or the CRC itself was not written yet */
	nvm[SETTINGS_NVM_OFFSET + 4] ^= 0x01;
	CHECK_EQ(settings_load(1, &r, sizeof(r), &def1), 1);
	nvm[SETTINGS_NVM_OFFSET + 31] = 0;
	CHECK_EQ(settings_load(1, &r, sizeof(r), &def1), 0);
}

static void test_bad_arguments(void) {
	uint8_t big[SETTINGS_DATA_MAX + 1];
	cfg_v1_t r;

	host_reset_state(0x00);
	CHECK_EQ(settings_save(1, big, sizeof(big)), -1);
	CHECK_EQ(settings_save(1, 0, 4), -1);
	CHECK_EQ(settings_save(1, big, 0), -1);
	CHECK_EQ(settings_load(1, &r, 0, &def1), 0);
	CHECK_EQ(settings_load(1, 0, sizeof(r), &def1), 0);
	/* the largest record fits */
	memset(big, 0xA5, sizeof(big));
	CHECK_EQ(settings_save(3, big, SETTINGS_DATA_MAX), 0);
	memset(big, 0, sizeof(big));
	CHECK_EQ(settings_load(3, big, SETTINGS_DATA_MAX, 0), 1);
	CHECK_EQ(big[SETTINGS_DATA_MAX - 1], 0xA5);
}

/* boot_ctrl, the crash log and the settings share the NVM without overlap */
static void test_shares_nvm(void) {
	cfg_v1_t c = { 7, { 0, 0, 0 }, 19200 };
	cfg_v1_t r;
	boot_ctrl_t ctrl;
	crash_rec_t rec;

	host_reset_state(0x00);
	CHECK(CRASH_LOG_NVM_OFFSET + CRASH_LOG_SLOTS * 20 <= SETTINGS_NVM_OFFSET);
	CHECK_EQ(settings_save(1, &c, sizeof(c)), 0);

	boot_ctrl_load(&ctrl);
	ctrl.cmd = BOOT_CMD_LOADER;
	CHECK_EQ(boot_ctrl_save(&ctrl), 0);
	for (uint8_t i = 0; i < CRASH_LOG_SLOTS + 2; i++) {
		memset(&rec, 0, sizeof(rec));
		rec.kind = CRASH_KIND_FATAL;
		rec.code = i;
		CHECK_EQ(crash_log_add(&rec), 0);
	}
	CHECK_EQ(crash_log_count(), CRASH_LOG_SLOTS);
	CHECK_EQ(settings_load(1, &r, sizeof(r), &def1), 1);
	CHECK_EQ(r.unit, 7);
	boot_ctrl_load(&ctrl);
	CHECK_EQ(ctrl.cmd, BOOT_CMD_LOADER);
}

TT_MAIN_BEGIN("test_sys")
	RUN_TEST(test_blank_nvm_gives_defaults);
	RUN_TEST(test_save_then_load);
	RUN_TEST(test_other_layout_is_ignored);
	RUN_TEST(test_torn_record_gives_defaults);
	RUN_TEST(test_bad_arguments);
	RUN_TEST(test_shares_nvm);
TT_MAIN_END()
