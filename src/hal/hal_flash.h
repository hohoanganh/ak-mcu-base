/**
 ******************************************************************************
 * @brief:  Partition-based flash access.
 *
 *   FLASH_PART_BOOT    : bootloader
 *   FLASH_PART_APP     : [header 256 B][app] - app executes at addr + 256
 *   FLASH_PART_STAGING : download slot for new images (OTA) before install.
 *                        Internal flash by default, or external SPI NOR -
 *                        the port only has to report info + erase/write.
 *   FLASH_PART_BACKUP  : copy of the APP image taken by the bootloader before
 *                        it installs a new one: the image a rollback returns
 *                        to. A port without room for it reports size 0.
 *
 * Offsets are relative to the partition start. Returns 0 = OK, < 0 = error.
 ******************************************************************************
**/

#ifndef __HAL_FLASH_H__
#define __HAL_FLASH_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

typedef enum {
	FLASH_PART_BOOT = 0,
	FLASH_PART_APP,
	FLASH_PART_STAGING,
	FLASH_PART_BACKUP,
	FLASH_PART_NUM
} flash_part_t;

typedef struct {
	uint32_t addr;			/* absolute address (APP: execution address) */
	uint32_t size;			/* bytes */
	uint32_t erase_size;	/* erase unit (page/sector) */
	uint32_t write_size;	/* program unit (write offset/len must be multiples) */
	uint8_t  erased_val;	/* byte value after erase (STM32L1 = 0x00!) */
} flash_part_info_t;

#define HAL_FLASH_OK			(0)
#define HAL_FLASH_ERR_ARG		(-1)
#define HAL_FLASH_ERR_HW		(-2)

extern const flash_part_info_t* hal_flash_info(flash_part_t part);

/* off, len: multiples of erase_size */
extern int hal_flash_erase(flash_part_t part, uint32_t off, uint32_t len);
/* off, len: multiples of write_size; target area must be erased */
extern int hal_flash_write(flash_part_t part, uint32_t off, const void* data, uint32_t len);
extern int hal_flash_read(flash_part_t part, uint32_t off, void* buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_FLASH_H__ */
