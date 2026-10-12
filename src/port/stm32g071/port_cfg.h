/**
 ******************************************************************************
 * @brief:  STM32G071RB port configuration (NUCLEO-G071RB and the like).
 *
 *  Cortex-M0+ at 16 MHz (HSI16, no PLL), flash 128K @ 0x08000000 in 2K pages
 *  programmed by 64-bit double words, erased value 0xFF. RAM 36K. No data
 *  EEPROM: the NVM of hal.h is emulated in the last two flash pages. No SPI
 *  NOR: OTA images are staged in internal flash, there is no BACKUP (no
 *  rollback) and no signed-image variant yet.
 *
 *    0x08000000  BOOT     12K  bootloader (6 pages)
 *    0x08003000  APP      56K  [header 256 B][app, vector @ 0x08003100]
 *    0x08011000  STAGING  56K  OTA image waiting for install
 *    0x0801F000  NVM       4K  two pages, boot_ctrl + crash log (hal_nvm)
 *
 *  The app linker script gets the APP size from the build system
 *  (--defsym __app_part_size), see port.cmake.
 ******************************************************************************
**/

#ifndef __PORT_CFG_H__
#define __PORT_CFG_H__

#define PORT_BOARD_NAME			"ak-g071"

#define PORT_FLASH_PAGE			(2048UL)
#define PORT_FLASH_WRITE		(8UL)			/* double word */
#define PORT_BOOT_ADDR			(0x08000000UL)
#define PORT_BOOT_SIZE			(0x3000UL)
#define PORT_APP_ADDR			(0x08003000UL)
#define PORT_APP_SIZE			(0xE000UL)
#define PORT_STAGING_ADDR		(0x08011000UL)
#define PORT_STAGING_SIZE		(0xE000UL)
#define PORT_BACKUP_ADDR		(0UL)			/* no room: no rollback on this chip */
#define PORT_BACKUP_SIZE		(0UL)
#define PORT_NVM_ADDR			(0x0801F000UL)	/* 2 pages, EEPROM emulation */

#define PORT_STAGING_EXTERNAL	(0)
#define PORT_IMAGE_SIGN			(0)

#define PORT_RAM_START			(0x20000000UL)
#define PORT_RAM_END			(0x20009000UL)	/* 36K */

#define PORT_SYSCLK_HZ			(16000000UL)	/* HSI16 straight through */

/* Console USART2: PA2 TX, PA3 RX, AF1 (the ST-Link virtual COM port of a Nucleo) */
#define PORT_CONSOLE_BAUD		(115200)
#define PORT_CONSOLE_RX_BUF		(256)		/* power of 2 */
#define PORT_CONSOLE_TX_BUF		(256)		/* power of 2, app only (boot TX is polled) */

/* LED LD4 of the Nucleo: PA5, on when high */
#define PORT_LED_PIN			(5)
#ifndef PORT_LED_ACTIVE_LOW
#define PORT_LED_ACTIVE_LOW		(0)
#endif

#endif /* __PORT_CFG_H__ */
