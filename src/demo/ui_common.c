/**
 * Chip- and kernel-independent parts of the demo UI: menu, layout helpers,
 * random numbers. Shared by the firmware and the host renderer.
 */
#include "ui.h"
#include "crc.h"

/*----------------------------------------------------------------------------
 * helpers
 *--------------------------------------------------------------------------*/
uint8_t ui_autoplay;

static uint32_t rng_state = 0x2545F491UL;

uint32_t ui_rand(void) {
	/* xorshift32 */
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

/* 127 * sin(2 * pi * i / 256) */
static const int8_t sin_tbl[256] = {
	   0,    3,    6,    9,   12,   16,   19,   22,   25,   28,   31,   34,   37,   40,   43,   46,
	  49,   51,   54,   57,   60,   63,   65,   68,   71,   73,   76,   78,   81,   83,   85,   88,
	  90,   92,   94,   96,   98,  100,  102,  104,  106,  107,  109,  111,  112,  113,  115,  116,
	 117,  118,  120,  121,  122,  122,  123,  124,  125,  125,  126,  126,  126,  127,  127,  127,
	 127,  127,  127,  127,  126,  126,  126,  125,  125,  124,  123,  122,  122,  121,  120,  118,
	 117,  116,  115,  113,  112,  111,  109,  107,  106,  104,  102,  100,   98,   96,   94,   92,
	  90,   88,   85,   83,   81,   78,   76,   73,   71,   68,   65,   63,   60,   57,   54,   51,
	  49,   46,   43,   40,   37,   34,   31,   28,   25,   22,   19,   16,   12,    9,    6,    3,
	   0,   -3,   -6,   -9,  -12,  -16,  -19,  -22,  -25,  -28,  -31,  -34,  -37,  -40,  -43,  -46,
	 -49,  -51,  -54,  -57,  -60,  -63,  -65,  -68,  -71,  -73,  -76,  -78,  -81,  -83,  -85,  -88,
	 -90,  -92,  -94,  -96,  -98, -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116,
	-117, -118, -120, -121, -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127,
	-127, -127, -127, -127, -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118,
	-117, -116, -115, -113, -112, -111, -109, -107, -106, -104, -102, -100,  -98,  -96,  -94,  -92,
	 -90,  -88,  -85,  -83,  -81,  -78,  -76,  -73,  -71,  -68,  -65,  -63,  -60,  -57,  -54,  -51,
	 -49,  -46,  -43,  -40,  -37,  -34,  -31,  -28,  -25,  -22,  -19,  -16,  -12,   -9,   -6,   -3,
};

int ui_sin(uint8_t a) {
	return sin_tbl[a];
}

int ui_cos(uint8_t a) {
	return sin_tbl[(uint8_t)(a + 64)];
}

/*----------------------------------------------------------------------------
 * screen mirror: a page as PackBits (see video.h) in hex
 *--------------------------------------------------------------------------*/
static void (*dump_out)(uint8_t c);
static uint16_t dump_chars;

static void dump_char(char c) {
	dump_out((uint8_t)c);
	dump_chars++;
}

static void dump_hex(uint8_t v) {
	static const char digits[] = "0123456789ABCDEF";

	dump_char(digits[v >> 4]);
	dump_char(digits[v & 15]);
}

uint16_t ui_stream_batch(ui_stream_t* st, uint16_t max_chars, void (*out)(uint8_t c)) {
	const uint8_t mask = (st->mode == UI_STREAM_ONCE) ? st->todo
						 : (st->mode == UI_STREAM_ON) ? st->dirty : 0;
	uint16_t chars = 0;

	for (uint8_t i = 0; i < KIT_LCD_PAGES && chars < max_chars; i++) {
		const uint8_t page = (uint8_t)((st->next + i) % KIT_LCD_PAGES);
		const uint8_t bit = (uint8_t)(1u << page);

		if (mask & bit) {
			chars = (uint16_t)(chars + ui_dump_page(page, out));
			st->dirty &= (uint8_t)~bit;
			st->todo &= (uint8_t)~bit;
			st->next = (uint8_t)((page + 1u) % KIT_LCD_PAGES);
		}
	}
	if (st->mode == UI_STREAM_ONCE && st->todo == 0) {
		st->mode = UI_STREAM_OFF;
	}
	return chars;
}

uint16_t ui_dump_page(uint8_t page, void (*out)(uint8_t c)) {
	const uint8_t* p = gfx_page(page);
	uint16_t crc = crc16_update(CRC16_INIT, p, GFX_W);
	uint16_t i = 0;

	dump_out = out;
	dump_chars = 0;
	dump_char('@');
	dump_char('P');
	dump_char((char)('0' + page));
	dump_char(' ');
	while (i < GFX_W) {
		uint16_t run = 1;

		while (i + run < GFX_W && p[i + run] == p[i] && run < 129) {
			run++;
		}
		if (run >= 2) {
			dump_hex((uint8_t)(run + 126));
			dump_hex(p[i]);
			i = (uint16_t)(i + run);
		}
		else {
			uint16_t lit = 1;

			/* bytes as they are, up to the next pair of equal ones */
			while (i + lit < GFX_W && lit < 128 && !(i + lit + 1 < GFX_W && p[i + lit] == p[i + lit + 1])) {
				lit++;
			}
			dump_hex((uint8_t)(lit - 1));
			for (uint16_t k = 0; k < lit; k++) {
				dump_hex(p[i + k]);
			}
			i = (uint16_t)(i + lit);
		}
	}
	dump_char(' ');
	dump_hex((uint8_t)(crc >> 8));
	dump_hex((uint8_t)crc);
	dump_char('\n');
	return dump_chars;
}

/* Title bar: inverted strip of 9 pixels with a left and a right text. */
void ui_title(const char* left, const char* right) {
	gfx_text(2, 1, left, 1);
	if (right) {
		gfx_text(GFX_W - 2 - gfx_text_width(right, 1), 1, right, 1);
	}
	gfx_invert(0, 0, GFX_W, 9);
}

/* Bottom line: what the three buttons do, each above its button. */
void ui_footer(const char* b1, const char* b2, const char* b3) {
	gfx_hline(0, 54, GFX_W, 1);
	if (b1) {
		gfx_text(1, 56, b1, 1);
	}
	if (b2) {
		/* centred in what the two outer labels leave free */
		int left = b1 ? 1 + gfx_text_width(b1, 1) : 0;
		int right = b3 ? GFX_W - 1 - gfx_text_width(b3, 1) : GFX_W;

		gfx_text(left + (right - left - gfx_text_width(b2, 1)) / 2, 56, b2, 1);
	}
	if (b3) {
		gfx_text(GFX_W - 1 - gfx_text_width(b3, 1), 56, b3, 1);
	}
}

/*----------------------------------------------------------------------------
 * menu
 *--------------------------------------------------------------------------*/
static const ui_screen_t* const menu_items[] = {
	&scr_clock,
	&scr_snake,
	&scr_flappy,
	&scr_dino,
	&scr_tetris,
	&scr_breakout,
	&scr_invaders,
	&scr_pong,
	&scr_cube,
	&scr_maze,
	&scr_music,
	&scr_video,
	&scr_weather,
	&scr_plot,
	&scr_blocks,
	&scr_saver,
	&scr_system,
};

#define MENU_NUM	((uint8_t)(sizeof(menu_items) / sizeof(menu_items[0])))

#define MENU_LINES	(4)

static uint8_t menu_sel;
static uint8_t menu_top;			/* first entry shown */

void menu_enter(void) {
}

static char lower(char c) {
	return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

const ui_screen_t* menu_find(const char* text) {
	if (!text[0]) {
		return 0;
	}
	for (uint8_t i = 0; i < MENU_NUM; i++) {
		for (const char* at = menu_items[i]->name; *at; at++) {
			uint8_t k = 0;

			while (text[k] && lower(at[k]) == lower(text[k])) {
				k++;
			}
			if (!text[k]) {
				menu_sel = i;
				return menu_items[i];
			}
		}
	}
	return 0;
}

const ui_screen_t* menu_key(uint8_t btn) {
	if (btn == KIT_BTN_1) {
		menu_sel = (uint8_t)((menu_sel + 1) % MENU_NUM);
		ui_beep(1800, 15);
	}
	else if (btn == KIT_BTN_2) {
		menu_sel = (uint8_t)((menu_sel + MENU_NUM - 1) % MENU_NUM);
		ui_beep(1800, 15);
	}
	else if (btn == KIT_BTN_3) {
		ui_beep(2400, 40);
		return menu_items[menu_sel];
	}
	return 0;
}

void menu_frame(uint32_t now_ms) {
	(void)now_ms;

	gfx_clear();
	ui_title("AK MCU KIT", "demo");
	if (menu_sel < menu_top) {
		menu_top = menu_sel;
	}
	else if (menu_sel >= menu_top + MENU_LINES) {
		menu_top = (uint8_t)(menu_sel - MENU_LINES + 1);
	}
	for (uint8_t line = 0; line < MENU_LINES && menu_top + line < MENU_NUM; line++) {
		uint8_t i = (uint8_t)(menu_top + line);
		int y = 12 + line * 10;

		gfx_text(10, y + 1, menu_items[i]->name, 1);
		if (i == menu_sel) {
			gfx_text(2, y + 1, ">", 1);
			gfx_invert(0, y, GFX_W - 4, 9);
		}
	}
	/* scroll bar: where the window sits in the list */
	gfx_vline(GFX_W - 2, 12, MENU_LINES * 10 - 1, 1);
	gfx_fill(GFX_W - 3, 12 + menu_top * (MENU_LINES * 10 - 1) / MENU_NUM, 3,
			 MENU_LINES * (MENU_LINES * 10 - 1) / MENU_NUM + 1, 1);
	ui_footer("DOWN", "UP", "OPEN");
}
