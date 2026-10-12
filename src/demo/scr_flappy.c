/**
 * Flappy: keep the bird between the pipes.
 *   B1 or B2: flap (also starts the game and restarts after a crash)
 * Positions are in 1/16 pixel so gravity can be gentle at 20 frames/s.
 */
#include "ui.h"

#define FIELD_TOP		(10)
#define FIELD_BOTTOM	(63)
#define BIRD_X			(24)
#define BIRD_SIZE		(5)
#define PIPE_W			(10)
#define PIPE_NUM		(3)
#define PIPE_SPACING	(52)
#define GAP_H			(24)
#define GRAVITY			(11)		/* 1/16 pixel per frame^2 */
#define FLAP_V			(-48)		/* 1/16 pixel per frame */
#define SCROLL			(2)			/* pixels per frame */

typedef struct {
	int16_t x;				/* left edge */
	int16_t gap_y;			/* top of the gap */
	uint8_t passed;
} pipe_t;

static pipe_t pipes[PIPE_NUM];
static int16_t bird_y16, bird_v;	/* 1/16 pixel */
static uint16_t score, best;
static uint8_t state;				/* 0 ready, 1 flying, 2 crashed */
static uint8_t idle_frames;

static int16_t random_gap(void) {
	return (int16_t)(FIELD_TOP + 3 + ui_rand() % (FIELD_BOTTOM - FIELD_TOP - GAP_H - 6));
}

static void flappy_enter(void) {
	for (uint8_t i = 0; i < PIPE_NUM; i++) {
		pipes[i].x = (int16_t)(GFX_W + 20 + i * PIPE_SPACING);
		pipes[i].gap_y = random_gap();
		pipes[i].passed = 0;
	}
	bird_y16 = (int16_t)(((FIELD_TOP + FIELD_BOTTOM) / 2 - BIRD_SIZE / 2) * 16);
	bird_v = 0;
	score = 0;
	state = 0;
	idle_frames = 0;
}

static void flappy_key(uint8_t btn) {
	if (btn == KIT_BTN_3) {
		return;
	}
	if (state == 2) {
		flappy_enter();
		return;
	}
	state = 1;
	bird_v = FLAP_V;
	ui_beep(1500, 12);
}

static void crash(void) {
	state = 2;
	if (score > best) {
		best = score;
	}
	ui_beep(250, 400);
}

static void update(void) {
	int16_t y;

	bird_v = (int16_t)(bird_v + GRAVITY);
	bird_y16 = (int16_t)(bird_y16 + bird_v);
	y = (int16_t)(bird_y16 / 16);

	if (y < FIELD_TOP) {
		bird_y16 = FIELD_TOP * 16;
		bird_v = 0;
		y = FIELD_TOP;
	}
	if (y + BIRD_SIZE > FIELD_BOTTOM) {
		crash();
		return;
	}

	for (uint8_t i = 0; i < PIPE_NUM; i++) {
		pipe_t* p = &pipes[i];

		p->x = (int16_t)(p->x - SCROLL);
		if (p->x + PIPE_W < 0) {
			/* behind the bird: comes back in after the last pipe */
			int16_t far = 0;
			for (uint8_t k = 0; k < PIPE_NUM; k++) {
				if (pipes[k].x > far) {
					far = pipes[k].x;
				}
			}
			p->x = (int16_t)(far + PIPE_SPACING);
			p->gap_y = random_gap();
			p->passed = 0;
		}
		if (BIRD_X + BIRD_SIZE > p->x && BIRD_X < p->x + PIPE_W &&
				(y < p->gap_y || y + BIRD_SIZE > p->gap_y + GAP_H)) {
			crash();
			return;
		}
		if (!p->passed && p->x + PIPE_W < BIRD_X) {
			p->passed = 1;
			score++;
			ui_beep(2600, 25);
		}
	}
}

static void flappy_frame(uint32_t now_ms) {
	char buf[12];
	int y;

	(void)now_ms;
	if (ui_autoplay) {
		if (state != 1) {
			if (++idle_frames >= (state == 0 ? 15 : 40)) {
				flappy_key(KIT_BTN_1);		/* start, or again after showing the score */
			}
		}
		else {
			/* flap when falling below a line near the bottom of the next gap */
			const pipe_t* next = 0;
			int16_t line;

			for (uint8_t i = 0; i < PIPE_NUM; i++) {
				if (pipes[i].x + PIPE_W >= BIRD_X && (!next || pipes[i].x < next->x)) {
					next = &pipes[i];
				}
			}
			line = (int16_t)((next ? next->gap_y + GAP_H - 4 : (FIELD_TOP + FIELD_BOTTOM) / 2 + 8) - BIRD_SIZE);
			if (bird_v >= 0 && bird_y16 / 16 >= line - 3) {
				flappy_key(KIT_BTN_1);
			}
		}
	}
	if (state == 1) {
		update();
	}
	y = bird_y16 / 16;

	gfx_clear();
	for (uint8_t i = 0; i < PIPE_NUM; i++) {
		const pipe_t* p = &pipes[i];

		gfx_fill(p->x, FIELD_TOP, PIPE_W, p->gap_y - FIELD_TOP, 1);
		gfx_fill(p->x - 1, p->gap_y - 3, PIPE_W + 2, 3, 1);							/* lip */
		gfx_fill(p->x, p->gap_y + GAP_H, PIPE_W, FIELD_BOTTOM - p->gap_y - GAP_H, 1);
		gfx_fill(p->x - 1, p->gap_y + GAP_H, PIPE_W + 2, 3, 1);
	}
	gfx_hline(0, FIELD_BOTTOM, GFX_W, 1);

	/* bird: body, eye, beak */
	gfx_fill(BIRD_X, y, BIRD_SIZE, BIRD_SIZE, 1);
	gfx_pixel(BIRD_X + 3, y + 1, 0);
	gfx_fill(BIRD_X + BIRD_SIZE, y + 2, 2, 1, 1);

	gfx_fill(0, 0, GFX_W, 9, 0);
	ui_title("FLAPPY", gfx_utoa(buf, score, 1));

	if (state == 0) {
		gfx_fill(18, 46, 92, 12, 0);
		gfx_rect(18, 46, 92, 12, 1);
		gfx_text_center(48, "B1 / B2: FLAP", 1);
	}
	else if (state == 2) {
		ui_game_over(20, best);
	}
}

const ui_screen_t scr_flappy = { "Flappy", flappy_enter, flappy_key, flappy_frame, 0 };
