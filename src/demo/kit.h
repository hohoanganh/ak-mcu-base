/**
 ******************************************************************************
 * @brief:  Board support of the AK Base Kit used by the demo: 128x64 OLED,
 *          three buttons, buzzer, RTC, SHT45, SPI flash. Implemented per port
 *          (port/stm32l151/kit.c); the demo code above it is chip independent
 *          and also runs in the host renderer (tests/test_demo.c).
 ******************************************************************************
**/

#ifndef __KIT_H__
#define __KIT_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define KIT_LCD_W			(128)
#define KIT_LCD_H			(64)
#define KIT_LCD_PAGES		(KIT_LCD_H / 8)

/* buttons below the display, left to right */
#define KIT_BTN_1			(0x01)
#define KIT_BTN_2			(0x02)
#define KIT_BTN_3			(0x04)

/* GPIO, buttons, buzzer; resets and starts the display. Returns 1 if the
 * display answered on its bus. */
extern uint8_t kit_init(void);

/* Part of one display page: n bytes for columns x .. x + n - 1, one byte per
 * column, bit 0 = top pixel of the page. Returns 0 if the display did not
 * acknowledge. */
extern uint8_t kit_lcd_write(uint8_t page, uint8_t x, const uint8_t* data, uint8_t n);

/* Does address a acknowledge? bus 0 = display pins, 1 = display pins swapped,
 * 2 = I2C1 header, 3 = I2C1 swapped (stm32l151 port only: a diagnosis for a
 * board whose display stays dark). */
extern uint8_t kit_bus_probe(uint8_t bus, uint8_t a);

/* Buttons held down right now (KIT_BTN_* mask, not debounced). */
extern uint8_t kit_buttons(void);

/* Square wave on the buzzer, 0 = silent. */
extern void kit_buzzer(uint16_t freq_hz);

/* RTC (hours 0..23). Return 1 on success, 0 if no RTC answers. */
extern uint8_t kit_rtc_get(uint8_t* hh, uint8_t* mm, uint8_t* ss);
extern uint8_t kit_rtc_set(uint8_t hh, uint8_t mm, uint8_t ss);

/* Temperature / humidity sensor (SHT4x). kit_sht_start() begins a measurement,
 * kit_sht_read() fetches it 10 ms or more later: t10 in 0.1 C, rh10 in 0.1 %.
 * Both return 0 if the sensor does not answer (or the checksum is wrong). */
extern uint8_t kit_sht_start(void);
extern uint8_t kit_sht_read(int16_t* t10, uint16_t* rh10);

/* Raw access to the I2C1 header (the bus of the RTC and the SHT45), 7-bit
 * address. kit_i2c_probe(): 1 if a device acknowledges. kit_i2c_read(): reads
 * len bytes starting at register reg, 1 on success. */
extern uint8_t kit_i2c_probe(uint8_t addr);
extern uint8_t kit_i2c_read(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len);
/* register writes, 1 if acknowledged (stm32l151 port only: fcb imu, the
 * ADS1115 of the remote) */
extern uint8_t kit_i2c_write(uint8_t addr, uint8_t reg, uint8_t val);
extern uint8_t kit_i2c_write_n(uint8_t addr, uint8_t reg, const uint8_t* data, uint8_t len);
/* Releases both lines and samples them n times: how often each was high and
 * how many times it changed. */
extern void kit_i2c_watch(uint32_t n, uint32_t* scl_high, uint32_t* sda_high, uint32_t* scl_edges,
						  uint32_t* sda_edges);

/* Only with -DAPP_KIT_SPI_SNIFF (env:kit_tools): the capture buffers take 5.6 KB
 * of the 32 KB of RAM.
 * SPI sniffer on the J6 pins: SPI1 becomes a receive-only slave (NSS PA4,
 * SCK PA5, MOSI PA7), every byte on the bus lands in a RAM buffer, a falling
 * edge of NSS starts a new frame. The flash is off the bus while it runs;
 * kit_spi_sniff_stop() gives SPI1 back. mode = CPOL << 1 | CPHA. trig <= 0xFF:
 * the capture starts with the first frame whose first byte is trig. */
#define KIT_SPI_SNIFF_BUF		(2560)
#define KIT_SPI_SNIFF_FRAMES	(512)
extern void kit_spi_sniff_start(uint8_t mode, uint16_t trig);
extern void kit_spi_sniff_stop(void);
/* 1 while running; bytes and frames captured so far */
extern uint8_t kit_spi_sniff_status(uint16_t* bytes, uint16_t* frames);
/* frame i: its bytes, length, and the time since the previous frame in us.
 * 0 if there is no such frame. */
extern uint8_t kit_spi_sniff_frame(uint16_t i, const uint8_t** data, uint16_t* len, uint32_t* dt_us);
/* PA4, PA5, PA7 as inputs sampled n times: high counts and edge counts per
 * pin, to tell SCK / CSN / DATA apart before capturing */
extern void kit_spi_watch(uint32_t n, uint8_t pull, uint32_t high[3], uint32_t edges[3]);	/* pull: 0 none, 1 up, 2 down */

/* Media store: the part of the SPI flash the firmware update does not use
 * (0 up to the OTA staging area). Who uses what, counted in sectors from the
 * end: the last sector holds the block program (scr_blocks.c), the
 * WEATHER_LOG_SECTORS before it the weather log (scr_weather.c), the video
 * clip starts at 0. Erase before writing; an erase clears one sector of
 * KIT_STORE_SECTOR bytes and can take some 100 ms. All return 1 on success. */
#define KIT_STORE_SECTOR	(4096U)
extern uint32_t kit_store_size(void);					/* bytes, 0 = no flash */
extern uint8_t kit_store_read(uint32_t off, void* buf, uint32_t len);
extern uint8_t kit_store_erase(uint32_t off);			/* off: start of a sector */
extern uint8_t kit_store_write(uint32_t off, const void* data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* __KIT_H__ */
