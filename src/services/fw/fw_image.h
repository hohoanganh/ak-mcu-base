/**
 ******************************************************************************
 * @brief:  Firmware image format: [256 B header][app binary].
 *
 *  - The header sits at the start of APP/STAGING; the app is linked at
 *    APP.addr + FW_IMAGE_HDR_SIZE (Cortex-M3 VTOR needs 256 B alignment).
 *  - Produced by tools/mkimage.py.
 *  - All fields little-endian, fixed 256 B layout (static assert).
 ******************************************************************************
**/

#ifndef __FW_IMAGE_H__
#define __FW_IMAGE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#include "fw_types.h"
#include "hal_flash.h"

#define FW_IMAGE_MAGIC			(0x57464B41UL)	/* "AKFW" */
#define FW_IMAGE_HDR_VERSION	(1)
#define FW_IMAGE_HDR_SIZE		(256)
#define FW_IMAGE_BOARD_LEN		(16)

typedef struct {
	uint32_t magic;				/*   0 FW_IMAGE_MAGIC */
	uint16_t hdr_version;		/*   4 FW_IMAGE_HDR_VERSION */
	uint16_t hdr_size;			/*   6 FW_IMAGE_HDR_SIZE */
	uint32_t img_size;			/*   8 binary size after the header */
	uint32_t img_crc32;			/*  12 crc32 of the binary */
	uint32_t load_addr;			/*  16 vector table address (APP.addr + 256) */
	fw_version_t version;		/*  20 major.minor.patch + build (8 B) */
	char     board[FW_IMAGE_BOARD_LEN];	/* 28 board name, '\0' padded */
	uint8_t  sig_type;			/*  44 FW_SIGN_NONE / FW_SIGN_ECDSA_P256 (fw_sign.h) */
	uint8_t  sig_reserved[3];	/*  45 */
	uint8_t  sig[64];			/*  48 ECDSA r||s over SHA-256(bytes 0..47 || binary) */
	uint8_t  reserved[FW_IMAGE_HDR_SIZE - 112 - 4];	/* 112 */
	uint32_t hdr_crc32;			/* 252 crc32 of bytes 0..251 */
} fw_image_hdr_t;

typedef char fw_image_hdr_size_check[(sizeof(fw_image_hdr_t) == FW_IMAGE_HDR_SIZE) ? 1 : -1];

/* Full check of the image in a partition: magic, header crc, board, load
 * address, size, binary crc, vector table. hdr_out may be NULL. */
extern fw_err_t fw_image_verify(flash_part_t part, fw_image_hdr_t* hdr_out);

/* Header-only check (fast, does not read the binary). */
extern fw_err_t fw_image_check_hdr(const fw_image_hdr_t* hdr, uint32_t part_size);

#ifdef __cplusplus
}
#endif

#endif /* __FW_IMAGE_H__ */
