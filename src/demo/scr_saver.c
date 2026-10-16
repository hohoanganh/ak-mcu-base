/**
 * Screen savers: three classic effects, full screen.
 *   B1: next effect (Game of Life, star field, plasma)
 *   B2: Life: new random start / stars: speed / plasma: finer or coarser
 *
 * Life runs on a 64x32 grid of 2x2 pixel cells that wraps at the edges. A row
 * is one 64-bit word, and the neighbours of all 64 cells of a row are counted
 * at once with bit operations, so a generation costs a few hundred word
 * operations instead of 2048 * 8 pixel lookups.
 */
#include <string.h>

#include "ui.h"

enum { FX_LIFE = 0, FX_STARS, FX_PLASMA, FX_NUM };

static const char* const fx_names[FX_NUM] = { "GAME OF LIFE", "STAR FIELD", "PLASMA" };

static uint8_t fx;
static uint8_t name_frames;			/* show the name for a moment after a change */
static uint32_t tick;

/*----------------------------------------------------------------------------
 * Game of Life
 *--------------------------------------------------------------------------*/
#define LIFE_W			(64)
#define LIFE_H			(32)
#define LIFE_EVERY		(2)			/* frames per generation */
#define LIFE_MAX_GEN	(900)

static uint64_t* life;				/* LIFE_H rows, in ui_scratch() */
static uint16_t life_gen;
static uint16_t life_pop, life_same;	/* population, generations it has not changed */

static void life_seed(void) {
	for (uint8_t y = 0; y < LIFE_H; y++) {
		/* two random words ANDed with a third: about one cell in three alive */
		uint64_t a = ((uint64_t)ui_rand() << 32) | ui_rand();
		uint64_t b = ((uint64_t)ui_rand() << 32) | ui_rand();

		life[y] = a & (b | (b >> 7));
	}
	life_gen = 0;
	life_same = 0;
}

static uint64_t rotl(uint64_t v) {
	return (v << 1) | (v >> 63);
}

static uint64_t rotr(uint64_t v) {
	return (v >> 1) | (v << 63);
}

static void life_step(void) {
	uint64_t next[LIFE_H];
	uint16_t pop = 0;

	for (uint8_t y = 0; y < LIFE_H; y++) {
		uint64_t up = life[(y + LIFE_H - 1) % LIFE_H], mid = life[y], dn = life[(y + 1) % LIFE_H];
		uint64_t ul = rotl(up), ur = rotr(up), dl = rotl(dn), dr = rotr(dn), ml = rotl(mid), mr = rotr(mid);
		/* the three cells above and the three below as 2-bit sums, left + right as another */
		uint64_t a0 = ul ^ up ^ ur, a1 = (ul & up) | (up & ur) | (ul & ur);
		uint64_t c0 = dl ^ dn ^ dr, c1 = (dl & dn) | (dn & dr) | (dl & dr);
		uint64_t b0 = ml ^ mr, b1 = ml & mr;
		/* add them: bit 0, bit 1 and "four or more" of the neighbour count */
		uint64_t t0 = a0 ^ c0 ^ b0, carry = (a0 & c0) | (c0 & b0) | (a0 & b0);
		uint64_t x = a1 ^ c1, z = b1 ^ carry;
		uint64_t t1 = x ^ z;
		uint64_t ge4 = (a1 & c1) | (b1 & carry) | (x & z);

		/* alive next: exactly 3 neighbours, or 2 and alive now */
		next[y] = t1 & ~ge4 & (t0 | mid);
		for (uint64_t v = next[y]; v; v &= v - 1) {
			pop++;
		}
	}
	memcpy(life, next, sizeof(next));
	life_gen++;
	life_same = (pop == life_pop) ? (uint16_t)(life_same + 1) : 0;
	life_pop = pop;
	/* died out, froze into still lifes and blinkers, or ran long enough: start again */
	if (pop == 0 || life_same > 60 || life_gen > LIFE_MAX_GEN) {
		life_seed();
	}
}

static void life_draw(void) {
	uint8_t* fb = gfx_fb();

	/* a display page holds four rows of cells; a cell is two pixels wide */
	for (uint8_t page = 0; page < KIT_LCD_PAGES; page++) {
		for (uint8_t x = 0; x < LIFE_W; x++) {
			uint8_t v = 0;

			for (uint8_t r = 0; r < 4; r++) {
				if ((life[page * 4 + r] >> (63 - x)) & 1) {
					v |= (uint8_t)(3 << (r * 2));
				}
			}
			fb[page * GFX_W + x * 2] = v;
			fb[page * GFX_W + x * 2 + 1] = v;
		}
	}
}

/*----------------------------------------------------------------------------
 * star field: stars fly towards the viewer
 *--------------------------------------------------------------------------*/
#define STAR_NUM		(40)
#define STAR_FAR		(255)

typedef struct {
	int8_t x, y;			/* direction from the centre */
	uint8_t z;				/* distance, STAR_FAR .. 1 */
} star_t;

static star_t* stars;				/* STAR_NUM, in ui_scratch(), shared with life:
									 * saver_start() seeds the effect it switches to */

typedef char saver_fits[(LIFE_H * sizeof(uint64_t) <= UI_SCRATCH_SIZE &&
						 STAR_NUM * sizeof(star_t) <= UI_SCRATCH_SIZE) ? 1 : -1];
static uint8_t star_speed = 3;

static void star_new(star_t* s, uint8_t z) {
	uint32_t r = ui_rand();

	s->x = (int8_t)(r & 0xFF);
	s->y = (int8_t)((r >> 8) & 0xFF);
	s->z = z;
}

static void stars_seed(void) {
	for (uint8_t i = 0; i < STAR_NUM; i++) {
		star_new(&stars[i], (uint8_t)(1 + ui_rand() % STAR_FAR));
	}
}

static void stars_draw(void) {
	gfx_clear();
	for (uint8_t i = 0; i < STAR_NUM; i++) {
		star_t* s = &stars[i];
		int px, py;

		if (s->z <= star_speed) {
			star_new(s, STAR_FAR);
		}
		else {
			s->z = (uint8_t)(s->z - star_speed);
		}
		px = GFX_W / 2 + s->x * 48 / s->z;
		py = GFX_H / 2 + s->y * 24 / s->z;
		if (px < 0 || px >= GFX_W || py < 0 || py >= GFX_H) {
			star_new(s, STAR_FAR);
			continue;
		}
		gfx_pixel(px, py, 1);
		if (s->z < 90) {				/* near stars are bigger */
			gfx_pixel(px + 1, py, 1);
		}
		if (s->z < 45) {
			gfx_pixel(px, py + 1, 1);
			gfx_pixel(px + 1, py + 1, 1);
		}
	}
}

/*----------------------------------------------------------------------------
 * plasma: three sine waves added up, shown as five grey levels of 2x2 pixels
 *--------------------------------------------------------------------------*/
static uint8_t plasma_zoom = 5;

static void plasma_draw(void) {
	/* 2x2 dither cells for level 0..4: the top and the bottom pixel pair */
	static const uint8_t top[5] = { 0, 1, 1, 3, 3 }, bottom[5] = { 0, 0, 2, 2, 3 };
	uint8_t* fb = gfx_fb();
	uint8_t t = (uint8_t)tick;

	for (uint8_t bx = 0; bx < GFX_W / 2; bx++) {
		int wx = ui_sin((uint8_t)(bx * plasma_zoom + t * 2));
		uint8_t col[2][KIT_LCD_PAGES];

		memset(col, 0, sizeof(col));
		for (uint8_t by = 0; by < GFX_H / 2; by++) {
			int v = wx + ui_sin((uint8_t)(by * (plasma_zoom + 3) - t * 3))
					+ ui_sin((uint8_t)((bx + by) * (plasma_zoom - 1) + t));		/* -381 .. 381 */
			uint8_t level = (uint8_t)((v + 382) * 5 / 764);
			uint8_t shift = (uint8_t)((by & 3) * 2);

			col[0][by >> 2] |= (uint8_t)((((top[level] >> 0) & 1) | (((bottom[level] >> 0) & 1) << 1)) << shift);
			col[1][by >> 2] |= (uint8_t)((((top[level] >> 1) & 1) | (((bottom[level] >> 1) & 1) << 1)) << shift);
		}
		for (uint8_t page = 0; page < KIT_LCD_PAGES; page++) {
			fb[page * GFX_W + bx * 2] = col[0][page];
			fb[page * GFX_W + bx * 2 + 1] = col[1][page];
		}
	}
}

/*----------------------------------------------------------------------------
 * the screen
 *--------------------------------------------------------------------------*/
static void saver_start(void) {
	name_frames = 30;
	if (fx == FX_LIFE) {
		life_seed();
	}
	else if (fx == FX_STARS) {
		stars_seed();
	}
}

static void saver_enter(void) {
	life = (uint64_t*)ui_scratch();
	stars = (star_t*)ui_scratch();
	tick = 0;
	saver_start();
}

static void saver_key(uint8_t btn) {
	if (btn == KIT_BTN_1) {
		fx = (uint8_t)((fx + 1) % FX_NUM);
		saver_start();
	}
	else if (btn == KIT_BTN_2) {
		if (fx == FX_LIFE) {
			life_seed();
		}
		else if (fx == FX_STARS) {
			star_speed = (uint8_t)(star_speed >= 9 ? 1 : star_speed + 2);
		}
		else {
			plasma_zoom = (uint8_t)(plasma_zoom >= 11 ? 3 : plasma_zoom + 2);
		}
	}
	ui_beep(1800, 15);
}

static void saver_frame(uint32_t now_ms) {
	(void)now_ms;
	tick++;
	if (ui_autoplay && tick % 200 == 0) {		/* attract mode: a new effect every 10 s */
		saver_key(KIT_BTN_1);
	}

	if (fx == FX_LIFE) {
		if (tick % LIFE_EVERY == 0) {
			life_step();
		}
		life_draw();
	}
	else if (fx == FX_STARS) {
		stars_draw();
	}
	else {
		plasma_draw();
	}

	if (name_frames) {
		int w = gfx_text_width(fx_names[fx], 1);

		name_frames--;
		gfx_fill((GFX_W - w) / 2 - 3, 26, w + 6, 12, 0);
		gfx_rect((GFX_W - w) / 2 - 3, 26, w + 6, 12, 1);
		gfx_text_center(28, fx_names[fx], 1);
	}
}

const ui_screen_t scr_saver = { "Screen saver", saver_enter, saver_key, saver_frame, 0 };
