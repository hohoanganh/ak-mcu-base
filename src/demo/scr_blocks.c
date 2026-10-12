/**
 ******************************************************************************
 * @brief:  Screen "Blocks": runs the program drawn in the block editor
 *          (docs/play/blocks.html) on the display, buttons, buzzer and LED of
 *          the kit. The machine is blk_vm.c; this file gives it the kit and
 *          takes the program from the console protocol.
 *
 *  A program arrives in three steps (fw_proto extension, see task_ui.c):
 *  blocks_begin(len), blocks_data(offset, bytes)..., blocks_end(crc16). It
 *  is then kept in the last sector of the media store, so it is still there
 *  after a power cycle.
 ******************************************************************************
**/

#include <string.h>

#include "hal.h"
#include "crc.h"

#include "ui.h"
#include "weather.h"
#include "blk_vm.h"
#include "blocks.h"

#define STORE_MAGIC		"BKS1"

static uint16_t rx_len;				/* program being received, 0 = none */
static uint8_t rx_open;

/*----------------------------------------------------------------------------
 * the kit, as the machine sees it
 *--------------------------------------------------------------------------*/
static void io_led(uint8_t on) {
	ui_led(on);
}

static void io_beep(uint16_t hz, uint16_t ms) {
	ui_beep(hz, ms);
}

static uint8_t io_buttons(void) {
	return kit_buttons();
}

static void io_cls(void) {
	gfx_clear();
}

static void io_text(int x, int y, const char* s, uint8_t size) {
	gfx_text(x, y, s, size);
}

static void io_pixel(int x, int y, uint8_t on) {
	gfx_pixel(x, y, on);
}

static void io_line(int x0, int y0, int x1, int y1) {
	gfx_line(x0, y0, x1, y1, 1);
}

static void io_rect(int x, int y, int w, int h, uint8_t how) {
	if (how) {
		gfx_fill(x, y, w, h, (uint8_t)(how == 1));
	}
	else {
		gfx_rect(x, y, w, h, 1);
	}
}

static int io_temp(void) {
	int16_t t10 = 0;
	uint16_t rh10 = 0;

	if (!weather_now(&t10, &rh10)) {
		return 0;
	}
	return (t10 >= 0) ? (t10 + 5) / 10 : -((5 - t10) / 10);
}

static int io_humi(void) {
	int16_t t10 = 0;
	uint16_t rh10 = 0;

	return weather_now(&t10, &rh10) ? (rh10 + 5) / 10 : 0;
}

static int io_time(uint8_t what) {
	uint8_t hh, mm, ss;

	if (!kit_rtc_get(&hh, &mm, &ss)) {			/* no clock chip: time since power-on */
		uint32_t t = hal_millis() / 1000UL;

		hh = (uint8_t)((t / 3600UL) % 24UL);
		mm = (uint8_t)((t / 60UL) % 60UL);
		ss = (uint8_t)(t % 60UL);
	}
	return what == 0 ? hh : what == 1 ? mm : ss;
}

static const blk_io_t kit_io = {
	io_led, io_beep, io_buttons, io_cls, io_text, io_pixel, io_line, io_rect,
	io_temp, io_humi, ui_rand, io_time,
};

/*----------------------------------------------------------------------------
 * the copy in the media store
 *--------------------------------------------------------------------------*/
static uint32_t store_at(void) {
	uint32_t size = kit_store_size();

	return size >= 2U * KIT_STORE_SECTOR ? size - KIT_STORE_SECTOR : 0;
}

static void store_save(void) {
	uint8_t head[8];
	uint16_t len;
	const uint8_t* img = blk_image(&len);
	uint32_t at = store_at();
	uint16_t crc;

	if (!at || !img) {
		return;
	}
	crc = crc16_update(CRC16_INIT, img, len);
	memcpy(head, STORE_MAGIC, 4);
	head[4] = (uint8_t)len;
	head[5] = (uint8_t)(len >> 8);
	head[6] = (uint8_t)crc;
	head[7] = (uint8_t)(crc >> 8);
	if (kit_store_erase(at) && kit_store_write(at, head, sizeof(head))) {
		(void)kit_store_write(at + sizeof(head), img, len);
	}
}

static void store_load(void) {
	uint8_t head[8];
	uint32_t at = store_at();
	uint16_t len;
	uint8_t* buf;

	if (!at || !kit_store_read(at, head, sizeof(head)) || memcmp(head, STORE_MAGIC, 4) != 0) {
		return;
	}
	len = (uint16_t)(head[4] | (head[5] << 8));
	if (len == 0 || len > BLK_MAX_SIZE) {
		return;
	}
	buf = blk_buffer();
	if (kit_store_read(at + sizeof(head), buf, len)
			&& crc16_update(CRC16_INIT, buf, len) == (uint16_t)(head[6] | (head[7] << 8))) {
		(void)blk_load(buf, len);
	}
}

/*----------------------------------------------------------------------------
 * a program arrives
 *--------------------------------------------------------------------------*/
uint8_t blocks_begin(uint16_t len) {
	rx_open = 0;
	if (len == 0 || len > BLK_MAX_SIZE) {
		return 0;
	}
	(void)blk_buffer();				/* stops and drops the old program */
	rx_len = len;
	rx_open = 1;
	return 1;
}

uint8_t blocks_data(uint16_t off, const uint8_t* data, uint16_t n) {
	if (!rx_open || (uint32_t)off + n > rx_len) {
		return 0;
	}
	/* the machine is stopped and empty since blocks_begin(): its buffer is free */
	memcpy(blk_buffer() + off, data, n);
	return 1;
}

uint8_t blocks_end(uint16_t crc) {
	uint8_t* buf;

	if (!rx_open) {
		return BLK_ERR_IMAGE;
	}
	rx_open = 0;
	buf = blk_buffer();
	if (crc16_update(CRC16_INIT, buf, rx_len) != crc || blk_load(buf, rx_len) != BLK_OK) {
		return BLK_ERR_IMAGE;
	}
	store_save();
	return BLK_OK;
}

/*----------------------------------------------------------------------------
 * screen
 *--------------------------------------------------------------------------*/
static void blocks_enter(void) {
	uint16_t len;

	if (!blk_image(&len)) {
		store_load();
	}
	gfx_clear();
	blk_start(&kit_io, hal_millis());
}

static void blocks_key(uint8_t btn) {
	blk_key(btn);
}

static void blocks_frame(uint32_t now_ms) {
	char buf[12];

	blk_run(now_ms);

	switch (blk_state()) {
	case BLK_EMPTY:
		gfx_clear();
		ui_title("BLOCKS", "");
		gfx_text_center(16, "no program yet", 1);
		gfx_text_center(30, "draw one with blocks:", 1);
		gfx_text_center(40, "play/blocks.html", 1);
		ui_footer("", "", "hold:MENU");
		break;

	case BLK_ERROR: {					/* over whatever the program drew */
		int x;

		gfx_fill(0, GFX_H - 9, GFX_W, 9, 0);
		x = gfx_text(0, GFX_H - 8, "error ", 1);
		x = gfx_text(x, GFX_H - 8, gfx_utoa(buf, blk_error(), 1), 1);
		x = gfx_text(x, GFX_H - 8, " at byte ", 1);
		gfx_text(x, GFX_H - 8, gfx_utoa(buf, blk_pc(), 1), 1);
	}
		break;

	default:
		break;
	}
}

static void blocks_leave(void) {
	blk_stop();
	ui_led_release();
}

const ui_screen_t scr_blocks = { "Blocks", blocks_enter, blocks_key, blocks_frame, blocks_leave };
