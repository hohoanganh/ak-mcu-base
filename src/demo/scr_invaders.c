/**
 * Invaders: shoot the rows of aliens before they reach the ground.
 *   B1: left     B2: right     (hold)
 *   B3: fire (one shot in the air at a time)
 * The fewer aliens are left, the faster they march. A cleared wave comes back
 * one row lower.
 */
#include <string.h>

#include "ui.h"

#define TOP				(10)
#define COLS			(8)
#define ROWS			(3)
#define PITCH_X			(12)
#define PITCH_Y			(9)
#define ALIEN_W			(9)
#define ALIEN_H			(6)
#define SHIP_W			(9)
#define SHIP_Y			(GFX_H - 5)
#define SHIP_STEP		(3)
#define BOMB_NUM		(3)

/* bit 15 = leftmost pixel; two walking poses per kind of alien */
static const uint16_t alien_a[2][ALIEN_H] = {
	{ 0x1C00, 0x7F00, 0xDD80, 0xFF80, 0x2200, 0x4100 },
	{ 0x1C00, 0x7F00, 0xDD80, 0xFF80, 0x4100, 0x2200 },
};
static const uint16_t alien_b[2][ALIEN_H] = {
	{ 0x2200, 0x7F00, 0xDD80, 0xFF80, 0xA280, 0x1400 },
	{ 0xA280, 0xFF80, 0xDD80, 0x7F00, 0x2200, 0x4100 },
};
static const uint16_t ship[4] = { 0x0800, 0x1C00, 0xFF80, 0xFF80 };

typedef struct {
	int16_t x, y;
	uint8_t on;
} shot_t;

static uint8_t alive[ROWS];				/* bit c set = alien there */
static int16_t fleet_x, fleet_y;		/* top left of the formation */
static int8_t fleet_dir;
static uint8_t pose;
static uint8_t march_count;
static int16_t ship_x;
static shot_t shot;
static shot_t bombs[BOMB_NUM];
static uint8_t state;					/* 0 ready, 1 playing, 2 game over */
static uint8_t lives, wave;
static uint16_t score, best;
static uint8_t idle_frames;
static int8_t move_frames, move_dir;

static uint8_t count_alive(void) {
	uint8_t n = 0;

	for (uint8_t r = 0; r < ROWS; r++) {
		for (uint8_t v = alive[r]; v; v &= (uint8_t)(v - 1)) {
			n++;
		}
	}
	return n;
}

static void new_wave(void) {
	memset(alive, 0xFF, sizeof(alive));
	fleet_x = 2;
	fleet_y = (int16_t)(TOP + 2 + (wave > 4 ? 4 : wave) * 3);
	fleet_dir = 1;
	march_count = 0;
	shot.on = 0;
	memset(bombs, 0, sizeof(bombs));
}

static void invaders_enter(void) {
	lives = 3;
	wave = 0;
	score = 0;
	state = 0;
	idle_frames = 0;
	move_frames = 0;
	ship_x = (GFX_W - SHIP_W) / 2;
	new_wave();
}

static void fire(void) {
	if (!shot.on) {
		shot.on = 1;
		shot.x = (int16_t)(ship_x + SHIP_W / 2);
		shot.y = SHIP_Y - 3;
		ui_beep(2200, 10);
	}
}

static void invaders_key(uint8_t btn) {
	if (state == 2) {
		invaders_enter();
		return;
	}
	state = 1;
	if (btn == KIT_BTN_3) {
		fire();
	}
	else {
		move_dir = (btn == KIT_BTN_1) ? -1 : 1;
		move_frames = 2;
	}
}

static void lose_life(void) {
	lives--;
	ui_beep(250, 300);
	memset(bombs, 0, sizeof(bombs));
	if (lives == 0) {
		state = 2;
		idle_frames = 0;
		if (score > best) {
			best = score;
		}
	}
}

/* leftmost and rightmost column that still has an alien */
static void fleet_span(int* first, int* last) {
	uint8_t any = (uint8_t)(alive[0] | alive[1] | alive[2]);

	*first = 0;
	*last = COLS - 1;
	while (*first < COLS - 1 && !((any >> *first) & 1)) {
		(*first)++;
	}
	while (*last > 0 && !((any >> *last) & 1)) {
		(*last)--;
	}
}

static void march(void) {
	uint8_t n = count_alive();
	/* 24 aliens: a step every 10 frames; the last one: every frame */
	uint8_t every = (uint8_t)(1 + n * 9 / (COLS * ROWS));
	int first, last;

	if (++march_count < every) {
		return;
	}
	march_count = 0;
	pose ^= 1;
	fleet_span(&first, &last);
	if ((fleet_dir > 0 && fleet_x + last * PITCH_X + ALIEN_W + 2 > GFX_W)
			|| (fleet_dir < 0 && fleet_x + first * PITCH_X - 2 < 0)) {
		fleet_dir = (int8_t)-fleet_dir;
		fleet_y += 3;
	}
	else {
		fleet_x = (int16_t)(fleet_x + fleet_dir * 2);
	}

	/* the lowest alien of a random column drops a bomb now and then */
	if (ui_rand() % 3 == 0) {
		uint8_t c = (uint8_t)(ui_rand() % COLS);

		for (int r = ROWS - 1; r >= 0; r--) {
			if ((alive[r] >> c) & 1) {
				for (uint8_t i = 0; i < BOMB_NUM; i++) {
					if (!bombs[i].on) {
						bombs[i].on = 1;
						bombs[i].x = (int16_t)(fleet_x + c * PITCH_X + ALIEN_W / 2);
						bombs[i].y = (int16_t)(fleet_y + r * PITCH_Y + ALIEN_H);
						break;
					}
				}
				break;
			}
		}
	}
}

static void autopilot(void) {
	int centre = ship_x + SHIP_W / 2;
	int target = centre;
	int first, last;

	/* a bomb coming down on the ship: step aside, away from the nearer edge */
	for (uint8_t i = 0; i < BOMB_NUM; i++) {
		if (bombs[i].on && bombs[i].y > SHIP_Y - 24 && bombs[i].x > ship_x - 3 && bombs[i].x < ship_x + SHIP_W + 3) {
			move_dir = (int8_t)((bombs[i].x >= centre) ? -1 : 1);
			if (ship_x < 6) {
				move_dir = 1;
			}
			if (ship_x > GFX_W - SHIP_W - 6) {
				move_dir = -1;
			}
			move_frames = 1;
			return;
		}
	}
	/* else get under the nearest column that still has aliens, and shoot */
	fleet_span(&first, &last);
	{
		int best_d = 1000;

		for (int c = first; c <= last; c++) {
			if ((alive[0] | alive[1] | alive[2]) & (1 << c)) {
				int x = fleet_x + c * PITCH_X + ALIEN_W / 2 + fleet_dir * 4;
				int d = x > centre ? x - centre : centre - x;

				if (d < best_d) {
					best_d = d;
					target = x;
				}
			}
		}
	}
	if (target > centre + 2) {
		move_dir = 1;
		move_frames = 1;
	}
	else if (target < centre - 2) {
		move_dir = -1;
		move_frames = 1;
	}
	else {
		fire();
	}
}

static void step(void) {
	uint8_t held = kit_buttons();
	int dir = 0;

	if (ui_autoplay) {
		autopilot();
	}
	if (move_frames) {
		move_frames--;
		dir = move_dir;
	}
	else if (held & KIT_BTN_1) {
		dir = -1;
	}
	else if (held & KIT_BTN_2) {
		dir = 1;
	}
	ship_x = (int16_t)(ship_x + dir * SHIP_STEP);
	if (ship_x < 0) {
		ship_x = 0;
	}
	if (ship_x > GFX_W - SHIP_W) {
		ship_x = GFX_W - SHIP_W;
	}

	march();

	if (shot.on) {
		int c, r;

		shot.y -= 4;
		c = (shot.x - fleet_x) / PITCH_X;
		r = (shot.y - fleet_y) / PITCH_Y;
		if (shot.y < TOP) {
			shot.on = 0;
		}
		else if (shot.x >= fleet_x && shot.y >= fleet_y && c < COLS && r < ROWS && ((alive[r] >> c) & 1)
				 && (shot.x - fleet_x) % PITCH_X < ALIEN_W && (shot.y - fleet_y) % PITCH_Y < ALIEN_H + 3) {
			alive[r] &= (uint8_t)~(1 << c);
			shot.on = 0;
			score = (uint16_t)(score + (ROWS - r) * 10);
			ui_beep(900, 25);
			if (count_alive() == 0) {
				wave++;
				new_wave();
				ui_beep(2600, 200);
			}
		}
	}

	for (uint8_t i = 0; i < BOMB_NUM; i++) {
		if (!bombs[i].on) {
			continue;
		}
		bombs[i].y += 2;
		if (bombs[i].y >= GFX_H) {
			bombs[i].on = 0;
		}
		else if (bombs[i].y >= SHIP_Y && bombs[i].x >= ship_x && bombs[i].x < ship_x + SHIP_W) {
			lose_life();
			return;
		}
	}

	/* the fleet reached the ship */
	for (int r = ROWS - 1; r >= 0; r--) {
		if (alive[r]) {
			if (fleet_y + r * PITCH_Y + ALIEN_H >= SHIP_Y) {
				lives = 1;
				lose_life();
			}
			break;
		}
	}
}

static void invaders_frame(uint32_t now_ms) {
	char buf[12];

	(void)now_ms;
	if (ui_autoplay && state != 1 && ++idle_frames >= (state == 0 ? 15 : 40)) {
		invaders_key(KIT_BTN_3);
	}
	if (state == 1) {
		step();
	}

	gfx_clear();
	ui_title("INVADERS", gfx_utoa(buf, score, 1));
	for (uint8_t i = 0; i < lives; i++) {
		gfx_fill(60 + i * 5, 3, 3, 3, 0);
	}
	for (uint8_t r = 0; r < ROWS; r++) {
		for (uint8_t c = 0; c < COLS; c++) {
			if ((alive[r] >> c) & 1) {
				gfx_bitmap(fleet_x + c * PITCH_X, fleet_y + r * PITCH_Y, (r == 0 ? alien_b : alien_a)[pose], ALIEN_H);
			}
		}
	}
	gfx_bitmap(ship_x, SHIP_Y, ship, 4);
	gfx_hline(0, GFX_H - 1, GFX_W, 1);
	if (shot.on) {
		gfx_vline(shot.x, shot.y, 3, 1);
	}
	for (uint8_t i = 0; i < BOMB_NUM; i++) {
		if (bombs[i].on) {
			gfx_vline(bombs[i].x, bombs[i].y, 2, 1);
			gfx_pixel(bombs[i].x + ((bombs[i].y >> 1) & 1 ? 1 : -1), bombs[i].y + 1, 1);
		}
	}
	if (state == 0) {
		gfx_text_center(46, "B3 fire", 1);
	}
	else if (state == 2) {
		ui_game_over(22, best);
	}
}

const ui_screen_t scr_invaders = { "Invaders", invaders_enter, invaders_key, invaders_frame, 0 };
