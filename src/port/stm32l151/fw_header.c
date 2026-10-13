/**
 * Image header linked into the app .elf (section .fw_header, start of APP).
 * After linking, tools/mkimage.py patch fills img_size + CRCs and updates the
 * .elf, so flashing the .elf over SWD also yields a bootable image.
 */
#include "fw_image.h"
#include "port_cfg.h"

#ifndef APP_VER_MAJOR
#define APP_VER_MAJOR	1
#define APP_VER_MINOR	3
#define APP_VER_PATCH	9
#endif
#ifndef APP_VER_BUILD
#define APP_VER_BUILD	0
#endif

__attribute__((section(".fw_header"), used))
const fw_image_hdr_t fw_app_header = {
	.magic = FW_IMAGE_MAGIC,
	.hdr_version = FW_IMAGE_HDR_VERSION,
	.hdr_size = FW_IMAGE_HDR_SIZE,
	.img_size = 0,			/* mkimage.py patch */
	.img_crc32 = 0,			/* mkimage.py patch */
	.load_addr = PORT_APP_ADDR + FW_IMAGE_HDR_SIZE,
	.version = { APP_VER_MAJOR, APP_VER_MINOR, APP_VER_PATCH, 0, APP_VER_BUILD },
	.board = PORT_BOARD_NAME,
	.hdr_crc32 = 0,			/* mkimage.py patch */
};
