#include <string.h>

#include "fw_image.h"
#include "crc.h"
#include "hal.h"

const char* fw_err_str(fw_err_t err) {
	switch (err) {
	case FW_OK:				return "ok";
	case FW_ERR_ARG:		return "bad arg";
	case FW_ERR_STATE:		return "bad state";
	case FW_ERR_TOO_BIG:	return "too big";
	case FW_ERR_FLASH:		return "flash error";
	case FW_ERR_MAGIC:		return "no image";
	case FW_ERR_HDR_CRC:	return "header crc";
	case FW_ERR_BOARD:		return "wrong board";
	case FW_ERR_ADDR:		return "wrong load addr";
	case FW_ERR_IMG_CRC:	return "image crc";
	case FW_ERR_OFFSET:		return "bad offset";
	case FW_ERR_CMD:		return "unknown cmd";
	case FW_ERR_SIZE:		return "bad size";
	case FW_ERR_VECTOR:		return "bad vector table";
	default:				return "?";
	}
}

/* strncmp(a, b, FW_IMAGE_BOARD_LEN) == 0, without pulling newlib's strcmp
 * (440 B) into the bootloader: gcc turns that strncmp into strcmp because
 * the board name is a literal shorter than the limit. */
static uint8_t board_match(const char* a, const char* b) {
	for (uint8_t i = 0; i < FW_IMAGE_BOARD_LEN; i++) {
		if (a[i] != b[i]) {
			return 0;
		}
		if (a[i] == 0) {
			break;
		}
	}
	return 1;
}

fw_err_t fw_image_check_hdr(const fw_image_hdr_t* hdr, uint32_t part_size) {
	if (hdr->magic != FW_IMAGE_MAGIC) {
		return FW_ERR_MAGIC;
	}

	if (crc32_update(CRC32_INIT, hdr, FW_IMAGE_HDR_SIZE - 4) != hdr->hdr_crc32) {
		return FW_ERR_HDR_CRC;
	}

	if (hdr->hdr_version != FW_IMAGE_HDR_VERSION || hdr->hdr_size != FW_IMAGE_HDR_SIZE) {
		return FW_ERR_MAGIC;
	}

	if (part_size <= FW_IMAGE_HDR_SIZE ||
			hdr->img_size < 8 || hdr->img_size > part_size - FW_IMAGE_HDR_SIZE) {
		return FW_ERR_SIZE;
	}

	if (!board_match(hdr->board, hal_board_name())) {
		return FW_ERR_BOARD;
	}

	/* Images always execute from APP, even while stored in STAGING. */
	if (hdr->load_addr != hal_flash_info(FLASH_PART_APP)->addr + FW_IMAGE_HDR_SIZE) {
		return FW_ERR_ADDR;
	}

	return FW_OK;
}

fw_err_t fw_image_verify(flash_part_t part, fw_image_hdr_t* hdr_out) {
	fw_image_hdr_t local;
	fw_image_hdr_t* hdr = hdr_out ? hdr_out : &local;	/* read straight into the caller's copy */
	uint8_t buf[64];
	uint32_t vec[2];
	uint32_t crc = CRC32_INIT;
	uint32_t off, n;
	fw_err_t err;
	const flash_part_info_t* info = hal_flash_info(part);

	if (info == 0) {
		return FW_ERR_ARG;
	}

	if (hal_flash_read(part, 0, hdr, sizeof(*hdr)) != HAL_FLASH_OK) {
		return FW_ERR_FLASH;
	}

	err = fw_image_check_hdr(hdr, info->size);
	if (err != FW_OK) {
		return err;
	}

	for (off = 0; off < hdr->img_size; off += n) {
		n = hdr->img_size - off;
		if (n > sizeof(buf)) {
			n = sizeof(buf);
		}
		if (hal_flash_read(part, FW_IMAGE_HDR_SIZE + off, buf, n) != HAL_FLASH_OK) {
			return FW_ERR_FLASH;
		}
		crc = crc32_update(crc, buf, n);
	}

	if (crc != hdr->img_crc32) {
		return FW_ERR_IMG_CRC;
	}

	/* Correct CRC but wrong linker script (e.g. linked at 0x08000000) must
	 * still be rejected before jumping into it. */
	if (hal_flash_read(part, FW_IMAGE_HDR_SIZE, vec, sizeof(vec)) != HAL_FLASH_OK) {
		return FW_ERR_FLASH;
	}
	if (!hal_vector_ok(vec[0], vec[1])) {
		return FW_ERR_VECTOR;
	}

	return FW_OK;
}
