/**
 * Bootloader loader on the second link (BOOT_AUX_LOADER=1, hal_rs485), on the
 * host: the window after reset, LOADER there -> loader on that link, full
 * upload + install over it, idle exit, and that nothing else changes the boot.
 * Built with BOOT_AUX_LOADER=1 and a short idle time (CMake: test_boot_aux).
 */
#include <string.h>
#include <setjmp.h>

#include "hal.h"
#include "hal_rs485.h"
#include "crc.h"
#include "boot_ctrl.h"
#include "fw_image.h"
#include "fw_update.h"
#include "fw_proto.h"
#include "fw_sign.h"
#include "port_host.h"
#include "tiny_test.h"

extern void boot_main(void);

static uint8_t img_buf[0x4000];

static uint32_t make_image(uint8_t* out, uint32_t img_size, uint8_t maj, uint32_t seed) {
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
	h->version.build = seed;
	strncpy(h->board, HOST_BOARD_NAME, FW_IMAGE_BOARD_LEN);
	h->hdr_crc32 = crc32_update(CRC32_INIT, h, FW_IMAGE_HDR_SIZE - 4);
	return FW_IMAGE_HDR_SIZE + img_size;
}

static uint8_t app_major(void) {
	fw_image_hdr_t h;
	return fw_image_verify(FLASH_PART_APP, &h) == FW_OK ? h.version.major : 0;
}

/* a board running app v1 (valid image in APP, nothing pending) */
static void setup_with_app(void) {
	uint32_t n;

	host_reset_state(0xFF);
	host_use_virtual_time(1);
	host_fatal_jmp = 0;
	hal_init();
	fw_update_abort();
	fw_image_sign_require(0);
	host_rs485_keep_rx_on_init(0);
	hal_rs485_init(115200);			/* drop what an earlier test left in the queues */
	host_rs485_keep_rx_on_init(1);
	n = make_image(img_buf, 2000, 1, 1);
	memcpy(host_part_mem(FLASH_PART_APP), img_buf, n);
	{ uint8_t junk[4096]; host_console_take_tx(junk, sizeof(junk)); }
}

enum { BOOT_JUMPED = 1, BOOT_RESET = 2, BOOT_FATAL = 3 };
static jmp_buf boot_jmp, fatal_jmp;

static void on_jump(uint32_t a) { (void)a; longjmp(boot_jmp, BOOT_JUMPED); }
static void on_reset(void) { longjmp(boot_jmp, BOOT_RESET); }

static int run_boot(void) {
	volatile int r;

	host_jump_handler = on_jump;
	host_reset_handler = on_reset;
	host_fatal_jmp = &fatal_jmp;
	if ((r = setjmp(boot_jmp)) != 0) {
		goto out;
	}
	if (setjmp(fatal_jmp) != 0) {
		r = BOOT_FATAL;
		goto out;
	}
	boot_main();
	r = 0;
out:
	host_fatal_jmp = 0;
	return r;
}

static void aux_frame(uint8_t cmd, uint8_t seq, const uint8_t* payload, uint16_t len) {
	uint8_t f[FW_PROTO_MAX_PAYLOAD + 8];
	host_rs485_inject(f, fw_proto_pack(f, cmd, seq, payload, len));
}

/* Responses the bootloader sent on the aux link: count frames, check status. */
static uint32_t aux_responses(uint8_t want_cmd, uint8_t* status_out, uint8_t* role_out) {
	uint8_t rx[4096];
	uint32_t n = host_rs485_take_tx(rx, sizeof(rx)), i = 0, frames = 0;

	while (i + 7 <= n) {
		uint16_t len, crc;
		if (rx[i] != FW_PROTO_SOF) {
			i++;
			continue;
		}
		len = (uint16_t)(rx[i + 3] | (rx[i + 4] << 8));
		crc = crc16_update(0xFFFF, rx + i + 1, 4U + len);
		if (i + 7 + len > n || crc != (uint16_t)(rx[i + 5 + len] | (rx[i + 6 + len] << 8))) {
			i++;
			continue;
		}
		frames++;
		if (rx[i + 1] == (want_cmd | FW_PROTO_RESP)) {
			if (status_out) *status_out = rx[i + 5];
			if (role_out && len >= 3) *role_out = rx[i + 7];
		}
		i += 7U + len;
	}
	return frames;
}

static int console_has(const char* s) {
	static char out[8192];
	uint32_t n = host_console_take_tx((uint8_t*)out, sizeof(out) - 1);
	out[n] = 0;
	return strstr(out, s) != 0;
}

/*--------------------------------------------------------------------------*/
static void test_quiet_link_runs_app(void) {
	uint32_t t0;

	setup_with_app();
	t0 = hal_millis();
	CHECK_EQ(run_boot(), BOOT_JUMPED);
	CHECK(hal_millis() - t0 >= BOOT_AUX_WINDOW_MS);			/* waited the window */
	CHECK(hal_millis() - t0 < BOOT_AUX_WINDOW_MS + 50);		/* ... and not longer */
	CHECK_EQ(aux_responses(0, 0, 0), 0);
	CHECK_EQ(host_rs485_baud(), BOOT_AUX_BAUD);
}

/* INFO polls of an OTA waiting for its install: answered (role BOOT), no loader */
static void test_info_in_window_is_answered_only(void) {
	uint8_t st = 0xEE, role = 0xEE;

	setup_with_app();
	aux_frame(FW_PROTO_CMD_INFO, 1, 0, 0);
	CHECK_EQ(run_boot(), BOOT_JUMPED);
	CHECK_EQ(aux_responses(FW_PROTO_CMD_INFO, &st, &role), 1);
	CHECK_EQ(st, FW_OK);
	CHECK_EQ(role, FW_ROLE_BOOT);
	CHECK(!console_has("loader mode"));
}

/* the gateway's ASCII protocol on the same wire changes nothing */
static void test_ascii_traffic_ignored(void) {
	static const char t[] = "<STATUS_?><SW_A_?>\r\n<PING>";

	setup_with_app();
	host_rs485_inject((const uint8_t*)t, sizeof(t) - 1);
	CHECK_EQ(run_boot(), BOOT_JUMPED);
	CHECK_EQ(aux_responses(0, 0, 0), 0);
	CHECK_EQ(app_major(), 1);
}

/* LOADER in the window, then a whole upload on the aux link: v1 -> v4 */
static void test_loader_upload_install_over_aux(void) {
	uint8_t pl[4 + FW_PROTO_MAX_CHUNK], seq = 0, st = 0xEE, role = 0xEE;
	uint32_t n, off;

	setup_with_app();
	n = make_image(img_buf, 1200, 4, 9);
	aux_frame(FW_PROTO_CMD_LOADER, seq++, 0, 0);
	aux_frame(FW_PROTO_CMD_INFO, seq++, 0, 0);
	pl[0] = (uint8_t)n; pl[1] = (uint8_t)(n >> 8); pl[2] = (uint8_t)(n >> 16); pl[3] = (uint8_t)(n >> 24);
	aux_frame(FW_PROTO_CMD_BEGIN, seq++, pl, 4);
	for (off = 0; off < n; off += FW_PROTO_MAX_CHUNK) {
		uint32_t len = (n - off < FW_PROTO_MAX_CHUNK) ? n - off : FW_PROTO_MAX_CHUNK;
		pl[0] = (uint8_t)off; pl[1] = (uint8_t)(off >> 8); pl[2] = (uint8_t)(off >> 16); pl[3] = (uint8_t)(off >> 24);
		memcpy(pl + 4, img_buf + off, len);
		aux_frame(FW_PROTO_CMD_DATA, seq++, pl, (uint16_t)(4 + len));
	}
	aux_frame(FW_PROTO_CMD_END, seq++, 0, 0);
	aux_frame(FW_PROTO_CMD_INSTALL, seq++, 0, 0);

	CHECK_EQ(run_boot(), BOOT_JUMPED);
	CHECK_EQ(app_major(), 4);
	CHECK(console_has("loader mode on aux link"));
	CHECK_EQ(aux_responses(FW_PROTO_CMD_INFO, &st, &role), (uint32_t)seq);	/* every frame answered */
	CHECK_EQ(st, FW_OK);
	CHECK_EQ(role, FW_ROLE_BOOT);
}

/* LOADER, then the host goes away: leave the loader after the idle time, run v1 */
static void test_loader_idle_exit(void) {
	uint8_t st = 0xEE;
	uint32_t t0;

	setup_with_app();
	aux_frame(FW_PROTO_CMD_LOADER, 7, 0, 0);
	t0 = hal_millis();
	CHECK_EQ(run_boot(), BOOT_JUMPED);
	CHECK_EQ(aux_responses(FW_PROTO_CMD_LOADER, &st, 0), 1);
	CHECK_EQ(st, FW_OK);
	CHECK(hal_millis() - t0 >= BOOT_AUX_IDLE_MS);
	CHECK(console_has("idle"));
	CHECK_EQ(app_major(), 1);
}

TT_MAIN_BEGIN("test_boot_aux")
	RUN_TEST(test_quiet_link_runs_app);
	RUN_TEST(test_info_in_window_is_answered_only);
	RUN_TEST(test_ascii_traffic_ignored);
	RUN_TEST(test_loader_upload_install_over_aux);
	RUN_TEST(test_loader_idle_exit);
TT_MAIN_END()
