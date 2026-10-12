/**
 * Dino runner: jump the cacti, duck under the birds.
 *   B1: jump (also starts the game and restarts after a crash)
 *   B2: duck, for as long as the button is held
 * The ground speeds up with the score. Positions are in 1/16 pixel.
 */
#include "ui.h"

#define GROUND_Y		(56)		/* feet stand on this row */
#define DINO_X			(12)
#define DINO_W			(13)
#define DINO_H			(14)
#define DUCK_W			(16)
#define DUCK_H			(9)
#define JUMP_V			(-80)		/* 1/16 pixel per frame */
#define GRAVITY			(9)
#define OBS_NUM			(3)
#define BIRD_Y			(38)		/* hits a standing dino, clears a ducking one */

enum { OBS_NONE = 0, OBS_CACTUS, OBS_CACTUS_BIG, OBS_BIRD };

typedef struct {
	int16_t x;
	uint8_t kind;
} obs_t;

/* bit 15 = leftmost pixel */
static const uint16_t dino_body[12] = {
	0b0000001111111000,
	0b0000011111111100,
	0b0000011011111100,
	0b0000011111111100,
	0b0000011111000000,
	0b0000011111111000,
	0b1000011111000000,
	0b1000111111100000,
	0b1101111110100000,
	0b1111111110000000,
	0b0111111110000000,
	0b0011111100000000,
};
static const uint16_t dino_legs[2][2] = {
	{ 0b0011001100000000, 0b0010000000000000 },
	{ 0b0011001100000000, 0b0000001000000000 },
};
static const uint16_t dino_duck[2][DUCK_H] = {
	{
		0b0000000000111110,
		0b1000000011111111,
		0b1111111111011111,
		0b1111111111111111,
		0b0111111111110000,
		0b0011111111101100,
		0b0011111110000000,
		0b0001101100000000,
		0b0001000000000000,
	},
	{
		0b0000000000111110,
		0b1000000011111111,
		0b1111111111011111,
		0b1111111111111111,
		0b0111111111110000,
		0b0011111111101100,
		0b0011111110000000,
		0b0001101100000000,
		0b0000001000000000,
	},
};
static const uint16_t cactus_small[10] = {
	0b0010000000000000,
	0b0010000000000000,
	0b1010000000000000,
	0b1010100000000000,
	0b1010100000000000,
	0b1110100000000000,
	0b0011100000000000,
	0b0010000000000000,
	0b0010000000000000,
	0b0010000000000000,
};
static const uint16_t cactus_big[14] = {
	0b0001100000000000,
	0b0001100000000000,
	0b0001101000000000,
	0b1001101000000000,
	0b1001101000000000,
	0b1001101000000000,
	0b1001111000000000,
	0b1111100000000000,
	0b0001100000000000,
	0b0001100000000000,
	0b0001100000000000,
	0b0001100000000000,
	0b0001100000000000,
	0b0001100000000000,
};
static const uint16_t bird[2][7] = {
	{
		0b0000100000000000,
		0b0001100000000000,
		0b0111110000000000,
		0b1111111111110000,
		0b0011111110000000,
		0b0001111100000000,
		0b0000000000000000,
	},
	{
		0b0000000000000000,
		0b0000000000000000,
		0b0111110000000000,
		0b1111111111110000,
		0b0011111110000000,
		0b0001111000000000,
		0b0000110000000000,
	},
};

static obs_t obs[OBS_NUM];
static int16_t y16, vy;			/* height above the ground, 1/16 pixel, <= 0 */
static uint16_t score, best;
static uint16_t frames;
static uint8_t state;			/* 0 ready, 1 running, 2 crashed */
static uint8_t duck_frames;		/* duck requested without a held button (shell, autoplay) */
static uint8_t idle_frames;
static uint8_t ground_shift;

static uint8_t speed(void) {
	uint8_t s = (uint8_t)(3 + score / 150);

	return s > 6 ? 6 : s;
}

static void obs_size(uint8_t kind, int* w, int* h, int* top) {
	switch (kind) {
	case OBS_CACTUS:		*w = 5;  *h = 10; *top = GROUND_Y - 10; break;
	case OBS_CACTUS_BIG:	*w = 7;  *h = 14; *top = GROUND_Y - 14; break;
	default:				*w = 12; *h = 6;  *top = BIRD_Y;        break;
	}
}

static int16_t far_x(void) {
	int16_t f = GFX_W;

	for (uint8_t i = 0; i < OBS_NUM; i++) {
		if (obs[i].kind != OBS_NONE && obs[i].x > f) {
			f = obs[i].x;
		}
	}
	return f;
}

static void spawn(obs_t* o) {
	uint32_t r = ui_rand();

	o->x = (int16_t)(far_x() + 55 + speed() * 8 + r % 60);
	/* birds only once the player has seen some cacti */
	if (score > 120 && (r >> 8) % 4 == 0) {
		o->kind = OBS_BIRD;
	}
	else {
		o->kind = ((r >> 12) & 1) ? OBS_CACTUS_BIG : OBS_CACTUS;
	}
}

static void dino_enter(void) {
	for (uint8_t i = 0; i < OBS_NUM; i++) {
		obs[i].kind = OBS_NONE;
	}
	for (uint8_t i = 0; i < OBS_NUM; i++) {
		spawn(&obs[i]);
	}
	y16 = 0;
	vy = 0;
	score = 0;
	frames = 0;
	state = 0;
	duck_frames = 0;
	idle_frames = 0;
}

static uint8_t ducking(void) {
	return y16 == 0 && (duck_frames || (kit_buttons() & KIT_BTN_2));
}

static void dino_key(uint8_t btn) {
	if (btn == KIT_BTN_3) {
		return;
	}
	if (state == 2) {
		dino_enter();
		return;
	}
	state = 1;
	if (btn == KIT_BTN_1) {
		if (y16 == 0) {
			vy = JUMP_V;
			duck_frames = 0;
			ui_beep(1500, 12);
		}
	}
	else {
		duck_frames = 12;
	}
}

static void crash(void) {
	state = 2;
	idle_frames = 0;
	if (score > best) {
		best = score;
	}
	ui_beep(250, 400);
}

static void autopilot(void) {
	const obs_t* next = 0;
	int w, h, top, dist;

	for (uint8_t i = 0; i < OBS_NUM; i++) {
		if (obs[i].kind != OBS_NONE && obs[i].x + 12 > DINO_X && (!next || obs[i].x < next->x)) {
			next = &obs[i];
		}
	}
	if (!next) {
		return;
	}
	obs_size(next->kind, &w, &h, &top);
	dist = next->x - (DINO_X + DINO_W);
	if (next->kind == OBS_BIRD) {
		if (dist < speed() * 6 && dist > -w - DUCK_W) {
			duck_frames = 3;
		}
	}
	else if (y16 == 0 && dist >= 0 && dist < speed() * 5) {
		dino_key(KIT_BTN_1);
	}
}

static void update(void) {
	uint8_t v = speed();
	int dx = DINO_X + 2, dw, dy, dh;

	if (duck_frames) {
		duck_frames--;
	}
	vy = (int16_t)(vy + GRAVITY);
	y16 = (int16_t)(y16 + vy);
	if (y16 >= 0) {
		y16 = 0;
		vy = 0;
	}

	frames++;
	if (frames % 2 == 0) {
		score++;
		if (score % 100 == 0) {
			ui_beep(2600, 40);
		}
	}
	ground_shift = (uint8_t)(ground_shift + v);

	/* hit box: a little smaller than the picture */
	if (ducking()) {
		dw = DUCK_W - 4; dh = DUCK_H - 2; dy = GROUND_Y - DUCK_H + 1;
	}
	else {
		dw = DINO_W - 4; dh = DINO_H - 3; dy = GROUND_Y - DINO_H + y16 / 16 + 1;
	}

	for (uint8_t i = 0; i < OBS_NUM; i++) {
		obs_t* o = &obs[i];
		int w, h, top;

		obs_size(o->kind, &w, &h, &top);
		o->x = (int16_t)(o->x - v);
		if (o->x + w < 0) {
			o->kind = OBS_NONE;
			spawn(o);
			continue;
		}
		if (dx < o->x + w - 1 && dx + dw > o->x + 1 && dy < top + h && dy + dh > top + 1) {
			crash();
			return;
		}
	}
}

static void dino_frame(uint32_t now_ms) {
	char buf[12];
	uint8_t step = (uint8_t)((frames / 3) & 1);
	int y = GROUND_Y + y16 / 16;

	(void)now_ms;
	if (ui_autoplay) {
		if (state != 1) {
			if (++idle_frames >= (state == 0 ? 15 : 40)) {
				dino_key(KIT_BTN_1);
			}
		}
		else {
			autopilot();
		}
	}
	if (state == 1) {
		update();
		y = GROUND_Y + y16 / 16;
	}

	gfx_clear();
	ui_title("DINO", gfx_utoa(buf, score, 1));

	/* ground: a line with pebbles that scroll */
	gfx_hline(0, GROUND_Y, GFX_W, 1);
	for (int x = -(ground_shift % 16); x < GFX_W; x += 16) {
		gfx_hline(x + 3, GROUND_Y + 2, 2, 1);
		gfx_pixel(x + 11, GROUND_Y + 4, 1);
	}

	for (uint8_t i = 0; i < OBS_NUM; i++) {
		const obs_t* o = &obs[i];

		if (o->kind == OBS_CACTUS) {
			gfx_bitmap(o->x, GROUND_Y - 10, cactus_small, 10);
		}
		else if (o->kind == OBS_CACTUS_BIG) {
			gfx_bitmap(o->x, GROUND_Y - 14, cactus_big, 14);
		}
		else if (o->kind == OBS_BIRD) {
			gfx_bitmap(o->x, BIRD_Y, bird[(frames / 4) & 1], 7);
		}
	}

	if (state == 1 && ducking()) {
		gfx_bitmap(DINO_X, GROUND_Y - DUCK_H, dino_duck[step], DUCK_H);
	}
	else {
		gfx_bitmap(DINO_X, y - DINO_H, dino_body, 12);
		/* legs run on the ground, hang still in the air */
		gfx_bitmap(DINO_X, y - 2, dino_legs[(y16 == 0 && state == 1) ? step : 0], 2);
	}

	if (state == 0) {
		gfx_text_center(20, "B1 jump  B2 duck", 1);
	}
	else if (state == 2) {
		ui_game_over(16, best);
	}
}

const ui_screen_t scr_dino = { "Dino runner", dino_enter, dino_key, dino_frame, 0 };
