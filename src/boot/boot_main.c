/**
 ******************************************************************************
 * @brief:  Bootloader main flow (chip independent).
 *
 *   load boot_ctrl -> verify APP, STAGING, BACKUP -> boot_decide()
 *     RUN      : jump to app (an unconfirmed image: count the trial boot)
 *     INSTALL  : copy APP -> BACKUP, STAGING -> APP, update boot_ctrl, loop
 *     ROLLBACK : copy BACKUP -> APP (the image before the last install), loop
 *     LOADER   : receive an image on the console via fw_proto (tools/ak_fw.py)
 *
 *   BOOT_AUX_LOADER=1 (opt-in, the board must link hal_rs485): after every
 *   reset, listen on the second link for BOOT_AUX_WINDOW_MS first; a LOADER
 *   frame there keeps the bootloader in loader mode ON THAT LINK. Lets a host
 *   wired to that link (e.g. a gateway on USART2) reflash a broken app
 *   without the console or the ROM bootloader.
 ******************************************************************************
**/

#include <string.h>

#include "hal.h"
#include "ak_log.h"
#include "boot_core.h"
#include "boot_ctrl.h"
#include "fw_image.h"
#include "fw_update.h"
#include "fw_proto.h"

#ifndef BOOT_VER_MAJOR
#define BOOT_VER_MAJOR		1
#define BOOT_VER_MINOR		4
#define BOOT_VER_PATCH		0
#endif

/* Loader on the second link (see the header). Off by default: the base and
 * every board without a host on hal_rs485 build exactly as before.
 * Window: why 300 ms - long enough for a host that pulses NRST and then sends
 * LOADER every ~30 ms, short enough not to matter at power-on. Any frame other
 * than LOADER (INFO polls of an OTA waiting for the install) is answered and
 * ignored; ASCII traffic on the link is ignored.
 * Idle: in loader mode on that link, no frame for BOOT_AUX_IDLE_MS -> leave
 * the loader (run the app if it is valid), so a host that died half-way does
 * not leave the board parked. */
#ifndef BOOT_AUX_LOADER
#define BOOT_AUX_LOADER		0
#endif
#ifndef BOOT_AUX_WINDOW_MS
#define BOOT_AUX_WINDOW_MS	(300)
#endif
#ifndef BOOT_AUX_IDLE_MS
#define BOOT_AUX_IDLE_MS	(60000)
#endif
#ifndef BOOT_AUX_BAUD
#define BOOT_AUX_BAUD		(115200)
#endif

#if BOOT_AUX_LOADER
#include "hal_rs485.h"
#endif

/* 0 (default): run CRC-checked images only. 1: also run an APP without
 * header (raw .bin flashed over SWD while debugging). */
#ifndef BOOT_ALLOW_RAW_APP
#define BOOT_ALLOW_RAW_APP	0
#endif

/* Bootloader watchdog: a hang (bus, flash) ends in a reset instead of a dead
 * board; repeated failed installs are then stopped by the attempt counter.
 * Kicked per copied page, per SPI NOR wait and in the loader loop. The
 * longest stretch without a kick is a full image verify (well below 1 s).
 * The app gets a stopped watchdog again: the jump goes through a reset. */
#ifndef BOOT_WDT_TIMEOUT_MS
#define BOOT_WDT_TIMEOUT_MS	(10000)
#endif

#define TAG "BOOT"

static const fw_version_t boot_version = { BOOT_VER_MAJOR, BOOT_VER_MINOR, BOOT_VER_PATCH, 0, 0 };

static void console_tx(const uint8_t* data, uint32_t len) {
	while (len--) {
		hal_console_putc(*data++);
	}
}

static const fw_proto_cfg_t proto_cfg = {
	FW_ROLE_BOOT,
	&boot_version,
	console_tx,
	0,						/* no application commands in the bootloader */
};

/* Where the loader talks: the console, or (BOOT_AUX_LOADER) the second link. */
typedef struct {
	const char* name;
	int (*getc)(void);
	void (*flush)(void);
	const fw_proto_cfg_t* cfg;
	uint32_t idle_ms;		/* 0: wait forever */
} loader_io_t;

static const loader_io_t console_io = { "console", hal_console_getc, hal_console_flush, &proto_cfg, 0 };

#if BOOT_AUX_LOADER
static void aux_tx(const uint8_t* data, uint32_t len) {
	hal_rs485_write(data, len);		/* returns once the last stop bit is out */
}

static void aux_flush(void) {
}

static const fw_proto_cfg_t aux_cfg = {
	FW_ROLE_BOOT,
	&boot_version,
	aux_tx,
	0,
};

static const loader_io_t aux_io = { "aux link", hal_rs485_getc, aux_flush, &aux_cfg, BOOT_AUX_IDLE_MS };

/* 1 if a LOADER frame arrived on the second link within the window. */
static uint8_t aux_window(void) {
	uint32_t t0;
	int c;

	hal_rs485_init(BOOT_AUX_BAUD);
	fw_proto_init(&aux_cfg);
	t0 = hal_millis();
	while (hal_millis() - t0 < BOOT_AUX_WINDOW_MS) {
		uint8_t got = 0;

		/* byte by byte: stop right after the LOADER frame, the bytes behind it
		 * (the upload that follows) are for the loader, and a later frame must
		 * not overwrite the pending action. Other actions are dropped. */
		while ((c = hal_rs485_getc()) >= 0) {
			fw_proto_feed((uint8_t)c, hal_millis());
			got = 1;
			if (fw_proto_take_action() == FW_PROTO_ACT_LOADER) {
				return 1;
			}
		}
		hal_wdt_kick();
		if (!got) {
			hal_delay_ms(1);	/* RX is interrupt-buffered; also lets virtual time run on the host */
		}
	}
	return 0;
}
#endif

/* Marker for "not verified" (STAGING is skipped when the app is fine). */
#define FW_NOT_CHECKED		((fw_err_t)0xFF)

static void print_ver(const char* name, const fw_image_hdr_t* hdr, fw_err_t err) {
	if (err == FW_NOT_CHECKED) {
		LOG_I(TAG, "%s: not checked\n", name);
	}
	else if (err == FW_OK) {
		LOG_I(TAG, "%s: v%d.%d.%d build %u, %u bytes\n", name,
			  hdr->version.major, hdr->version.minor, hdr->version.patch,
			  hdr->version.build, hdr->img_size);
	}
	else {
		LOG_I(TAG, "%s: %s\n", name, fw_err_str(err));
	}
}

/* Returns FW_PROTO_ACT_INSTALL (new image received + verified), FW_PROTO_ACT_RUN
 * (also: io->idle_ms without a frame) or FW_PROTO_ACT_ROLLBACK. */
static fw_proto_action_t loader_loop(const loader_io_t* io) {
	uint32_t last_blink = 0;
	uint32_t last_rx = hal_millis();
	int c;

	LOG_I(TAG, "loader mode on %s - send firmware with tools/ak_fw.py\n", io->name);

	fw_update_abort();
	fw_proto_init(io->cfg);

	for (;;) {
		uint8_t got = 0;

		while ((c = io->getc()) >= 0) {
			if (fw_proto_feed((uint8_t)c, hal_millis())) {
				last_rx = hal_millis();
			}
			got = 1;
		}
		if (io->idle_ms && !got) {
			hal_delay_ms(1);	/* idle timer: lets virtual time run on the host */
		}

		switch (fw_proto_take_action()) {
		case FW_PROTO_ACT_INSTALL:
			io->flush();
			return FW_PROTO_ACT_INSTALL;

		case FW_PROTO_ACT_RUN:
			io->flush();
			return FW_PROTO_ACT_RUN;

		case FW_PROTO_ACT_ROLLBACK:
			io->flush();
			return FW_PROTO_ACT_ROLLBACK;

		case FW_PROTO_ACT_RESET:
			io->flush();
			hal_reset();
			break;

		default:
			break;
		}

		if (io->idle_ms && hal_millis() - last_rx >= io->idle_ms) {
			LOG_W(TAG, "loader on %s idle %u ms - leaving\n", io->name, (unsigned)io->idle_ms);
			fw_update_abort();
			return FW_PROTO_ACT_RUN;
		}

		if (hal_millis() - last_blink >= 100) {
			last_blink = hal_millis();
			hal_led_toggle();
		}
		hal_wdt_kick();
	}
}

void boot_main(void) {
	const flash_part_info_t* app = hal_flash_info(FLASH_PART_APP);
	boot_ctrl_t ctrl;
	boot_state_t st;
	fw_image_hdr_t app_hdr, stg_hdr, bak_hdr;
	fw_err_t app_err, stg_err, bak_err;
	boot_act_t act;
	uint8_t aux_req = 0;		/* LOADER arrived on the second link */

	xprintf("\n");
	LOG_I(TAG, "ak-mcu-base bootloader v%d.%d.%d, board %s, reset reason %d\n",
		  BOOT_VER_MAJOR, BOOT_VER_MINOR, BOOT_VER_PATCH, hal_board_name(), hal_reset_reason());

	hal_wdt_start(BOOT_WDT_TIMEOUT_MS);

#if BOOT_AUX_LOADER
	aux_req = aux_window();
	if (aux_req) {
		LOG_I(TAG, "LOADER on aux link\n");
	}
#endif

	boot_ctrl_load(&ctrl);

	for (;;) {
		uint8_t trial_over = ctrl.trial && ctrl.trial_boots >= BOOT_MAX_TRIAL_BOOTS;

		hal_wdt_kick();
		app_err = fw_image_verify(FLASH_PART_APP, &app_hdr);

		/* STAGING only matters for an update request or a broken app, BACKUP
		 * for a rollback or a broken app: skip the full reads otherwise (slow
		 * on SPI NOR, done on every boot). */
		if (ctrl.cmd == BOOT_CMD_UPDATE || app_err != FW_OK) {
			stg_err = fw_image_verify(FLASH_PART_STAGING, &stg_hdr);
		}
		else {
			stg_err = FW_NOT_CHECKED;
		}
		if (ctrl.cmd == BOOT_CMD_ROLLBACK || trial_over || app_err != FW_OK) {
			bak_err = fw_image_verify(FLASH_PART_BACKUP, &bak_hdr);
		}
		else {
			bak_err = FW_NOT_CHECKED;
		}

		memset(&st, 0, sizeof(st));
		st.cmd = ctrl.cmd;
		st.install_attempts = ctrl.install_attempts;
		st.trial = ctrl.trial;
		st.trial_boots = ctrl.trial_boots;
		st.app_ok = (app_err == FW_OK);
		st.staging_ok = (stg_err == FW_OK);
		st.backup_ok = (bak_err == FW_OK);
		if (BOOT_ALLOW_RAW_APP && app_err == FW_ERR_MAGIC) {
			uint32_t vec[2];
			if (hal_flash_read(FLASH_PART_APP, FW_IMAGE_HDR_SIZE, vec, sizeof(vec)) == HAL_FLASH_OK) {
				st.app_raw_ok = hal_vector_ok(vec[0], vec[1]);
			}
		}

		print_ver("app    ", &app_hdr, app_err);
		print_ver("staging", &stg_hdr, stg_err);
		print_ver("backup ", &bak_hdr, bak_err);

		act = boot_decide(&st);
		LOG_I(TAG, "cmd %d, attempts %d, trial %d -> %s\n", ctrl.cmd, ctrl.install_attempts,
			  ctrl.trial ? ctrl.trial_boots : 0, boot_act_str(act));

		/* UPDATE requested but STAGING invalid / attempts exhausted: drop it */
		if (ctrl.cmd == BOOT_CMD_UPDATE && act != BOOT_ACT_INSTALL) {
			LOG_W(TAG, "update dropped (staging: %s)\n", fw_err_str(stg_err));
			ctrl.cmd = BOOT_CMD_NONE;
			ctrl.last_result = (uint8_t)(stg_err != FW_OK ? stg_err : FW_ERR_STATE);
			boot_ctrl_save(&ctrl);
		}
		/* ROLLBACK requested, or trial over, but nothing valid to go back to:
		 * keep what is there */
		if ((ctrl.cmd == BOOT_CMD_ROLLBACK || trial_over) && act != BOOT_ACT_ROLLBACK) {
			LOG_W(TAG, "no rollback (backup: %s)\n", fw_err_str(bak_err));
			ctrl.cmd = (uint8_t)(ctrl.cmd == BOOT_CMD_ROLLBACK ? BOOT_CMD_NONE : ctrl.cmd);
			ctrl.trial = 0;
			ctrl.trial_boots = 0;
			boot_ctrl_save(&ctrl);
		}

		/* host asked for the loader on the second link: after the bookkeeping
		 * above (based on the real decision), so a pending install or rollback
		 * is kept and done on the next pass once the loader returns */
		if (aux_req) {
			act = BOOT_ACT_LOADER;
		}

		switch (act) {
		case BOOT_ACT_RUN_RAW:
			LOG_W(TAG, "app has no image header (debug flash?)\n");
			/* fall through */
		case BOOT_ACT_RUN:
			/* a valid image is running: stale failed-attempt count is meaningless */
			if (act == BOOT_ACT_RUN && ctrl.install_attempts && ctrl.cmd == BOOT_CMD_NONE) {
				ctrl.install_attempts = 0;
				boot_ctrl_save(&ctrl);
			}
			/* an unconfirmed image: one more boot used up. The count is saved
			 * BEFORE the jump, so a crash during the boot still counts. */
			if (act == BOOT_ACT_RUN && ctrl.trial && ctrl.trial_boots < BOOT_MAX_TRIAL_BOOTS) {
				ctrl.trial_boots++;
				boot_ctrl_save(&ctrl);
				LOG_W(TAG, "trial boot %d/%d\n", ctrl.trial_boots, BOOT_MAX_TRIAL_BOOTS);
			}
			LOG_I(TAG, "jump to 0x%08X\n\n", app->addr + FW_IMAGE_HDR_SIZE);
			hal_console_flush();
			hal_jump_to_app(app->addr + FW_IMAGE_HDR_SIZE);
			break;

		case BOOT_ACT_INSTALL: {
			fw_err_t err;
			uint8_t have_backup = (bak_err == FW_OK);

			/* persist BEFORE copying: after a power loss the next boot knows
			 * an install is pending (cmd UPDATE) and counts the attempt. The
			 * recovery path (cmd was NONE) must set UPDATE too. */
			ctrl.cmd = BOOT_CMD_UPDATE;
			ctrl.install_attempts++;
			boot_ctrl_save(&ctrl);

			/* keep the running image: the one a rollback returns to. A
			 * half-copied APP (install cut by a power loss) cannot be kept;
			 * the backup of the attempt before is still there then. */
			if (app_err == FW_OK && hal_flash_info(FLASH_PART_BACKUP)->size) {
				LOG_I(TAG, "backing up v%d.%d.%d...\n", app_hdr.version.major,
					  app_hdr.version.minor, app_hdr.version.patch);
				err = boot_copy_image(FLASH_PART_BACKUP, FLASH_PART_APP, &app_hdr);
				have_backup = (err == FW_OK);
				if (!have_backup) {
					LOG_W(TAG, "backup failed: %s\n", fw_err_str(err));
				}
			}

			LOG_I(TAG, "installing v%d.%d.%d (attempt %d)...\n", stg_hdr.version.major,
				  stg_hdr.version.minor, stg_hdr.version.patch, ctrl.install_attempts);
			hal_led_set(1);
			err = boot_install(&stg_hdr);
			hal_led_set(0);

			ctrl.last_result = (uint8_t)err;
			if (err == FW_OK) {
				ctrl.cmd = BOOT_CMD_NONE;
				ctrl.install_attempts = 0;
				ctrl.install_count++;
				ctrl.trial = have_backup;		/* nothing to go back to: no trial */
				ctrl.trial_boots = 0;
				ctrl.rolled_back = 0;
				LOG_I(TAG, "install ok%s\n", have_backup ? ", on trial" : "");
			}
			else {
				LOG_E(TAG, "install failed: %s\n", fw_err_str(err));
			}
			boot_ctrl_save(&ctrl);
		}
			break;

		case BOOT_ACT_ROLLBACK: {
			fw_err_t err;

			/* same bookkeeping as an install: resumable after a power loss,
			 * bounded by the attempt counter */
			ctrl.cmd = BOOT_CMD_ROLLBACK;
			ctrl.install_attempts++;
			boot_ctrl_save(&ctrl);

			LOG_W(TAG, "rolling back to v%d.%d.%d (attempt %d)...\n", bak_hdr.version.major,
				  bak_hdr.version.minor, bak_hdr.version.patch, ctrl.install_attempts);
			hal_led_set(1);
			err = boot_copy_image(FLASH_PART_APP, FLASH_PART_BACKUP, &bak_hdr);
			hal_led_set(0);

			ctrl.last_result = (uint8_t)err;
			if (err == FW_OK) {
				/* the rejected image must not come back through the
				 * "APP corrupt: recover from STAGING" path */
				hal_flash_erase(FLASH_PART_STAGING, 0, hal_flash_info(FLASH_PART_STAGING)->erase_size);
				ctrl.cmd = BOOT_CMD_NONE;
				ctrl.install_attempts = 0;
				ctrl.trial = 0;
				ctrl.trial_boots = 0;
				ctrl.rolled_back = 1;
				LOG_I(TAG, "rollback ok\n");
			}
			else {
				LOG_E(TAG, "rollback failed: %s\n", fw_err_str(err));
			}
			boot_ctrl_save(&ctrl);
		}
			break;

		case BOOT_ACT_LOADER:
		default: {
#if BOOT_AUX_LOADER
			fw_proto_action_t r = loader_loop(aux_req ? &aux_io : &console_io);
#else
			fw_proto_action_t r = loader_loop(&console_io);
#endif

			aux_req = 0;

			if (r == FW_PROTO_ACT_INSTALL) {
				ctrl.cmd = BOOT_CMD_UPDATE;
				ctrl.install_attempts = 0;	/* new image -> reset counter */
			}
			else if (r == FW_PROTO_ACT_ROLLBACK) {
				ctrl.cmd = BOOT_CMD_ROLLBACK;
				ctrl.install_attempts = 0;
			}
			else if (ctrl.cmd == BOOT_CMD_LOADER) {
				ctrl.cmd = BOOT_CMD_NONE;
			}
			boot_ctrl_save(&ctrl);
		}
			break;
		}
	}
}

#if !defined(AK_SIM)
int main(void) {
	boot_main();
	return 0;
}
#endif
