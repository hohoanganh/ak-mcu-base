#include <string.h>

#include "fw_update.h"
#include "hal.h"

static fw_update_state_t state = FW_UPDATE_IDLE;
static uint32_t total;
static uint32_t written;
static uint32_t erased_upto;	/* STAGING bytes [0, erased_upto) are erased */

fw_update_state_t fw_update_state(void) {
	return state;
}

uint32_t fw_update_written(void) {
	return written;
}

uint32_t fw_update_total(void) {
	return total;
}

void fw_update_abort(void) {
	state = FW_UPDATE_IDLE;
	total = 0;
	written = 0;
	erased_upto = 0;
}

fw_err_t fw_update_begin(uint32_t total_size) {
	const flash_part_info_t* info = hal_flash_info(FLASH_PART_STAGING);

	fw_update_abort();

	if (total_size <= FW_IMAGE_HDR_SIZE) {
		return FW_ERR_SIZE;
	}
	if (total_size > info->size) {
		return FW_ERR_TOO_BIG;
	}

	total = total_size;
	state = FW_UPDATE_RECEIVING;
	return FW_OK;
}

static fw_err_t ensure_erased(uint32_t end) {
	const flash_part_info_t* info = hal_flash_info(FLASH_PART_STAGING);

	while (erased_upto < end) {
		if (hal_flash_erase(FLASH_PART_STAGING, erased_upto, info->erase_size) != HAL_FLASH_OK) {
			return FW_ERR_FLASH;
		}
		erased_upto += info->erase_size;
	}
	return FW_OK;
}

fw_err_t fw_update_write(uint32_t offset, const uint8_t* data, uint32_t len) {
	const flash_part_info_t* info = hal_flash_info(FLASH_PART_STAGING);
	uint32_t ws = info->write_size;
	uint32_t body, tail;
	fw_err_t err;

	if (state != FW_UPDATE_RECEIVING) {
		return FW_ERR_STATE;
	}
	if (offset != written) {
		return FW_ERR_OFFSET;
	}
	if (len == 0 || offset + len > total) {
		return FW_ERR_SIZE;
	}

	tail = len % ws;
	/* an unaligned chunk is only allowed as the last one */
	if (tail != 0 && offset + len != total) {
		return FW_ERR_SIZE;
	}
	body = len - tail;

	err = ensure_erased(offset + len + (tail ? (ws - tail) : 0));
	if (err != FW_OK) {
		fw_update_abort();
		return err;
	}

	if (body && hal_flash_write(FLASH_PART_STAGING, offset, data, body) != HAL_FLASH_OK) {
		fw_update_abort();
		return FW_ERR_FLASH;
	}

	if (tail) {
		uint8_t last[16];

		if (ws > sizeof(last)) {
			fw_update_abort();
			return FW_ERR_ARG;
		}
		memset(last, info->erased_val, ws);
		memcpy(last, data + body, tail);
		if (hal_flash_write(FLASH_PART_STAGING, offset + body, last, ws) != HAL_FLASH_OK) {
			fw_update_abort();
			return FW_ERR_FLASH;
		}
	}

	written += len;
	return FW_OK;
}

fw_err_t fw_update_finish(fw_image_hdr_t* hdr_out) {
	fw_image_hdr_t local;
	fw_image_hdr_t* hdr = hdr_out ? hdr_out : &local;	/* one header on the stack, not three */
	fw_err_t err;

	if (state != FW_UPDATE_RECEIVING || written != total) {
		return FW_ERR_STATE;
	}

	err = fw_image_verify(FLASH_PART_STAGING, hdr);
	if (err == FW_OK && FW_IMAGE_HDR_SIZE + hdr->img_size != total) {
		err = FW_ERR_SIZE;
	}

	if (err != FW_OK) {
		fw_update_abort();
		return err;
	}

	state = FW_UPDATE_READY;
	return FW_OK;
}
