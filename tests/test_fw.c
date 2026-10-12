/**
 * Firmware update + bootloader tests on host: crc, header, STAGING writer,
 * protocol, boot_decide, boot_install, power cut at EVERY flash operation.
 */
#include <string.h>
#include <setjmp.h>

#include "hal.h"
#include "crc.h"
#include "boot_ctrl.h"
#include "crash_log.h"
#include "boot_core.h"
#include "fw_image.h"
#include "fw_update.h"
#include "fw_proto.h"
#include "flog.h"
#include "port_host.h"
#include "tiny_test.h"

extern void boot_main(void);

static uint8_t img_buf[0x10000];

/*----------------------------------------------------------------------------
 * helper
 *--------------------------------------------------------------------------*/
static uint32_t make_image(uint8_t* out, uint32_t img_size, uint8_t maj, uint8_t min, uint8_t pat,
						   const char* board, uint32_t seed) {
	fw_image_hdr_t* h = (fw_image_hdr_t*)out;
	uint32_t app_addr = hal_flash_info(FLASH_PART_APP)->addr;
	uint8_t* p = out + FW_IMAGE_HDR_SIZE;
	uint32_t x = seed * 2654435761UL + 1;
	uint32_t sp = HOST_RAM_END;
	uint32_t rst = app_addr + FW_IMAGE_HDR_SIZE + 0x101;

	memset(out, 0, FW_IMAGE_HDR_SIZE);
	for (uint32_t i = 0; i < img_size; i++) {
		x = x * 1103515245UL + 12345UL;
		p[i] = (uint8_t)(x >> 16);
	}
	memcpy(p, &sp, 4);
	memcpy(p + 4, &rst, 4);

	h->magic = FW_IMAGE_MAGIC;
	h->hdr_version = FW_IMAGE_HDR_VERSION;
	h->hdr_size = FW_IMAGE_HDR_SIZE;
	h->img_size = img_size;
	h->img_crc32 = crc32_update(CRC32_INIT, p, img_size);
	h->load_addr = app_addr + FW_IMAGE_HDR_SIZE;
	h->version.major = maj;
	h->version.minor = min;
	h->version.patch = pat;
	h->version.build = seed;
	strncpy(h->board, board, FW_IMAGE_BOARD_LEN);
	h->hdr_crc32 = crc32_update(CRC32_INIT, h, FW_IMAGE_HDR_SIZE - 4);
	return FW_IMAGE_HDR_SIZE + img_size;
}

static void rehdr(uint8_t* out) {
	fw_image_hdr_t* h = (fw_image_hdr_t*)out;
	h->hdr_crc32 = crc32_update(CRC32_INIT, h, FW_IMAGE_HDR_SIZE - 4);
}

static void put_part(flash_part_t part, const uint8_t* data, uint32_t len) {
	memcpy(host_part_mem(part), data, len);
}

static fw_err_t upload(const uint8_t* img, uint32_t total, uint32_t chunk) {
	fw_err_t e = fw_update_begin(total);
	for (uint32_t off = 0; e == FW_OK && off < total; off += chunk) {
		uint32_t n = (total - off < chunk) ? total - off : chunk;
		e = fw_update_write(off, img + off, n);
	}
	return e != FW_OK ? e : fw_update_finish(0);
}

static void setup(uint8_t erased) {
	host_reset_state(erased);	/* keeps the layout chosen by host_set_layout() */
	host_use_virtual_time(1);
	host_fatal_jmp = 0;
	host_reset_handler = 0;
	host_jump_handler = 0;
	hal_init();
	fw_update_abort();
}

static uint8_t part_version(flash_part_t part, fw_err_t* err) {
	fw_image_hdr_t h;
	*err = fw_image_verify(part, &h);
	return (*err == FW_OK) ? h.version.major : 0;
}

/*----------------------------------------------------------------------------
 * run boot_main() like a real MCU: jump / reset / power cut -> longjmp here
 *--------------------------------------------------------------------------*/
enum { BOOT_JUMPED = 1, BOOT_RESET = 2, BOOT_CUT = 3, BOOT_FATAL = 4 };
static jmp_buf boot_jmp;
static uint32_t jumped_addr;

static void on_jump(uint32_t a) {
	jumped_addr = a;
	longjmp(boot_jmp, BOOT_JUMPED);
}

static void on_reset(void) {
	longjmp(boot_jmp, BOOT_RESET);
}

static jmp_buf cut_jmp;
static jmp_buf fatal_jmp;

static int run_boot(uint32_t power_cut_after) {
	volatile int r;

	host_jump_handler = on_jump;
	host_reset_handler = on_reset;
	host_fatal_jmp = &fatal_jmp;
	jumped_addr = 0;

	if ((r = setjmp(boot_jmp)) != 0) {
		goto out;
	}
	if (setjmp(cut_jmp) != 0) {
		r = BOOT_CUT;
		goto out;
	}
	if (setjmp(fatal_jmp) != 0) {
		r = BOOT_FATAL;
		goto out;
	}

	host_flash_power_cut(power_cut_after, &cut_jmp);
	fw_update_abort();
	boot_main();
	r = 0;

out:
	host_flash_power_cut(0, 0);
	host_fatal_jmp = 0;
	return r;
}

static void inject_frame(uint8_t cmd, uint8_t seq, const uint8_t* payload, uint16_t len) {
	uint8_t f[FW_PROTO_MAX_PAYLOAD + 8];
	host_console_inject(f, fw_proto_pack(f, cmd, seq, payload, len));
}

static uint32_t app_entry(void) {
	return hal_flash_info(FLASH_PART_APP)->addr + FW_IMAGE_HDR_SIZE;
}

/*----------------------------------------------------------------------------
 * test
 *--------------------------------------------------------------------------*/
static void test_crc(void) {
	CHECK_EQ(crc32_update(0, "123456789", 9), 0xCBF43926UL);
	CHECK_EQ(crc16_update(0xFFFF, "123456789", 9), 0x29B1);
	/* incremental == one shot */
	CHECK_EQ(crc32_update(crc32_update(0, "1234", 4), "56789", 5), 0xCBF43926UL);
}

static void test_upload_ok(void) {
	fw_image_hdr_t h;
	uint32_t total;

	for (int erased = 0; erased < 2; erased++) {
		setup(erased ? 0xFF : 0x00);	/* 0x00 = STM32L1 */
		total = make_image(img_buf, 5001, 2, 3, 4, HOST_BOARD_NAME, 7);
		CHECK_EQ(upload(img_buf, total, 128), FW_OK);
		CHECK_EQ(fw_update_state(), FW_UPDATE_READY);
		CHECK_EQ(fw_image_verify(FLASH_PART_STAGING, &h), FW_OK);
		CHECK_EQ(h.version.minor, 3);
		CHECK_EQ(h.img_size, 5001);
	}
}

static void test_upload_errors(void) {
	uint32_t total;
	fw_image_hdr_t* h = (fw_image_hdr_t*)img_buf;

	setup(0xFF);
	total = make_image(img_buf, 3000, 1, 0, 0, HOST_BOARD_NAME, 1);

	CHECK_EQ(fw_update_begin(hal_flash_info(FLASH_PART_STAGING)->size + 1), FW_ERR_TOO_BIG);
	CHECK_EQ(fw_update_begin(100), FW_ERR_SIZE);
	CHECK_EQ(fw_update_write(0, img_buf, 4), FW_ERR_STATE);

	CHECK_EQ(fw_update_begin(total), FW_OK);
	CHECK_EQ(fw_update_write(4, img_buf, 4), FW_ERR_OFFSET);
	if (hal_flash_info(FLASH_PART_STAGING)->write_size > 1) {
		CHECK_EQ(fw_update_write(0, img_buf, 3), FW_ERR_SIZE);	/* unaligned chunk that is not the last */
		CHECK_EQ(fw_update_write(0, img_buf, 128), FW_OK);
	}
	else {
		/* byte-writable staging (SPI NOR): any chunk size is fine */
		CHECK_EQ(fw_update_write(0, img_buf, 3), FW_OK);
		CHECK_EQ(fw_update_write(3, img_buf + 3, 125), FW_OK);
	}
	CHECK_EQ(fw_update_finish(0), FW_ERR_STATE);			/* incomplete */

	/* corrupt one binary byte */
	img_buf[FW_IMAGE_HDR_SIZE + 1000] ^= 0x01;
	CHECK_EQ(upload(img_buf, total, 128), FW_ERR_IMG_CRC);
	img_buf[FW_IMAGE_HDR_SIZE + 1000] ^= 0x01;
	CHECK_EQ(upload(img_buf, total, 128), FW_OK);

	/* sai board */
	strcpy(h->board, "other-board");
	rehdr(img_buf);
	CHECK_EQ(upload(img_buf, total, 128), FW_ERR_BOARD);

	/* wrong load address */
	make_image(img_buf, 3000, 1, 0, 0, HOST_BOARD_NAME, 1);
	h->load_addr += 0x100;
	rehdr(img_buf);
	CHECK_EQ(upload(img_buf, total, 128), FW_ERR_ADDR);

	/* header crc */
	make_image(img_buf, 3000, 1, 0, 0, HOST_BOARD_NAME, 1);
	h->version.major = 9;
	CHECK_EQ(upload(img_buf, total, 128), FW_ERR_HDR_CRC);

	/* no magic */
	make_image(img_buf, 3000, 1, 0, 0, HOST_BOARD_NAME, 1);
	h->magic = 0;
	CHECK_EQ(upload(img_buf, total, 128), FW_ERR_MAGIC);

	/* bad vector table (wrong link address): rejected despite valid CRC */
	make_image(img_buf, 3000, 1, 0, 0, HOST_BOARD_NAME, 1);
	{
		uint32_t bad = 0x08000101UL;
		memcpy(img_buf + FW_IMAGE_HDR_SIZE + 4, &bad, 4);
		h->img_crc32 = crc32_update(0, img_buf + FW_IMAGE_HDR_SIZE, 3000);
		rehdr(img_buf);
	}
	CHECK_EQ(upload(img_buf, total, 128), FW_ERR_VECTOR);

	/* header img_size does not match the uploaded size */
	make_image(img_buf, 3000, 1, 0, 0, HOST_BOARD_NAME, 1);
	CHECK_EQ(upload(img_buf, total + 4, 128), FW_ERR_SIZE);
}

static void test_boot_decide(void) {
	boot_state_t s;
	const uint8_t M = BOOT_MAX_INSTALL_ATTEMPTS;
	const uint8_t T = BOOT_MAX_TRIAL_BOOTS;

	memset(&s, 0, sizeof(s));
#define DECIDE(cmd_, att_, app_, raw_, stg_) \
	(s.cmd = (cmd_), s.install_attempts = (att_), s.app_ok = (app_), \
	 s.app_raw_ok = (raw_), s.staging_ok = (stg_), boot_decide(&s))
#define DECIDE_T(cmd_, att_, app_, stg_, bak_, trial_, boots_) \
	(s.cmd = (cmd_), s.install_attempts = (att_), s.app_ok = (app_), s.app_raw_ok = 0, \
	 s.staging_ok = (stg_), s.backup_ok = (bak_), s.trial = (trial_), s.trial_boots = (boots_), boot_decide(&s))

	CHECK_EQ(DECIDE(BOOT_CMD_NONE, 0, 1, 0, 0), BOOT_ACT_RUN);
	CHECK_EQ(DECIDE(BOOT_CMD_NONE, 0, 1, 0, 1), BOOT_ACT_RUN);
	CHECK_EQ(DECIDE(BOOT_CMD_UPDATE, 0, 1, 0, 1), BOOT_ACT_INSTALL);
	CHECK_EQ(DECIDE(BOOT_CMD_UPDATE, 0, 1, 0, 0), BOOT_ACT_RUN);		/* bad staging: keep running old app */
	CHECK_EQ(DECIDE(BOOT_CMD_UPDATE, M, 1, 0, 1), BOOT_ACT_RUN);		/* attempts exhausted */
	CHECK_EQ(DECIDE(BOOT_CMD_UPDATE, M, 0, 1, 1), BOOT_ACT_LOADER);	/* NEVER run a half-copied image */
	CHECK_EQ(DECIDE(BOOT_CMD_UPDATE, 1, 0, 1, 1), BOOT_ACT_INSTALL);	/* resume install after power loss */
	CHECK_EQ(DECIDE(BOOT_CMD_NONE, 0, 0, 0, 1), BOOT_ACT_INSTALL);		/* recover from staging */
	CHECK_EQ(DECIDE(BOOT_CMD_NONE, 0, 0, 1, 1), BOOT_ACT_RUN_RAW);		/* debug SWD flash */
	CHECK_EQ(DECIDE(BOOT_CMD_NONE, 2, 0, 1, 1), BOOT_ACT_INSTALL);		/* install pending: no raw run */
	CHECK_EQ(DECIDE(BOOT_CMD_NONE, M, 0, 1, 1), BOOT_ACT_LOADER);
	CHECK_EQ(DECIDE(BOOT_CMD_NONE, 0, 0, 0, 0), BOOT_ACT_LOADER);
	CHECK_EQ(DECIDE(BOOT_CMD_LOADER, 0, 1, 0, 1), BOOT_ACT_LOADER);

	/* trial and rollback */
	CHECK_EQ(DECIDE_T(BOOT_CMD_NONE, 0, 1, 0, 1, 1, 0), BOOT_ACT_RUN);			/* on trial, boots left */
	CHECK_EQ(DECIDE_T(BOOT_CMD_NONE, 0, 1, 0, 1, 1, T - 1), BOOT_ACT_RUN);
	CHECK_EQ(DECIDE_T(BOOT_CMD_NONE, 0, 1, 0, 1, 1, T), BOOT_ACT_ROLLBACK);		/* never confirmed */
	CHECK_EQ(DECIDE_T(BOOT_CMD_NONE, 0, 1, 0, 0, 1, T), BOOT_ACT_RUN);			/* ...but no backup: keep it */
	CHECK_EQ(DECIDE_T(BOOT_CMD_NONE, M, 1, 0, 1, 1, T), BOOT_ACT_RUN);			/* rollback attempts exhausted */
	CHECK_EQ(DECIDE_T(BOOT_CMD_NONE, 0, 1, 0, 1, 0, T), BOOT_ACT_RUN);			/* confirmed: boots irrelevant */
	CHECK_EQ(DECIDE_T(BOOT_CMD_ROLLBACK, 0, 1, 0, 1, 0, 0), BOOT_ACT_ROLLBACK);	/* app asked for it */
	CHECK_EQ(DECIDE_T(BOOT_CMD_ROLLBACK, 0, 1, 0, 0, 0, 0), BOOT_ACT_RUN);		/* nothing to go back to */
	CHECK_EQ(DECIDE_T(BOOT_CMD_ROLLBACK, 1, 0, 0, 1, 0, 0), BOOT_ACT_ROLLBACK);	/* resume after power loss */
	CHECK_EQ(DECIDE_T(BOOT_CMD_UPDATE, 0, 1, 1, 1, 1, T), BOOT_ACT_INSTALL);	/* a new update wins */
	CHECK_EQ(DECIDE_T(BOOT_CMD_LOADER, 0, 1, 1, 1, 1, T), BOOT_ACT_LOADER);
	CHECK_EQ(DECIDE_T(BOOT_CMD_NONE, 0, 0, 0, 1, 0, 0), BOOT_ACT_ROLLBACK);		/* APP corrupt, no staging: backup */
	CHECK_EQ(DECIDE_T(BOOT_CMD_NONE, 0, 0, 1, 1, 0, 0), BOOT_ACT_INSTALL);		/* APP corrupt: staging first */
	CHECK_EQ(DECIDE_T(BOOT_CMD_NONE, M, 0, 0, 1, 0, 0), BOOT_ACT_LOADER);
#undef DECIDE
#undef DECIDE_T
}

static void test_boot_ctrl(void) {
	boot_ctrl_t c;

	setup(0xFF);
	boot_ctrl_load(&c);
	CHECK_EQ(c.cmd, BOOT_CMD_NONE);
	CHECK_EQ(boot_ctrl_set_cmd(BOOT_CMD_UPDATE), 0);
	boot_ctrl_load(&c);
	CHECK_EQ(c.cmd, BOOT_CMD_UPDATE);
	/* corrupt NVM -> defaults */
	host_nvm_mem()[5] ^= 0xFF;
	boot_ctrl_load(&c);
	CHECK_EQ(c.cmd, BOOT_CMD_NONE);
}

/* Power loss inside boot_ctrl_save(): NVM bytes are written one by one, so
 * the NVM holds a prefix of the new bytes over the old ones. Whatever the cut
 * point, load must return the old or the new state, never lose both. */
static void test_boot_ctrl_torn_write(void) {
	uint8_t before[HAL_NVM_SIZE];
	uint8_t after[HAL_NVM_SIZE];
	boot_ctrl_t c;
	int lost = 0;

	setup(0xFF);
	CHECK_EQ(boot_ctrl_set_cmd(BOOT_CMD_UPDATE), 0);
	memcpy(before, host_nvm_mem(), HAL_NVM_SIZE);
	CHECK_EQ(boot_ctrl_set_cmd(BOOT_CMD_LOADER), 0);
	memcpy(after, host_nvm_mem(), HAL_NVM_SIZE);

	for (uint32_t k = 0; k <= HAL_NVM_SIZE; k++) {
		memcpy(host_nvm_mem(), before, HAL_NVM_SIZE);
		memcpy(host_nvm_mem(), after, k);
		boot_ctrl_load(&c);
		if (c.cmd != BOOT_CMD_UPDATE && c.cmd != BOOT_CMD_LOADER) {
			lost++;
		}
	}
	CHECK_EQ(lost, 0);
}

/* The record alternates between two slots; the newest one always wins, also
 * after the sequence counter wraps. */
static void test_boot_ctrl_many_saves(void) {
	boot_ctrl_t c;
	int wrong = 0;

	setup(0xFF);
	for (uint32_t i = 1; i <= 600; i++) {
		boot_ctrl_load(&c);
		c.install_count = i;
		c.cmd = (uint8_t)(i % 3);
		if (boot_ctrl_save(&c) != 0) {
			wrong++;
		}
		boot_ctrl_load(&c);
		if (c.install_count != i || c.cmd != (uint8_t)(i % 3)) {
			wrong++;
		}
	}
	CHECK_EQ(wrong, 0);
}

/* Crash log: ring of 8 records, newest first, survives wrap of the ring and
 * of the sequence counter, and never touches boot_ctrl. */
static void test_crash_log_ring(void) {
	crash_rec_t r;
	boot_ctrl_t c;
	int wrong = 0;

	setup(0xFF);
	CHECK_EQ(crash_log_count(), 0);
	CHECK_EQ(crash_log_read(0, &r), 0);
	CHECK_EQ(boot_ctrl_set_cmd(BOOT_CMD_UPDATE), 0);

	for (uint32_t i = 1; i <= 300; i++) {
		memset(&r, 0, sizeof(r));
		r.kind = CRASH_KIND_FATAL;
		r.pc = i;
		if (crash_log_add(&r) != 0) {
			wrong++;
		}
		/* newest first, as many as have been written (max 8) */
		uint32_t expect = (i < CRASH_LOG_SLOTS) ? i : CRASH_LOG_SLOTS;
		if (crash_log_count() != expect) {
			wrong++;
		}
		for (uint32_t n = 0; n < expect; n++) {
			if (!crash_log_read((uint8_t)n, &r) || r.pc != i - n) {
				wrong++;
			}
		}
	}
	CHECK_EQ(wrong, 0);

	boot_ctrl_load(&c);
	CHECK_EQ(c.cmd, BOOT_CMD_UPDATE);

	crash_log_clear();
	CHECK_EQ(crash_log_count(), 0);
	boot_ctrl_load(&c);
	CHECK_EQ(c.cmd, BOOT_CMD_UPDATE);
}

/* A record cut by a power loss (prefix of the new bytes) is skipped: the
 * log shows the older records, never garbage. */
static void test_crash_log_torn_write(void) {
	uint8_t before[HAL_NVM_SIZE];
	uint8_t after[HAL_NVM_SIZE];
	crash_rec_t r;
	int bad = 0;

	setup(0xFF);
	for (uint32_t i = 1; i <= 11; i++) {		/* ring already wrapped */
		memset(&r, 0, sizeof(r));
		r.kind = CRASH_KIND_HARDFAULT;
		r.pc = i;
		crash_log_add(&r);
	}
	memcpy(before, host_nvm_mem(), HAL_NVM_SIZE);
	memset(&r, 0, sizeof(r));
	r.kind = CRASH_KIND_HARDFAULT;
	r.pc = 12;
	crash_log_add(&r);
	memcpy(after, host_nvm_mem(), HAL_NVM_SIZE);

	for (uint32_t k = 0; k <= HAL_NVM_SIZE; k++) {
		memcpy(host_nvm_mem(), before, HAL_NVM_SIZE);
		memcpy(host_nvm_mem(), after, k);
		if (!crash_log_read(0, &r) || (r.pc != 11 && r.pc != 12)) {
			bad++;
		}
		/* every record that is listed is a real one, in order */
		for (uint8_t n = 1; n < crash_log_count(); n++) {
			crash_rec_t o;
			if (!crash_log_read(n, &o) || o.pc != r.pc - n) {
				bad++;
			}
		}
	}
	CHECK_EQ(bad, 0);
}

/* crash_log_capture(): what the previous run left behind becomes one record. */
static void test_crash_log_capture(void) {
	crash_rec_t r;

	setup(0xFF);
	/* normal software reset: nothing to store */
	CHECK_EQ(crash_log_capture(), CRASH_KIND_NONE);
	CHECK_EQ(crash_log_count(), 0);

	/* HardFault while task 2 handled signal 11 */
	host_crash_inject(HAL_CRASH_HARDFAULT, 0, 0x08004321, 0x08001235, 0x00008200);
	host_set_last_dispatch(2, 11);
	CHECK_EQ(crash_log_capture(), CRASH_KIND_HARDFAULT);
	CHECK_EQ(crash_log_read(0, &r), 1);
	CHECK_EQ(r.kind, CRASH_KIND_HARDFAULT);
	CHECK_EQ(r.pc, 0x08004321);
	CHECK_EQ(r.lr, 0x08001235);
	CHECK_EQ(r.info, 0x00008200);
	CHECK_EQ(r.task, 2);
	CHECK_EQ(r.sig, 11);
	/* taken once */
	CHECK_EQ(crash_log_capture(), CRASH_KIND_NONE);

	/* FATAL("MF", 0x31) */
	host_crash_inject(HAL_CRASH_FATAL, 0x31, 0, 0, 0x0000464D);
	CHECK_EQ(crash_log_capture(), CRASH_KIND_FATAL);
	CHECK_EQ(crash_log_read(0, &r), 1);
	CHECK_EQ(r.code, 0x31);
	CHECK_EQ(r.info, 0x0000464D);

	/* watchdog reset while task 3 was stuck in signal 12 */
	host_set_reset_reason(HAL_RESET_REASON_WATCHDOG);
	host_set_last_dispatch(3, 12);
	CHECK_EQ(crash_log_capture(), CRASH_KIND_WATCHDOG);
	CHECK_EQ(crash_log_read(0, &r), 1);
	CHECK_EQ(r.kind, CRASH_KIND_WATCHDOG);
	CHECK_EQ(r.task, 3);
	CHECK_EQ(r.sig, 12);
	CHECK_EQ(crash_log_count(), 3);
}

static void test_boot_runs_valid_app(void) {
	uint32_t n;

	setup(0xFF);
	n = make_image(img_buf, 4000, 1, 0, 0, HOST_BOARD_NAME, 1);
	put_part(FLASH_PART_APP, img_buf, n);
	CHECK_EQ(run_boot(0), BOOT_JUMPED);
	CHECK_EQ(jumped_addr, app_entry());
}

/* App valid, no update pending: boot must not read STAGING at all (a full
 * SPI NOR read on every boot is slow). Garbage in STAGING is irrelevant. */
static void test_boot_skips_staging_when_app_ok(void) {
	uint32_t n;

	setup(0xFF);
	n = make_image(img_buf, 4000, 1, 0, 0, HOST_BOARD_NAME, 1);
	put_part(FLASH_PART_APP, img_buf, n);
	memset(host_part_mem(FLASH_PART_STAGING), 0xA5, 4096);
	host_console_take_tx(0, 1 << 20);
	CHECK_EQ(run_boot(0), BOOT_JUMPED);
	{
		static uint8_t log[8192];
		uint32_t len = host_console_take_tx(log, sizeof(log) - 1);
		log[len] = 0;
		CHECK(strstr((char*)log, "staging: not checked") != 0);
	}
}

static void test_boot_update_flow(void) {
	uint32_t n;
	fw_err_t e;
	boot_ctrl_t c;

	setup(0xFF);
	n = make_image(img_buf, 4000, 1, 0, 0, HOST_BOARD_NAME, 1);
	put_part(FLASH_PART_APP, img_buf, n);
	n = make_image(img_buf, 9000, 2, 0, 0, HOST_BOARD_NAME, 2);
	CHECK_EQ(upload(img_buf, n, 128), FW_OK);
	CHECK_EQ(boot_ctrl_set_cmd(BOOT_CMD_UPDATE), 0);

	CHECK_EQ(run_boot(0), BOOT_JUMPED);
	CHECK_EQ(part_version(FLASH_PART_APP, &e), 2);
	boot_ctrl_load(&c);
	CHECK_EQ(c.cmd, BOOT_CMD_NONE);
	CHECK_EQ(c.install_count, 1);
	CHECK_EQ(c.install_attempts, 0);
	if (host_layout_external()) {
		/* the old image was kept, the new one runs on trial */
		CHECK_EQ(part_version(FLASH_PART_BACKUP, &e), 1);
		CHECK_EQ(c.trial, 1);
		CHECK_EQ(c.trial_boots, 1);
	}
	else {
		CHECK_EQ(c.trial, 0);			/* no backup partition: no trial */
	}

	/* reboot: run directly, no reinstall; one more trial boot */
	CHECK_EQ(run_boot(0), BOOT_JUMPED);
	boot_ctrl_load(&c);
	CHECK_EQ(c.install_count, 1);
	CHECK_EQ(c.trial_boots, host_layout_external() ? 2 : 0);

	/* the app confirms: trial over, boots no longer counted */
	CHECK_EQ(boot_ctrl_confirm(), host_layout_external() ? 1 : 0);
	CHECK_EQ(boot_ctrl_confirm(), 0);
	CHECK_EQ(run_boot(0), BOOT_JUMPED);
	boot_ctrl_load(&c);
	CHECK_EQ(c.trial, 0);
	CHECK_EQ(c.trial_boots, 0);
	CHECK_EQ(part_version(FLASH_PART_APP, &e), 2);
}

/* The new image never confirms itself: after BOOT_MAX_TRIAL_BOOTS boots the
 * bootloader puts the old one back and forgets the rejected image. */
static void test_boot_rollback_unconfirmed(void) {
	uint32_t n;
	fw_err_t e;
	boot_ctrl_t c;

	if (!host_layout_external()) {
		return;
	}
	setup(0xFF);
	n = make_image(img_buf, 4000, 1, 0, 0, HOST_BOARD_NAME, 1);
	put_part(FLASH_PART_APP, img_buf, n);
	n = make_image(img_buf, 9000, 2, 0, 0, HOST_BOARD_NAME, 2);
	CHECK_EQ(upload(img_buf, n, 128), FW_OK);
	boot_ctrl_set_cmd(BOOT_CMD_UPDATE);

	for (int i = 0; i < BOOT_MAX_TRIAL_BOOTS; i++) {
		CHECK_EQ(run_boot(0), BOOT_JUMPED);		/* install + trial boot 1, then 2, 3 */
		CHECK_EQ(part_version(FLASH_PART_APP, &e), 2);
		boot_ctrl_load(&c);
		CHECK_EQ(c.trial_boots, i + 1);
	}
	CHECK_EQ(run_boot(0), BOOT_JUMPED);			/* rollback, then run v1 */
	CHECK_EQ(part_version(FLASH_PART_APP, &e), 1);
	boot_ctrl_load(&c);
	CHECK_EQ(c.cmd, BOOT_CMD_NONE);
	CHECK_EQ(c.trial, 0);
	CHECK_EQ(c.rolled_back, 1);
	CHECK_EQ(c.install_attempts, 0);
	CHECK_EQ(part_version(FLASH_PART_STAGING, &e), 0);	/* rejected image gone */
	CHECK_EQ(e, FW_ERR_MAGIC);

	/* v1 is not on trial: it keeps running, nothing changes */
	CHECK_EQ(run_boot(0), BOOT_JUMPED);
	CHECK_EQ(part_version(FLASH_PART_APP, &e), 1);
	boot_ctrl_load(&c);
	CHECK_EQ(c.rolled_back, 1);
	CHECK_EQ(c.trial, 0);
}

/* The app asks for the previous image (BOOT_CMD_ROLLBACK), e.g. "fw rollback". */
static void test_boot_rollback_on_request(void) {
	uint32_t n;
	fw_err_t e;
	boot_ctrl_t c;

	if (!host_layout_external()) {
		return;
	}
	setup(0xFF);
	n = make_image(img_buf, 4000, 1, 0, 0, HOST_BOARD_NAME, 1);
	put_part(FLASH_PART_APP, img_buf, n);
	n = make_image(img_buf, 9000, 2, 0, 0, HOST_BOARD_NAME, 2);
	CHECK_EQ(upload(img_buf, n, 128), FW_OK);
	boot_ctrl_set_cmd(BOOT_CMD_UPDATE);
	CHECK_EQ(run_boot(0), BOOT_JUMPED);
	CHECK_EQ(boot_ctrl_confirm(), 1);

	CHECK_EQ(boot_ctrl_set_cmd(BOOT_CMD_ROLLBACK), 0);
	CHECK_EQ(run_boot(0), BOOT_JUMPED);
	CHECK_EQ(part_version(FLASH_PART_APP, &e), 1);
	boot_ctrl_load(&c);
	CHECK_EQ(c.cmd, BOOT_CMD_NONE);
	CHECK_EQ(c.rolled_back, 1);

	/* a rollback with nothing to go back to is dropped, the app keeps running */
	memset(host_part_mem(FLASH_PART_BACKUP), 0xFF, 4096);
	CHECK_EQ(boot_ctrl_set_cmd(BOOT_CMD_ROLLBACK), 0);
	CHECK_EQ(run_boot(0), BOOT_JUMPED);
	CHECK_EQ(part_version(FLASH_PART_APP, &e), 1);
	boot_ctrl_load(&c);
	CHECK_EQ(c.cmd, BOOT_CMD_NONE);

	/* an image installed while APP was invalid has no backup: never on trial */
	setup(0xFF);
	n = make_image(img_buf, 9000, 2, 0, 0, HOST_BOARD_NAME, 2);
	put_part(FLASH_PART_STAGING, img_buf, n);
	boot_ctrl_set_cmd(BOOT_CMD_UPDATE);
	for (int i = 0; i < BOOT_MAX_TRIAL_BOOTS + 2; i++) {
		CHECK_EQ(run_boot(0), BOOT_JUMPED);
		CHECK_EQ(part_version(FLASH_PART_APP, &e), 2);
	}
	boot_ctrl_load(&c);
	CHECK_EQ(c.trial, 0);
}

/* APP corrupt, STAGING useless, BACKUP good: the bootloader restores BACKUP. */
static void test_boot_recover_from_backup(void) {
	uint32_t n;
	fw_err_t e;

	if (!host_layout_external()) {
		return;
	}
	setup(0xFF);
	n = make_image(img_buf, 5000, 3, 0, 0, HOST_BOARD_NAME, 9);
	put_part(FLASH_PART_BACKUP, img_buf, n);
	put_part(FLASH_PART_APP, img_buf, n);
	host_part_mem(FLASH_PART_APP)[2000] ^= 0x10;
	memset(host_part_mem(FLASH_PART_STAGING), 0xA5, 4096);
	CHECK_EQ(run_boot(0), BOOT_JUMPED);
	CHECK_EQ(part_version(FLASH_PART_APP, &e), 3);
	CHECK_EQ(jumped_addr, app_entry());
}

/* Trial expired -> rollback, power cut at EVERY flash operation of it: the
 * board always ends up running v1, never a partial image. */
static void test_boot_rollback_power_cut_everywhere(void) {
	static uint8_t old_img[0x10000];
	uint32_t n_old, n_new, total_ops, k;
	int failures = 0;
	fw_err_t e;
	boot_ctrl_t c;

	if (!host_layout_external()) {
		return;
	}
	setup(0xFF);
	n_old = make_image(old_img, 6000, 1, 0, 0, HOST_BOARD_NAME, 11);
	n_new = make_image(img_buf, 7000, 2, 0, 0, HOST_BOARD_NAME, 22);
	put_part(FLASH_PART_APP, img_buf, n_new);
	put_part(FLASH_PART_BACKUP, old_img, n_old);
	put_part(FLASH_PART_STAGING, img_buf, n_new);
	boot_ctrl_load(&c);
	c.trial = 1;
	c.trial_boots = BOOT_MAX_TRIAL_BOOTS;
	boot_ctrl_save(&c);
	{
		uint32_t before = host_flash_ops();
		CHECK_EQ(run_boot(0), BOOT_JUMPED);
		total_ops = host_flash_ops() - before;
	}
	CHECK_EQ(part_version(FLASH_PART_APP, &e), 1);
	CHECK(total_ops > 50);

	for (k = 1; k <= total_ops; k++) {
		int r, boots = 0;

		setup(0xFF);
		put_part(FLASH_PART_APP, img_buf, n_new);
		put_part(FLASH_PART_BACKUP, old_img, n_old);
		put_part(FLASH_PART_STAGING, img_buf, n_new);
		boot_ctrl_load(&c);
		c.trial = 1;
		c.trial_boots = BOOT_MAX_TRIAL_BOOTS;
		boot_ctrl_save(&c);

		r = run_boot(k);
		while (r == BOOT_CUT && boots < 5) {
			r = run_boot(0);
			boots++;
		}
		if (r != BOOT_JUMPED || part_version(FLASH_PART_APP, &e) != 1) {
			failures++;
			if (failures < 5) {
				printf("    cut at op %u: r=%d app=%s\n", k, r, fw_err_str(e));
			}
		}
	}
	printf("    %u cut points tested\n", total_ops);
	CHECK_EQ(failures, 0);
}

/* Power cut at EVERY erase/write of an install. After each cut, reboot until
 * the app runs: it must always be v2, never a partial image. */
static void test_boot_power_cut_everywhere(void) {
	uint32_t n_old, n_new, total_ops, k;
	int failures = 0;
	static uint8_t old_img[0x10000];
	fw_err_t e;

	/* count flash ops of one complete install */
	setup(0xFF);
	n_old = make_image(old_img, 6000, 1, 0, 0, HOST_BOARD_NAME, 11);
	n_new = make_image(img_buf, 7000, 2, 0, 0, HOST_BOARD_NAME, 22);
	put_part(FLASH_PART_APP, old_img, n_old);
	put_part(FLASH_PART_STAGING, img_buf, n_new);
	boot_ctrl_set_cmd(BOOT_CMD_UPDATE);
	{
		uint32_t before = host_flash_ops();
		CHECK_EQ(run_boot(0), BOOT_JUMPED);
		total_ops = host_flash_ops() - before;
	}
	CHECK(total_ops > 100);

	for (k = 1; k <= total_ops; k++) {
		int r, boots = 0;

		setup(0xFF);
		put_part(FLASH_PART_APP, old_img, n_old);
		put_part(FLASH_PART_STAGING, img_buf, n_new);
		boot_ctrl_set_cmd(BOOT_CMD_UPDATE);

		r = run_boot(k);
		while (r == BOOT_CUT && boots < 5) {
			r = run_boot(0);	/* power back on */
			boots++;
		}
		if (r != BOOT_JUMPED || part_version(FLASH_PART_APP, &e) != 2 ||
				(host_layout_external() && part_version(FLASH_PART_BACKUP, &e) != 1)) {
			failures++;
			if (failures < 5) {
				printf("    cut at op %u: r=%d app=%s\n", k, r, fw_err_str(e));
			}
		}
	}
	printf("    %u cut points tested\n", total_ops);
	CHECK_EQ(failures, 0);
}

/* Three installs in a row cut by power loss -> attempts exhausted: enter the
 * loader, NEVER run the partial image (no header, but a sane vector table). */
static void test_boot_attempts_exhausted(void) {
	uint32_t n;
	boot_ctrl_t c;

	setup(0xFF);
	n = make_image(img_buf, 7000, 2, 0, 0, HOST_BOARD_NAME, 22);
	put_part(FLASH_PART_STAGING, img_buf, n);
	boot_ctrl_set_cmd(BOOT_CMD_UPDATE);

	for (int i = 0; i < BOOT_MAX_INSTALL_ATTEMPTS; i++) {
		CHECK_EQ(run_boot(20), BOOT_CUT);
	}
	boot_ctrl_load(&c);
	CHECK_EQ(c.install_attempts, BOOT_MAX_INSTALL_ATTEMPTS);

	/* loader: send RESET to leave the loop */
	inject_frame(FW_PROTO_CMD_RESET, 1, 0, 0);
	CHECK_EQ(run_boot(0), BOOT_RESET);
}

/* Blank board: boot enters the loader, image is uploaded via the protocol
 * (like tools/ak_fw.py), INSTALL -> boot installs and runs it. */
static void test_loader_full_flow(void) {
	uint32_t n, off;
	uint8_t pl[4 + FW_PROTO_MAX_CHUNK];
	uint8_t seq = 0;
	uint8_t rx[4096];
	uint32_t rxn;
	fw_err_t e;

	setup(0xFF);
	n = make_image(img_buf, 3333, 3, 1, 0, HOST_BOARD_NAME, 5);

	inject_frame(FW_PROTO_CMD_INFO, seq++, 0, 0);
	pl[0] = (uint8_t)n; pl[1] = (uint8_t)(n >> 8); pl[2] = (uint8_t)(n >> 16); pl[3] = (uint8_t)(n >> 24);
	inject_frame(FW_PROTO_CMD_BEGIN, seq++, pl, 4);
	for (off = 0; off < n; off += FW_PROTO_MAX_CHUNK) {
		uint32_t len = (n - off < FW_PROTO_MAX_CHUNK) ? n - off : FW_PROTO_MAX_CHUNK;
		pl[0] = (uint8_t)off; pl[1] = (uint8_t)(off >> 8); pl[2] = (uint8_t)(off >> 16); pl[3] = (uint8_t)(off >> 24);
		memcpy(pl + 4, img_buf + off, len);
		inject_frame(FW_PROTO_CMD_DATA, seq++, pl, (uint16_t)(4 + len));
	}
	inject_frame(FW_PROTO_CMD_END, seq++, 0, 0);
	inject_frame(FW_PROTO_CMD_INSTALL, seq++, 0, 0);

	CHECK_EQ(run_boot(0), BOOT_JUMPED);
	CHECK_EQ(part_version(FLASH_PART_APP, &e), 3);

	/* INFO response: role BOOT; every response has status OK */
	rxn = host_console_take_tx(rx, sizeof(rx));
	{
		uint32_t i = 0, frames = 0, bad = 0;
		int saw_info = 0;
		while (i + 7 <= rxn) {
			if (rx[i] != FW_PROTO_SOF) {
				i++;
				continue;
			}
			uint16_t len = (uint16_t)(rx[i + 3] | (rx[i + 4] << 8));
			uint16_t crc = crc16_update(0xFFFF, rx + i + 1, 4U + len);
			if (i + 7 + len > rxn || crc != (uint16_t)(rx[i + 5 + len] | (rx[i + 6 + len] << 8))) {
				i++;
				continue;
			}
			frames++;
			if (rx[i + 5] != FW_OK) {
				bad++;
			}
			if (rx[i + 1] == (FW_PROTO_CMD_INFO | FW_PROTO_RESP)) {
				saw_info = 1;
				CHECK_EQ(rx[i + 7], FW_ROLE_BOOT);
			}
			i += 7U + len;
		}
		CHECK_EQ(frames, (uint32_t)seq);
		CHECK_EQ(bad, 0);
		CHECK(saw_info);
	}
}

/* Bad-CRC frames are silently dropped; interrupted frames are discarded after
 * the timeout; non-protocol text bytes return 0 (go to the shell). */
static void resp_tx(const uint8_t* d, uint32_t n) {
	while (n--) {
		hal_console_putc(*d++);
	}
}

/* application command: echoes its payload behind a marker */
static uint8_t ext_echo(uint8_t cmd, const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t* resp_len) {
	if (cmd != 0x41) {
		return FW_ERR_CMD;
	}
	resp[0] = 0xEE;
	memcpy(&resp[1], req, len);
	*resp_len = (uint16_t)(1 + len);
	return FW_OK;
}

static void test_proto_robustness(void) {
	static const fw_version_t v = { 1, 2, 3, 0, 4 };
	static const fw_proto_cfg_t cfg = { FW_ROLE_APP, &v, resp_tx, 0 };
	static const fw_proto_cfg_t cfg_ext = { FW_ROLE_APP, &v, resp_tx, ext_echo };
	uint8_t f[16];
	uint32_t n;

	setup(0xFF);
	fw_proto_init(&cfg);
	CHECK_EQ(fw_proto_feed('h', 0), 0);
	CHECK_EQ(fw_proto_busy(), 0);

	n = fw_proto_pack(f, FW_PROTO_CMD_INFO, 9, 0, 0);
	f[n - 1] ^= 0xFF;	/* bad crc */
	for (uint32_t i = 0; i < n; i++) {
		CHECK_EQ(fw_proto_feed(f[i], 0), 1);
	}
	CHECK_EQ(host_console_take_tx(0, 1000), 0);

	/* interrupted frame: 3 bytes, silence > timeout, next frame still accepted */
	n = fw_proto_pack(f, FW_PROTO_CMD_INFO, 10, 0, 0);
	fw_proto_feed(f[0], 0);
	fw_proto_feed(f[1], 1);
	fw_proto_feed(f[2], 2);
	for (uint32_t i = 0; i < n; i++) {
		fw_proto_feed(f[i], 1000 + i);
	}
	CHECK(host_console_take_tx(0, 1000) > 0);

	/* unknown command */
	n = fw_proto_pack(f, 0x55, 11, 0, 0);
	for (uint32_t i = 0; i < n; i++) {
		fw_proto_feed(f[i], 2000);
	}
	{
		uint8_t r[16];
		CHECK_EQ(host_console_take_tx(r, sizeof(r)), 8);
		CHECK_EQ(r[1], 0x55 | FW_PROTO_RESP);
		CHECK_EQ(r[5], FW_ERR_CMD);
	}

	/* INSTALL without a verified image -> STATE, no action */
	n = fw_proto_pack(f, FW_PROTO_CMD_INSTALL, 12, 0, 0);
	for (uint32_t i = 0; i < n; i++) {
		fw_proto_feed(f[i], 3000);
	}
	CHECK_EQ(fw_proto_take_action(), FW_PROTO_ACT_NONE);
	host_console_take_tx(0, 1000);

	/* commands from 0x40 go to the application, if it asked for them */
	{
		static const uint8_t arg[3] = { 1, 2, 3 };
		uint8_t r[24];

		n = fw_proto_pack(f, 0x41, 13, arg, sizeof(arg));
		for (uint32_t i = 0; i < n; i++) {
			fw_proto_feed(f[i], 4000);
		}
		CHECK_EQ(host_console_take_tx(r, sizeof(r)), 8);		/* no handler: unknown command */
		CHECK_EQ(r[5], FW_ERR_CMD);

		fw_proto_init(&cfg_ext);
		for (uint32_t i = 0; i < n; i++) {
			fw_proto_feed(f[i], 5000);
		}
		CHECK_EQ(host_console_take_tx(r, sizeof(r)), 12);		/* status + marker + 3 bytes */
		CHECK_EQ(r[1], 0x41 | FW_PROTO_RESP);
		CHECK_EQ(r[5], FW_OK);
		CHECK_EQ(r[6], 0xEE);
		CHECK(memcmp(&r[7], arg, sizeof(arg)) == 0);

		n = fw_proto_pack(f, 0x42, 14, 0, 0);					/* the handler does not know this one */
		for (uint32_t i = 0; i < n; i++) {
			fw_proto_feed(f[i], 6000);
		}
		CHECK_EQ(host_console_take_tx(r, sizeof(r)), 8);
		CHECK_EQ(r[5], FW_ERR_CMD);

		n = fw_proto_pack(f, FW_PROTO_CMD_INFO, 15, 0, 0);		/* built-in commands are not affected */
		for (uint32_t i = 0; i < n; i++) {
			fw_proto_feed(f[i], 7000);
		}
		CHECK(host_console_take_tx(r, sizeof(r)) > 8);
	}
}

/* APP has a header but bad CRC and STAGING holds an image: boot must recover,
 * not treat it as "raw" and run it. */
static void test_boot_recover_corrupt_app(void) {
	uint32_t n;
	fw_err_t e;

	setup(0xFF);
	n = make_image(img_buf, 5000, 4, 0, 0, HOST_BOARD_NAME, 9);
	put_part(FLASH_PART_APP, img_buf, n);
	put_part(FLASH_PART_STAGING, img_buf, n);
	host_part_mem(FLASH_PART_APP)[2000] ^= 0x10;
	CHECK_EQ(part_version(FLASH_PART_APP, &e), 0);
	CHECK_EQ(e, FW_ERR_IMG_CRC);

	CHECK_EQ(run_boot(0), BOOT_JUMPED);
	CHECK_EQ(part_version(FLASH_PART_APP, &e), 4);
}

/* Recovery path (cmd NONE, APP corrupt, STAGING good) cut at every step:
 * must never jump into a partial image. */
static void test_boot_recover_power_cut_everywhere(void) {
	static uint8_t bad_app[0x10000];
	uint32_t n, total_ops, k;
	int failures = 0;
	fw_err_t e;

	setup(0xFF);
	n = make_image(img_buf, 5000, 4, 0, 0, HOST_BOARD_NAME, 9);
	memcpy(bad_app, img_buf, n);
	bad_app[3000] ^= 0x10;
	put_part(FLASH_PART_APP, bad_app, n);
	put_part(FLASH_PART_STAGING, img_buf, n);
	{
		uint32_t before = host_flash_ops();
		CHECK_EQ(run_boot(0), BOOT_JUMPED);
		total_ops = host_flash_ops() - before;
	}

	for (k = 1; k <= total_ops; k++) {
		int r, boots = 0;

		setup(0xFF);
		put_part(FLASH_PART_APP, bad_app, n);
		put_part(FLASH_PART_STAGING, img_buf, n);
		r = run_boot(k);
		while (r == BOOT_CUT && boots < 5) {
			r = run_boot(0);
			boots++;
		}
		if (r != BOOT_JUMPED || part_version(FLASH_PART_APP, &e) != 4) {
			failures++;
		}
	}
	printf("    %u cut points tested\n", total_ops);
	CHECK_EQ(failures, 0);
}

/*----------------------------------------------------------------------------
 * flog: record ring on a RAM "NOR" with power-cut injection
 *--------------------------------------------------------------------------*/
#define FL_SECTOR		(512U)
#define FL_SECTORS		(4)
#define FL_BASE			(1024U)
static uint8_t fl_mem[FL_BASE + FL_SECTOR * FL_SECTORS];
static uint32_t fl_ops, fl_cut_at;
static jmp_buf fl_cut_jmp;

typedef struct {
	uint32_t n;			/* sequence number of the record */
	uint8_t  pad[5];
} fl_rec_t;				/* 9 B: an odd size, slot 11 B */

static void fl_tick(void) {
	fl_ops++;
	if (fl_cut_at && fl_ops >= fl_cut_at) {
		longjmp(fl_cut_jmp, 1);
	}
}
static uint8_t fl_read(uint32_t off, void* buf, uint32_t len) {
	if (off + len > sizeof(fl_mem)) return 0;
	memcpy(buf, fl_mem + off, len);
	return 1;
}
static uint8_t fl_write(uint32_t off, const void* data, uint32_t len) {
	if (off + len > sizeof(fl_mem)) return 0;
	/* a cut lands in the middle of the write: the first half is on flash */
	if (fl_cut_at && fl_ops + 1 >= fl_cut_at) {
		for (uint32_t i = 0; i < len / 2; i++) fl_mem[off + i] &= ((const uint8_t*)data)[i];
	}
	fl_tick();
	for (uint32_t i = 0; i < len; i++) fl_mem[off + i] &= ((const uint8_t*)data)[i];
	return 1;
}
static uint8_t fl_erase(uint32_t off) {
	if (off % FL_SECTOR || off + FL_SECTOR > sizeof(fl_mem)) return 0;
	if (fl_cut_at && fl_ops + 1 >= fl_cut_at) {
		memset(fl_mem + off, 0xFF, FL_SECTOR / 2);	/* half erased */
	}
	fl_tick();
	memset(fl_mem + off, 0xFF, FL_SECTOR);
	return 1;
}
static const flog_ops_t fl_ops_tbl = { fl_read, fl_write, fl_erase };

static int fl_mount(flog_t* l) {
	return flog_mount(l, &fl_ops_tbl, FL_BASE, FL_SECTOR, FL_SECTORS, sizeof(fl_rec_t));
}

/* every readable record is in order; returns the number of torn ones */
static int fl_check_order(const flog_t* l, uint32_t* last_n) {
	fl_rec_t r;
	uint32_t prev = 0;
	int torn = 0;

	for (uint32_t i = 0; i < flog_count(l); i++) {
		int ok = flog_read(l, i, &r);
		if (ok == 1) {
			CHECK(prev == 0 || r.n > prev);
			prev = r.n;
		}
		else {
			CHECK_EQ(ok, 0);
			torn++;
		}
	}
	*last_n = prev;
	return torn;
}

static void test_flog_ring(void) {
	flog_t l;
	fl_rec_t r;
	uint32_t per, last;

	memset(fl_mem, 0x00, sizeof(fl_mem));		/* a foreign, unformatted region */
	fl_ops = fl_cut_at = 0;
	CHECK_EQ(fl_mount(&l), 0);
	CHECK_EQ(flog_count(&l), 0);
	CHECK_EQ(flog_read(&l, 0, &r), -1);
	per = l.per_sector;
	CHECK_EQ(per, (FL_SECTOR - FLOG_HDR_SIZE) / (sizeof(fl_rec_t) + 2));

	/* fill one sector and a bit, remount: everything back */
	for (uint32_t n = 1; n <= per + 3; n++) {
		memset(&r, 0, sizeof(r));
		r.n = n;
		CHECK_EQ(flog_append(&l, &r), 0);
	}
	CHECK_EQ(flog_count(&l), per + 3);
	CHECK_EQ(fl_mount(&l), 0);
	CHECK_EQ(flog_count(&l), per + 3);
	CHECK_EQ(l.used, 2);
	CHECK_EQ(flog_read(&l, 0, &r), 1);
	CHECK_EQ(r.n, 1);
	CHECK_EQ(flog_read(&l, per + 2, &r), 1);
	CHECK_EQ(r.n, per + 3);
	CHECK_EQ(fl_check_order(&l, &last), 0);

	/* wrap the ring several times: the oldest sector goes, order is kept */
	for (uint32_t n = per + 4; n <= 10 * per; n++) {
		r.n = n;
		CHECK_EQ(flog_append(&l, &r), 0);
	}
	CHECK_EQ(l.used, FL_SECTORS);
	CHECK(flog_count(&l) > (FL_SECTORS - 1) * per && flog_count(&l) <= FL_SECTORS * per);
	CHECK_EQ(fl_check_order(&l, &last), 0);
	CHECK_EQ(last, 10 * per);
	CHECK_EQ(fl_mount(&l), 0);
	CHECK_EQ(fl_check_order(&l, &last), 0);
	CHECK_EQ(last, 10 * per);
	CHECK_EQ(flog_read(&l, 0, &r), 1);
	CHECK_EQ(r.n, 10 * per - flog_count(&l) + 1);

	/* clear: every header is gone, so the ring starts over */
	CHECK_EQ(flog_clear(&l), 0);
	CHECK_EQ(flog_count(&l), 0);
	CHECK_EQ(fl_mount(&l), 0);
	CHECK_EQ(flog_count(&l), 0);
	r.n = 1;
	CHECK_EQ(flog_append(&l, &r), 0);
	CHECK_EQ(fl_mount(&l), 0);
	CHECK_EQ(flog_count(&l), 1);
	CHECK_EQ(l.head_seq, 1);

	/* bad arguments */
	CHECK_EQ(flog_mount(&l, &fl_ops_tbl, FL_BASE, FL_SECTOR, 1, 9), -1);
	CHECK_EQ(flog_mount(&l, &fl_ops_tbl, FL_BASE, FL_SECTOR, 4, 0), -1);
	CHECK_EQ(flog_mount(&l, &fl_ops_tbl, FL_BASE, 16, 4, 9), -1);
}

/* Power cut at every flash operation while the ring wraps twice: after each
 * cut the log mounts, keeps its order, loses at most the record being
 * written, and goes on taking records. */
static void test_flog_power_cut_everywhere(void) {
	static uint8_t snapshot[sizeof(fl_mem)];
	flog_t l;
	fl_rec_t r;
	uint32_t total_ops, k, last, next_n;
	int torn_total = 0, failures = 0;

	memset(fl_mem, 0xFF, sizeof(fl_mem));
	fl_ops = fl_cut_at = 0;
	CHECK_EQ(fl_mount(&l), 0);
	memcpy(snapshot, fl_mem, sizeof(fl_mem));
	next_n = 1;
	for (uint32_t n = 1; n <= 2 * FL_SECTORS * l.per_sector; n++) {
		memset(&r, 0, sizeof(r));
		r.n = n;
		flog_append(&l, &r);
	}
	total_ops = fl_ops;
	CHECK(total_ops > 100);

	for (k = 1; k <= total_ops; k++) {
		uint32_t before;

		memcpy(fl_mem, snapshot, sizeof(fl_mem));
		fl_ops = 0;
		fl_cut_at = k;
		CHECK_EQ(fl_mount(&l), 0);
		if (setjmp(fl_cut_jmp) == 0) {
			for (uint32_t n = 1; ; n++) {
				memset(&r, 0, sizeof(r));
				r.n = n;
				flog_append(&l, &r);
			}
		}
		fl_cut_at = 0;
		/* power back on */
		if (fl_mount(&l) != 0) {
			failures++;
			continue;
		}
		torn_total += fl_check_order(&l, &last);
		before = flog_count(&l);
		/* the log keeps working: 2 more records, both readable */
		r.n = last + 1;
		if (flog_append(&l, &r) != 0 || flog_append(&l, &r) != 0 || flog_count(&l) < 2 ||
				flog_read(&l, flog_count(&l) - 1, &r) != 1 || r.n != last + 1 ||
				flog_read(&l, flog_count(&l) - 2, &r) != 1 || r.n != last + 1) {
			failures++;
		}
		(void)before;
		(void)next_n;
	}
	printf("    %u cut points tested, %d torn records seen\n", total_ops, torn_total);
	CHECK_EQ(failures, 0);
	CHECK(torn_total > 0);		/* the cuts really land inside writes */
}

TT_MAIN_BEGIN("test_fw")
	RUN_TEST(test_crc);
	RUN_TEST(test_flog_ring);
	RUN_TEST(test_flog_power_cut_everywhere);
	RUN_TEST(test_boot_decide);
	RUN_TEST(test_proto_robustness);

	/* Layout-dependent tests run twice: staging on emulated SPI NOR (4K
	 * sectors, byte writes) and staging in internal flash (256 B pages). */
	for (int layout = 1; layout >= 0; layout--) {
		host_set_layout((uint8_t)layout);
		printf(" [staging: %s]\n", layout ? "external SPI NOR" : "internal flash");
		RUN_TEST(test_upload_ok);
		RUN_TEST(test_upload_errors);
		RUN_TEST(test_boot_ctrl);
		RUN_TEST(test_boot_ctrl_torn_write);
		RUN_TEST(test_boot_ctrl_many_saves);
		RUN_TEST(test_crash_log_ring);
		RUN_TEST(test_crash_log_torn_write);
		RUN_TEST(test_crash_log_capture);
		RUN_TEST(test_boot_runs_valid_app);
		RUN_TEST(test_boot_skips_staging_when_app_ok);
		RUN_TEST(test_boot_update_flow);
		RUN_TEST(test_boot_rollback_unconfirmed);
		RUN_TEST(test_boot_rollback_on_request);
		RUN_TEST(test_boot_recover_from_backup);
		RUN_TEST(test_boot_rollback_power_cut_everywhere);
		RUN_TEST(test_boot_recover_corrupt_app);
		RUN_TEST(test_loader_full_flow);
		RUN_TEST(test_boot_attempts_exhausted);
		RUN_TEST(test_boot_power_cut_everywhere);
		RUN_TEST(test_boot_recover_power_cut_everywhere);
	}
TT_MAIN_END()
