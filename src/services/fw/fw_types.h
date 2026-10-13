/**
 ******************************************************************************
 * @brief:  Common firmware-update types: error codes, version.
 ******************************************************************************
**/

#ifndef __FW_TYPES_H__
#define __FW_TYPES_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

/* Error codes; also the status byte of protocol responses. Keep in sync with
 * ERRORS in tools/ak_fw.py. */
typedef enum {
	FW_OK = 0,
	FW_ERR_ARG = 1,
	FW_ERR_STATE = 2,
	FW_ERR_TOO_BIG = 3,
	FW_ERR_FLASH = 4,
	FW_ERR_MAGIC = 5,
	FW_ERR_HDR_CRC = 6,
	FW_ERR_BOARD = 7,
	FW_ERR_ADDR = 8,
	FW_ERR_IMG_CRC = 9,
	FW_ERR_OFFSET = 10,
	FW_ERR_CMD = 11,
	FW_ERR_SIZE = 12,
	FW_ERR_VECTOR = 13,
	FW_ERR_SIG = 14,		/* unsigned, or signed with another key (FW_IMAGE_SIGN builds) */
} fw_err_t;

typedef struct {
	uint8_t  major;
	uint8_t  minor;
	uint8_t  patch;
	uint8_t  reserved;
	uint32_t build;
} fw_version_t;

extern const char* fw_err_str(fw_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* __FW_TYPES_H__ */
