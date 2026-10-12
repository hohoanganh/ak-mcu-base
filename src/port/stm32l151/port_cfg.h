/**
 ******************************************************************************
 * @brief:  STM32L151CB port configuration (AK Embedded Base Kit).
 *
 *  Flash 128K @ 0x08000000, 256 B pages, erased value 0x00.
 *  RAM 16K @ 0x20000000. Data EEPROM 4K @ 0x08080000.
 *  External W25Qxx SPI NOR on SPI1 (default STAGING location).
 *
 *  PORT_STAGING_EXTERNAL = 1 (default):
 *    0x08000000  BOOT     12K  bootloader
 *    0x08003000  APP     116K  [header 256 B][app, vector @ 0x08003100]
 *    NOR 0x80000 STAGING 116K  OTA image waiting for install (4K sectors)
 *    NOR 0xA0000 BACKUP  116K  the APP image before the last install (rollback)
 *
 *  PORT_STAGING_EXTERNAL = 0 (board without SPI flash):
 *    0x08000000  BOOT     12K
 *    0x08003000  APP      58K
 *    0x08011800  STAGING  58K  internal flash
 *
 *  The app linker script (app.ld) gets the APP size from the build system
 *  (--defsym __app_part_size), see port.cmake / pio_mcu_base.py.
 ******************************************************************************
**/

#ifndef __PORT_CFG_H__
#define __PORT_CFG_H__

#define PORT_BOARD_NAME			"ak-l151"

#define PORT_FLASH_PAGE			(256UL)
#define PORT_BOOT_ADDR			(0x08000000UL)

#ifndef PORT_STAGING_EXTERNAL
#define PORT_STAGING_EXTERNAL	(1)
#endif

/* Signed images (fw_sign.h): the bootloader carries P-256 + SHA-256 and
 * needs 16K, so APP starts at 0x08004000 in that variant. The linker scripts
 * take the same split through --defsym __boot_size. */
#ifndef PORT_IMAGE_SIGN
#define PORT_IMAGE_SIGN			(0)
#endif

#if PORT_IMAGE_SIGN
#define PORT_BOOT_SIZE			(0x4000UL)
#define PORT_APP_ADDR			(0x08004000UL)
#if PORT_STAGING_EXTERNAL
#define PORT_APP_SIZE			(0x1C000UL)		/* 112K */
#else
#define PORT_APP_SIZE			(0xE000UL)		/* 56K */
#define PORT_STAGING_ADDR		(0x08012000UL)
#define PORT_STAGING_SIZE		(0xE000UL)
#endif
#else
#define PORT_BOOT_SIZE			(0x3000UL)
#define PORT_APP_ADDR			(0x08003000UL)
#if PORT_STAGING_EXTERNAL
#define PORT_APP_SIZE			(0x1D000UL)		/* 116K: rest of internal flash */
#else
#define PORT_APP_SIZE			(0xE800UL)
#define PORT_STAGING_ADDR		(0x08011800UL)
#define PORT_STAGING_SIZE		(0xE800UL)
#endif
#endif

#if PORT_STAGING_EXTERNAL
#define PORT_STAGING_ADDR		(0x00080000UL)	/* SPI NOR offset (same as ak-base-kit) */
#define PORT_STAGING_SIZE		(0x1D000UL)
#define PORT_BACKUP_ADDR		(0x000A0000UL)	/* SPI NOR offset, above STAGING */
#define PORT_BACKUP_SIZE		(0x1D000UL)
#else
#define PORT_BACKUP_ADDR		(0UL)			/* no room in internal flash: no rollback */
#define PORT_BACKUP_SIZE		(0UL)
#endif

#define PORT_RAM_START			(0x20000000UL)
#define PORT_RAM_END			(0x20004000UL)

#define PORT_EEPROM_ADDR		(0x08080000UL)	/* boot_ctrl at EEPROM start */

/* Console USART1: PA9 TX, PA10 RX */
#define PORT_CONSOLE_BAUD		(115200)
#define PORT_CONSOLE_RX_BUF		(256)		/* power of 2 */
#define PORT_CONSOLE_TX_BUF		(256)		/* power of 2, app only (boot TX is polled) */

/* SPI NOR: SPI1 PA5/PA6/PA7, CS PB14. */
#define PORT_NOR_CS_PORT		GPIOB
#define PORT_NOR_CS_PIN			GPIO_Pin_14
#define PORT_NOR_CS_CLK			RCC_AHBPeriph_GPIOB

/* AK Base Kit only: a module on the SPI expansion connector J6 (nRF24L01+,
 * W5500) shares SPI1 with the flash. Its chip select must be held high or it
 * answers on MISO. On the kit 3.0 that is PA4 (J6 pin 4, schematic sheet 6);
 * earlier kits used PB9: override the three PORT_NRF_CSN_* for those.
 * A product board has no such device: leave 0 and the pin stays untouched. */
#ifndef PORT_KIT_NRF24_CSN
#define PORT_KIT_NRF24_CSN		(0)
#endif
#if PORT_KIT_NRF24_CSN && !defined(PORT_NRF_CSN_PIN)
#define PORT_NRF_CSN_PORT		GPIOA
#define PORT_NRF_CSN_PIN		GPIO_Pin_4
#define PORT_NRF_CSN_CLK		RCC_AHBPeriph_GPIOA
#endif
#define PORT_NOR_SPI_PRESCALER	SPI_BaudRatePrescaler_8	/* 32 MHz / 8 = 4 MHz, as ak-base-kit */

/* RS485: USART2 PA2 TX, PA3 RX; direction pin PA1, high = transmit */
#define PORT_RS485_DIR_PORT		GPIOA
#define PORT_RS485_DIR_PIN		GPIO_Pin_1
#define PORT_RS485_DIR_CLK		RCC_AHBPeriph_GPIOA
#define PORT_RS485_RX_BUF		(256)		/* power of 2, one full Modbus frame */

/* LED life PB8. On the kit the LED sits between +3V3 and the pin (net
 * LED_DBG_N, schematic sheet 7): it lights when the pin is LOW. */
#define PORT_LED_PORT			GPIOB
#define PORT_LED_PIN			GPIO_Pin_8
#define PORT_LED_CLK			RCC_AHBPeriph_GPIOB
#ifndef PORT_LED_ACTIVE_LOW
#define PORT_LED_ACTIVE_LOW		(1)
#endif

#endif /* __PORT_CFG_H__ */
