/**
 * Kernel side of the demo UI: the UI task, button polling, beeps.
 * See ui.h for the overall picture.
 */
#include <string.h>

#include "ak.h"
#include "task.h"
#include "timer.h"

#include "hal.h"
#include "ak_log.h"

#include "ui.h"
#include "music.h"
#include "video.h"
#include "weather.h"
#include "plot.h"
#include "blk_vm.h"
#include "blocks.h"
#include "app.h"
#include "fw_types.h"
#include "hal_rs485.h"
#include "task_list.h"

#define TAG "UI"

#define BTN_DEBOUNCE_MS		(15)

uint8_t ui_last_pages;
uint16_t ui_last_frame_ms;

static const ui_screen_t* current;		/* 0 = menu */
static uint8_t lcd_ok;

/* screen mirror (shell "ui dump" / "ui stream") */
#define STREAM_CHARS_PER_FRAME	(400)		/* about 35 ms of UART time at 115200 baud */
static ui_stream_t stream;
static uint32_t stream_next_ms;			/* the UART is busy with the last batch until then */

/*----------------------------------------------------------------------------
 * RS485 link for a screen (Pong): Modbus stops listening while it is open
 *--------------------------------------------------------------------------*/
void ui_link_open(void) {
	task_polling_set_ability(TASK_POLL_MODBUS_ID, AK_DISABLE);
	while (hal_rs485_getc() >= 0) {
	}
}

void ui_link_close(void) {
	while (hal_rs485_getc() >= 0) {
	}
	task_polling_set_ability(TASK_POLL_MODBUS_ID, AK_ENABLE);
}

int ui_link_getc(void) {
	return hal_rs485_getc();
}

void ui_link_write(const uint8_t* data, uint8_t len) {
	hal_rs485_write(data, len);
}

/*----------------------------------------------------------------------------
 * status LED for a screen (the heartbeat stops toggling it meanwhile)
 *--------------------------------------------------------------------------*/
void ui_led(uint8_t on) {
	app_led_held = 1;
	hal_led_set(on);
}

void ui_led_release(void) {
	app_led_held = 0;
}

/*----------------------------------------------------------------------------
 * beep: buzzer on now, a one-shot timer turns it off
 *--------------------------------------------------------------------------*/
void ui_beep(uint16_t freq_hz, uint16_t ms) {
	if (music_playing()) {
		return;				/* the buzzer belongs to the song */
	}
	kit_buzzer(freq_hz);
	timer_set(TASK_UI_ID, UI_SIG_BEEP_OFF, ms, TIMER_ONE_SHOT);
}

/*----------------------------------------------------------------------------
 * music: music_step() sounds the next piece and says how long it lasts; a
 * one-shot timer brings UI_SIG_NOTE when that time is over
 *--------------------------------------------------------------------------*/
static void music_next(void) {
	uint16_t ms = music_step();

	if (ms) {
		timer_set(TASK_UI_ID, UI_SIG_NOTE, ms, TIMER_ONE_SHOT);
	}
}

void ui_music_play(const char* rtttl) {
	timer_remove_attr(TASK_UI_ID, UI_SIG_BEEP_OFF);		/* a pending beep-off would cut the first note */
	if (music_start(rtttl)) {
		music_next();
	}
}

void ui_music_stop(void) {
	timer_remove_attr(TASK_UI_ID, UI_SIG_NOTE);
	music_stop();
}

/*----------------------------------------------------------------------------
 * buttons: polled from the main loop, debounced, turned into messages.
 * B1 / B2 report on press (games react at once). B3 reports on release as a
 * short press, or as BACK once it has been held for UI_HOLD_MS.
 *--------------------------------------------------------------------------*/
void task_poll_buttons(void) {
	static uint32_t last_ms;
	static uint8_t raw, stable;
	static uint32_t b3_down_ms;
	static uint8_t b3_long_sent;
	uint32_t now = hal_millis();
	uint8_t sample, pressed, released;

	if (current == &scr_pong) {
		pong_poll();			/* answers the other kit without waiting for the next frame */
	}
	if (now - last_ms < BTN_DEBOUNCE_MS) {
		return;
	}
	last_ms = now;

	sample = kit_buttons();
	if (sample != raw) {		/* still bouncing: wait for two equal samples */
		raw = sample;
		return;
	}
	pressed = (uint8_t)(sample & ~stable);
	released = (uint8_t)(stable & ~sample);
	stable = sample;
	if (pressed & (KIT_BTN_1 | KIT_BTN_2)) {
		ui_autoplay = 0;		/* a player took over */
	}

	if (pressed & KIT_BTN_1) {
		task_post_pure_msg(TASK_UI_ID, UI_SIG_KEY_1);
	}
	if (pressed & KIT_BTN_2) {
		task_post_pure_msg(TASK_UI_ID, UI_SIG_KEY_2);
	}
	if (pressed & KIT_BTN_3) {
		b3_down_ms = now;
		b3_long_sent = 0;
	}
	if ((stable & KIT_BTN_3) && !b3_long_sent && now - b3_down_ms >= UI_HOLD_MS) {
		b3_long_sent = 1;
		task_post_pure_msg(TASK_UI_ID, UI_SIG_BACK);
	}
	if ((released & KIT_BTN_3) && !b3_long_sent) {
		task_post_pure_msg(TASK_UI_ID, UI_SIG_KEY_3);
	}
}

/*----------------------------------------------------------------------------
 * shell: look at the demo and press its buttons over the console
 *--------------------------------------------------------------------------*/
void cmd_ui(const char* args) {
	if (args[0] >= '1' && args[0] <= '3' && args[1] == 0) {
		task_post_pure_msg(TASK_UI_ID, (uint8_t)(UI_SIG_KEY_1 + (args[0] - '1')));
	}
	else if (args[0] == 'b') {
		task_post_pure_msg(TASK_UI_ID, UI_SIG_BACK);
	}
	else if (args[0] == 'd') {
		/* the whole screen once; a running stream just sends it all again */
		stream.dirty = 0xFF;
		stream.todo = 0xFF;
		stream_next_ms = 0;
		if (stream.mode == UI_STREAM_OFF) {
			stream.mode = UI_STREAM_ONCE;
		}
	}
	else if (args[0] == 's') {
		stream.mode = (stream.mode == UI_STREAM_ON) ? UI_STREAM_OFF : UI_STREAM_ON;
		stream.dirty = 0xFF;
		stream.todo = 0;
		stream_next_ms = 0;
		xprintf("stream %s\n", stream.mode ? "on" : "off");
	}
	else if (args[0] == 'o') {
		const char* name = args;

		while (*name && *name != ' ') {
			name++;
		}
		while (*name == ' ') {
			name++;
		}
		if (!ui_open(name)) {
			xprintf("no screen named like that. usage: ui open <part of a menu name> | ui open menu\n");
		}
	}
	else if (args[0] == 'a') {
		ui_autoplay = !ui_autoplay;
		xprintf("autoplay %s\n", ui_autoplay ? "on: open a game, it plays itself" : "off");
	}
	else {
		xprintf("display %s, screen: %s, last frame %d ms, %d of %d pages sent, buttons 0x%02X\n",
				lcd_ok ? "ok" : "NOT ANSWERING", current ? current->name : "menu",
				ui_last_frame_ms, ui_last_pages, KIT_LCD_PAGES, kit_buttons());
	}
}

static void open_screen(const ui_screen_t* s);

void cmd_plot(const char* args) {
	int32_t v = 0;
	uint8_t neg = (args[0] == '-');
	const char* p = args + neg;

	if (*p < '0' || *p > '9') {
		xprintf("usage: plot <number>   (-32768..32767, shown on the Scope screen)\n");
		return;
	}
	while (*p >= '0' && *p <= '9' && v < 100000) {
		v = v * 10 + (*p++ - '0');
	}
	if (neg) {
		v = -v;
	}
	plot_add((int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v));
}

void cmd_th(const char* args) {
	char a[8], b[8];
	int16_t t;
	uint16_t h;

	if (!weather_now(&t, &h)) {
		xprintf("no answer from the SHT45\n");
		return;
	}
	xprintf("%s C, %s %%RH, %d samples, one every %d s\n", weather_fmt(a, t), weather_fmt(b, h),
			weather_count(), weather_period_s());
	if (args[0] == 'c') {
		xprintf("n,temp_c,rh_pct\n");
		for (uint8_t i = 0; i < weather_count(); i++) {
			weather_sample(i, &t, &h);
			xprintf("%d,%s,%s\n", i, weather_fmt(a, t), weather_fmt(b, h));
		}
	}
}

/* "i2c": scan the I2C1 header. "i2c <addr> <reg> [n]": read n bytes (1..16)
 * starting at a register; numbers in hex. */
static const char* i2c_hex(const char* p, uint16_t* v) {
	uint8_t n = 0;

	*v = 0;
	while (*p == ' ') {
		p++;
	}
	for (;; p++, n++) {
		char c = *p;

		if (c >= '0' && c <= '9') {
			*v = (uint16_t)((*v << 4) | (uint16_t)(c - '0'));
		}
		else if (c >= 'a' && c <= 'f') {
			*v = (uint16_t)((*v << 4) | (uint16_t)(c - 'a' + 10));
		}
		else if (c >= 'A' && c <= 'F') {
			*v = (uint16_t)((*v << 4) | (uint16_t)(c - 'A' + 10));
		}
		else {
			break;
		}
	}
	return n ? p : (const char*)0;
}

void cmd_i2c(const char* args) {
	uint8_t buf[16];
	uint16_t addr, reg, len = 1;
	uint8_t found = 0;
	const char* p = args;

	while (*p == ' ') {
		p++;
	}
	if (*p == 'w') {					/* "i2c w": watch the idle lines */
		uint32_t sh, dh, se, de;
		const uint32_t n = 400000;

		kit_i2c_watch(n, &sh, &dh, &se, &de);
		xprintf("SCL high %d%% edges %d   SDA high %d%% edges %d   (%d samples)\n", (int)(sh / (n / 100)),
				(int)se, (int)(dh / (n / 100)), (int)de, (int)n);
		return;
	}
	if (*p == 0) {
		for (addr = 0x08; addr < 0x78; addr++) {
			if (kit_i2c_probe((uint8_t)addr)) {
				xprintf("0x%02X ", addr);
				found++;
			}
		}
		xprintf("\n%d device(s)\n", found);
		return;
	}
	p = i2c_hex(p, &addr);
	if (p) {
		p = i2c_hex(p, &reg);
	}
	if (!p || addr > 0x7F || reg > 0xFF) {
		xprintf("usage: i2c | i2c <addr> <reg> [n]   (hex, n up to 10 = 16 bytes)\n");
		return;
	}
	if (i2c_hex(p, &len) == (const char*)0) {
		len = 1;
	}
	if (len < 1 || len > sizeof(buf)) {
		len = sizeof(buf);
	}
	if (!kit_i2c_read((uint8_t)addr, (uint8_t)reg, buf, (uint8_t)len)) {
		xprintf("no answer from 0x%02X\n", addr);
		return;
	}
	xprintf("0x%02X[0x%02X]:", addr, reg);
	for (uint8_t i = 0; i < len; i++) {
		xprintf(" %02X", buf[i]);
	}
	xprintf("\n");
}

#if defined(APP_KIT_SPI_SNIFF)
static const char* dec_num(const char* p, uint16_t* v) {
	uint8_t n = 0;

	*v = 0;
	while (*p == ' ') {
		p++;
	}
	while (*p >= '0' && *p <= '9' && *v < 6000) {
		*v = (uint16_t)(*v * 10 + (*p++ - '0'));
		n++;
	}
	return n ? p : (const char*)0;
}

/* "spi start [mode]" arms the sniffer (mode = CPOL*2 + CPHA, default 0),
 * "spi" shows what came in, "spi dump [from]" prints frames, 24 per call,
 * "spi stop" hands SPI1 back to the flash. */
void cmd_spi(const char* args) {
	const char* p = args;
	const uint8_t* d;
	uint16_t bytes, frames, len, v, i, n;
	uint32_t dt;
	uint8_t on;

	while (*p == ' ') {
		p++;
	}
	if (strncmp(p, "start", 5) == 0) {
		uint16_t trig = 0x100;			/* "spi start [mode] [trig]": trig = first byte (hex) that starts the capture */
		const char* q = dec_num(p + 5, &v);

		if (!q) {
			v = 0;
		}
		else if (!i2c_hex(q, &trig) || trig > 0xFF) {
			trig = 0x100;
		}
		kit_spi_sniff_start((uint8_t)(v & 3), trig);
		xprintf("sniffing: J6 NSS PA4, SCK PA5, MOSI PA7, mode %d, %d bytes / %d frames max", v & 3,
				KIT_SPI_SNIFF_BUF, KIT_SPI_SNIFF_FRAMES);
		if (trig <= 0xFF) {
			xprintf(", starts at a frame beginning with %02X", trig);
		}
		xprintf("\n");
		return;
	}
	if (strncmp(p, "stop", 4) == 0) {
		kit_spi_sniff_stop();
		xprintf("stopped, SPI1 back to the flash\n");
		return;
	}
	if (*p == 'w') {					/* "spi w": which wire is which */
		static const char* const name[3] = { "PA4", "PA5", "PA7" };
		uint32_t high[3], edges[3];
		const uint32_t cnt = 400000;

		/* "spi w", "spi wu", "spi wd": no pull, pull-up, pull-down */
		kit_spi_watch(cnt, (uint8_t)(p[1] == 'u' ? 1 : p[1] == 'd' ? 2 : 0), high, edges);
		for (uint8_t k = 0; k < 3; k++) {
			xprintf("%s high %3d%% edges %6d\n", name[k], (int)(high[k] / (cnt / 100)), (int)edges[k]);
		}
		xprintf("most edges = SCK -> PA5, fewest = CSN -> PA4, the other = DATA -> PA7\n");
		return;
	}
	on = kit_spi_sniff_status(&bytes, &frames);
	if (strncmp(p, "dump", 4) == 0) {
		if (!dec_num(p + 4, &v)) {
			v = 0;
		}
		for (i = v, n = 0; n < 24 && kit_spi_sniff_frame(i, &d, &len, &dt); i++, n++) {
			xprintf("%3d +%5d us %2d:", i, (int)dt, len);
			for (uint16_t j = 0; j < len && j < 40; j++) {
				xprintf(" %02X", d[j]);
			}
			if (len > 40) {
				xprintf(" ...");
			}
			xprintf("\n");
		}
		if (i < frames) {
			xprintf("more: spi dump %d\n", i);
		}
		else if (n == 0) {
			xprintf("no frame %d (%d captured)\n", v, frames);
		}
		return;
	}
	xprintf("%s, %d bytes, %d frames%s%s\n", on ? "on" : "off", bytes, frames,
			bytes >= KIT_SPI_SNIFF_BUF ? " (buffer full)" : "",
			frames >= KIT_SPI_SNIFF_FRAMES ? " (frame table full)" : "");
	if (*p) {
		xprintf("usage: spi start [mode] | spi | spi dump [from] | spi stop\n");
	}
}
#endif /* APP_KIT_SPI_SNIFF */

/*----------------------------------------------------------------------------
 * media store over the console protocol (tools/ak_video.py)
 *   40 INFO   -                 size(4) sector(4)
 *   41 ERASE  offset(4)         -          one sector
 *   42 WRITE  offset(4) data    -          checked by reading back
 *
 * block program (docs/play/blocks.html, see blocks.h)
 *   48 BEGIN  length(2)         max(2)     the running program stops
 *   49 DATA   offset(2) data    -
 *   4A END    crc16(2)          -          checked, kept, started on the Blocks screen
 *--------------------------------------------------------------------------*/
static uint32_t get_u32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_u32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

uint8_t ui_proto_ext(uint8_t cmd, const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t* resp_len) {
	switch (cmd) {
	case 0x40:
		put_u32(&resp[0], kit_store_size());
		put_u32(&resp[4], KIT_STORE_SECTOR);
		*resp_len = 8;
		return FW_OK;

	case 0x41:
		if (len != 4) {
			return FW_ERR_ARG;
		}
		if (current == &scr_video) {
			open_screen(0);			/* the clip on screen is about to disappear */
		}
		return kit_store_erase(get_u32(req)) ? FW_OK : FW_ERR_FLASH;

	case 0x42: {
		uint8_t back[64];
		uint32_t off;

		if (len <= 4) {
			return FW_ERR_ARG;
		}
		off = get_u32(req);
		req += 4;
		len = (uint16_t)(len - 4);
		if (!kit_store_write(off, req, len)) {
			return FW_ERR_FLASH;
		}
		while (len) {
			uint16_t n = len > sizeof(back) ? (uint16_t)sizeof(back) : len;

			if (!kit_store_read(off, back, n) || memcmp(back, req, n) != 0) {
				return FW_ERR_FLASH;
			}
			off += n;
			req += n;
			len = (uint16_t)(len - n);
		}
		return FW_OK;
	}

	case 0x48:
		resp[0] = (uint8_t)BLK_MAX_SIZE;
		resp[1] = (uint8_t)(BLK_MAX_SIZE >> 8);
		*resp_len = 2;
		if (len != 2) {
			return FW_ERR_ARG;
		}
		return blocks_begin((uint16_t)(req[0] | (req[1] << 8))) ? FW_OK : FW_ERR_SIZE;

	case 0x49:
		if (len <= 2) {
			return FW_ERR_ARG;
		}
		return blocks_data((uint16_t)(req[0] | (req[1] << 8)), req + 2, (uint16_t)(len - 2)) ? FW_OK : FW_ERR_OFFSET;

	case 0x4A:
		if (len != 2) {
			return FW_ERR_ARG;
		}
		if (blocks_end((uint16_t)(req[0] | (req[1] << 8))) != BLK_OK) {
			return FW_ERR_IMG_CRC;
		}
		open_screen(&scr_blocks);			/* starts it */
		return FW_OK;

	default:
		return FW_ERR_CMD;
	}
}

/* "blk": what the block program is doing. "blk run": start it again.
 * "blk stop": stop it. */
void cmd_blk(const char* args) {
	static const char* const names[] = { "no program", "running", "waiting", "ended", "error" };
	uint16_t len;

	if (args[0] == 'r') {
		if (!ui_open("blocks")) {
			xprintf("no Blocks screen\n");
		}
		return;
	}
	if (args[0] == 's') {
		blk_stop();
	}
	(void)blk_image(&len);
	xprintf("%s, %d of %d bytes, at byte %d", names[blk_state()], len, BLK_MAX_SIZE, blk_pc());
	if (blk_state() == BLK_ERROR) {
		xprintf(", error %d", blk_error());
	}
	xprintf("\n");
}

/*----------------------------------------------------------------------------
 * UI task
 *--------------------------------------------------------------------------*/
static void open_screen(const ui_screen_t* s) {
	if (current && current->leave) {
		current->leave();
	}
	current = s;
	if (s) {
		s->enter();
	}
	else {
		menu_enter();
	}
}

uint8_t ui_open(const char* text) {
	const ui_screen_t* s = menu_find(text);

	if (s) {
		open_screen(s);
		return 1;
	}
	if (text[0] == 'm' && text[1] == 'e' && text[2] == 'n' && text[3] == 'u' && text[4] == 0) {
		open_screen(0);
		return 1;
	}
	return 0;
}

static void key(uint8_t btn) {
	if (current) {
		current->key(btn);
	}
	else {
		const ui_screen_t* chosen = menu_key(btn);
		if (chosen) {
			open_screen(chosen);
		}
	}
}

void task_ui(ak_msg_t* msg) {
	uint32_t t0;

	switch (msg->sig) {
	case UI_SIG_INIT:
		lcd_ok = kit_init();
		if (!lcd_ok) {
			LOG_W(TAG, "display does not answer, demo runs without it\n");
		}
		open_screen(0);
		gfx_clear();
		gfx_flush(1);
		timer_set(TASK_UI_ID, UI_SIG_FRAME, UI_FRAME_MS, TIMER_ONE_SHOT);
		ui_beep(2000, 60);
		LOG_I(TAG, "demo started: %d ms per frame\n", UI_FRAME_MS);
		break;

	case UI_SIG_FRAME:
		t0 = hal_millis();
		weather_poll(t0);
		if (current) {
			current->frame(t0);
		}
		else {
			menu_frame(t0);
		}
		ui_last_pages = lcd_ok ? gfx_flush(0) : 0;
		ui_last_frame_ms = (uint16_t)(hal_millis() - t0);
		stream.dirty |= gfx_changed();
		if (stream.mode != UI_STREAM_OFF && (int32_t)(t0 - stream_next_ms) >= 0) {
			uint16_t chars = ui_stream_batch(&stream, STREAM_CHARS_PER_FRAME, hal_console_putc);

			if (chars) {
				xprintf("@E\n");		/* end of the batch: the PC redraws */
				stream_next_ms = hal_millis() + chars / 11U;
			}
		}
		/* The next frame is asked for only now. A periodic timer would keep
		 * posting while a full-screen effect needs more than UI_FRAME_MS, and
		 * the message pool would fill up; this way the effect just runs slower. */
		timer_set(TASK_UI_ID, UI_SIG_FRAME,
				  ui_last_frame_ms + 2U < UI_FRAME_MS ? (uint32_t)(UI_FRAME_MS - ui_last_frame_ms) : 2U, TIMER_ONE_SHOT);
		break;

	case UI_SIG_KEY_1:
		key(KIT_BTN_1);
		break;

	case UI_SIG_KEY_2:
		key(KIT_BTN_2);
		break;

	case UI_SIG_KEY_3:
		key(KIT_BTN_3);
		break;

	case UI_SIG_BACK:
		if (current) {
			ui_beep(1200, 40);
			open_screen(0);
		}
		break;

	case UI_SIG_BEEP_OFF:
		if (!music_playing()) {
			kit_buzzer(0);
		}
		break;

	case UI_SIG_NOTE:
		music_next();
		break;

	default:
		break;
	}
}
