#include <string.h>

#include "boot_core.h"
#include "boot_ctrl.h"
#include "hal.h"

boot_act_t boot_decide(const boot_state_t* s) {
	uint8_t attempts_left = s->install_attempts < BOOT_MAX_INSTALL_ATTEMPTS;
	uint8_t can_install = s->staging_ok && attempts_left;
	uint8_t can_restore = s->backup_ok && attempts_left;

	/* app requested loader mode (LOADER command) */
	if (s->cmd == BOOT_CMD_LOADER) {
		return BOOT_ACT_LOADER;
	}

	/* app requested an update */
	if (s->cmd == BOOT_CMD_UPDATE && can_install) {
		return BOOT_ACT_INSTALL;
	}

	/* app asked for the previous image back, or the new image never
	 * confirmed itself within its trial boots */
	if (can_restore && (s->cmd == BOOT_CMD_ROLLBACK || (s->trial && s->trial_boots >= BOOT_MAX_TRIAL_BOOTS))) {
		return BOOT_ACT_ROLLBACK;
	}

	if (s->app_ok) {
		return BOOT_ACT_RUN;
	}

	/* APP without header (raw SWD flash): allowed for debugging, but ONLY when
	 * no install is pending. boot_copy_image() erases the APP header first, so
	 * a half-copied image also has no header yet a sane vector table - it must
	 * never run. */
	if (s->app_raw_ok && s->cmd == BOOT_CMD_NONE && s->install_attempts == 0) {
		return BOOT_ACT_RUN_RAW;
	}

	/* APP corrupt / empty: recover from the image kept in STAGING, else from
	 * the one the last install replaced */
	if (can_install) {
		return BOOT_ACT_INSTALL;
	}
	if (can_restore) {
		return BOOT_ACT_ROLLBACK;
	}

	return BOOT_ACT_LOADER;
}

const char* boot_act_str(boot_act_t act) {
	switch (act) {
	case BOOT_ACT_RUN:		return "run";
	case BOOT_ACT_RUN_RAW:	return "run (raw, no header)";
	case BOOT_ACT_INSTALL:	return "install";
	case BOOT_ACT_ROLLBACK:	return "rollback";
	case BOOT_ACT_LOADER:	return "loader";
	default:				return "?";
	}
}

/* 128 B = STM32L1 half page: lets the port use its fast program path. */
#define COPY_CHUNK		(128U)

static fw_err_t copy_page(flash_part_t dst, flash_part_t src, uint32_t off, uint32_t page_size) {
	uint32_t buf[COPY_CHUNK / 4];	/* word aligned */
	uint32_t i;

	if (hal_flash_erase(dst, off, page_size) != HAL_FLASH_OK) {
		return FW_ERR_FLASH;
	}

	for (i = 0; i < page_size; i += sizeof(buf)) {
		if (hal_flash_read(src, off + i, buf, sizeof(buf)) != HAL_FLASH_OK) {
			return FW_ERR_FLASH;
		}
		if (hal_flash_write(dst, off + i, buf, sizeof(buf)) != HAL_FLASH_OK) {
			return FW_ERR_FLASH;
		}
	}

	hal_wdt_kick();
	return FW_OK;
}

fw_err_t boot_copy_image(flash_part_t dst, flash_part_t src, const fw_image_hdr_t* hdr) {
	const flash_part_info_t* d = hal_flash_info(dst);
	const flash_part_info_t* s = hal_flash_info(src);
	uint32_t page;
	uint32_t total = FW_IMAGE_HDR_SIZE + hdr->img_size;
	uint32_t off;
	fw_err_t err;

	if (d == 0 || s == 0 || d->size == 0) {
		return FW_ERR_ARG;
	}
	page = d->erase_size;

	/* page is a multiple of the copy chunk; header fits in the first page.
	 * The source may have a different geometry (e.g. SPI NOR, 4K sectors):
	 * it is only read here, in whole destination pages. */
	if ((page % COPY_CHUNK) != 0 || page < FW_IMAGE_HDR_SIZE) {
		return FW_ERR_ARG;
	}
	if (total > d->size || ((total + page - 1) / page) * page > s->size) {
		return FW_ERR_TOO_BIG;
	}

	/* 1) invalidate the destination header first: it stays invalid until the copy completes */
	if (hal_flash_erase(dst, 0, page) != HAL_FLASH_OK) {
		return FW_ERR_FLASH;
	}

	/* 2) copy remaining pages */
	for (off = page; off < total; off += page) {
		err = copy_page(dst, src, off, page);
		if (err != FW_OK) {
			return err;
		}
	}

	/* 3) copy the header page last */
	err = copy_page(dst, src, 0, page);
	if (err != FW_OK) {
		return err;
	}

	return fw_image_verify(dst, 0);
}
