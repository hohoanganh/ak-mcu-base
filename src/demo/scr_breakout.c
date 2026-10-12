/**
 * Breakout: keep the ball in play and clear the wall of bricks.
 *   B1: left     B2: right     (hold)
 *   B3: serve the ball
 * Where the ball meets the paddle decides the angle it leaves with.
 * Positions are in 1/16 pixel.
 */
#include <string.h>

#include "ui.h"

#define TOP				(10)		/* play field below the title bar */
#define BRICK_COLS		(12)
#define BRICK_ROWS		(4)
#define BRICK_W			(10)		/* pitch; the brick itself is 1 pixel narrower */
#define BRICK_H			(4)
#define BRICK_X0		(4)
#define BRICK_Y0		(TOP + 3)
#define PADDLE_W		(18)
#define PADDLE_Y		(GFX_H - 3)
#define PADDLE_STEP		(4)
#define BALL			(2)			/* a 2x2 pixel ball */

static uint16_t bricks[BRICK_ROWS];		/* bit c set = brick there */
static int16_t paddle;					/* left edge */
static int16_t bx, by, vx, vy;			/* ball, 1/16 pixel */
static uint8_t state;					/* 0 ball on the paddle, 1 flying, 2 game over */
static uint8_t lives, level;
static uint16_t score, best;
static uint8_t idle_frames;
static int8_t move_frames, move_dir;	/* movement asked by a key press (shell, tap) */

static void wall(void) {
	for (uint8_t r = 0; r < BRICK_ROWS; r++) {
		bricks[r] = (1 << BRICK_COLS) - 1;
	}
}

static void serve_pos(void) {
	state = 0;
	idle_frames = 0;
}

static void breakout_enter(void) {
	wall();
	paddle = (GFX_W - PADDLE_W) / 2;
	lives = 3;
	level = 0;
	score = 0;
	move_frames = 0;
	serve_pos();
}

static void breakout_key(uint8_t btn) {
	if (state == 2) {
		breakout_enter();
		return;
	}
	if (btn == KIT_BTN_3) {
		if (state == 0) {
			state = 1;
			vx = (int16_t)((ui_rand() & 1) ? 14 : -14);
			vy = (int16_t)(-(36 + (level > 4 ? 4 : level) * 2));
			ui_beep(1500, 12);
		}
	}
	else {
		move_dir = (btn == KIT_BTN_1) ? -1 : 1;
		move_frames = 2;
	}
}

static void bounce_beep(uint16_t hz) {
	ui_beep(hz, 8);
}

/* Removes the brick at pixel (x, y) if there is one. */
static uint8_t hit_brick(int x, int y) {
	int c, r;

	if (x < BRICK_X0 || y < BRICK_Y0) {
		return 0;
	}
	c = (x - BRICK_X0) / BRICK_W;
	r = (y - BRICK_Y0) / BRICK_H;
	if (c >= BRICK_COLS || r >= BRICK_ROWS || !((bricks[r] >> c) & 1)) {
		return 0;
	}
	/* the 1-pixel gaps right of and below a brick are not part of it */
	if ((x - BRICK_X0) % BRICK_W == BRICK_W - 1 || (y - BRICK_Y0) % BRICK_H == BRICK_H - 1) {
		return 0;
	}
	bricks[r] &= (uint16_t)~(1 << c);
	score = (uint16_t)(score + (BRICK_ROWS - r));
	bounce_beep((uint16_t)(1400 + (BRICK_ROWS - r) * 200));
	return 1;
}

static void fly(void) {
	int x, y, nx, ny;
	uint8_t left = 0;

	/* one axis at a time, so it is clear which side of a brick was hit */
	nx = (bx + vx) / 16;
	y = by / 16;
	if (nx < 0 || nx > GFX_W - BALL) {
		vx = (int16_t)-vx;
		bounce_beep(900);
	}
	else if (hit_brick(vx > 0 ? nx + BALL - 1 : nx, y) || hit_brick(vx > 0 ? nx + BALL - 1 : nx, y + BALL - 1)) {
		vx = (int16_t)-vx;
	}
	else {
		bx = (int16_t)(bx + vx);
	}

	x = bx / 16;
	ny = (by + vy) / 16;
	if (ny < TOP) {
		vy = (int16_t)-vy;
		bounce_beep(900);
	}
	else if (hit_brick(x, vy > 0 ? ny + BALL - 1 : ny) || hit_brick(x + BALL - 1, vy > 0 ? ny + BALL - 1 : ny)) {
		vy = (int16_t)-vy;
	}
	else if (vy > 0 && ny + BALL - 1 >= PADDLE_Y && by / 16 + BALL - 1 < PADDLE_Y
			 && x + BALL > paddle && x < paddle + PADDLE_W) {
		/* off the paddle: the further from its middle, the flatter the angle */
		int off = x + BALL / 2 - (paddle + PADDLE_W / 2);		/* -9 .. 9 */

		vy = (int16_t)-vy;
		vx = (int16_t)(off * 3);
		if (vx == 0) {
			vx = (int16_t)((ui_rand() & 1) ? 3 : -3);
		}
		bounce_beep(1100);
	}
	else {
		by = (int16_t)(by + vy);
	}

	if (by / 16 >= GFX_H) {					/* past the paddle */
		lives--;
		ui_beep(250, 300);
		if (lives == 0) {
			state = 2;
			idle_frames = 0;
			if (score > best) {
				best = score;
			}
		}
		else {
			serve_pos();
		}
		return;
	}

	for (uint8_t r = 0; r < BRICK_ROWS; r++) {
		left |= (uint8_t)(bricks[r] != 0);
	}
	if (!left) {							/* wall cleared: a new one, faster ball */
		level++;
		wall();
		serve_pos();
		ui_beep(2600, 200);
	}
}

static void step(void) {
	uint8_t held = kit_buttons();
	int dir = 0;

	if (ui_autoplay) {
		/* follow the ball, aiming a little off-centre so the angle keeps changing */
		int target = (state == 1 ? bx / 16 : paddle + PADDLE_W / 2) - PADDLE_W / 2 + (int)(score % 7) - 3;

		if (state == 0 && ++idle_frames > 20) {
			breakout_key(KIT_BTN_3);
		}
		if (target > paddle + 2) {
			dir = 1;
		}
		else if (target < paddle - 2) {
			dir = -1;
		}
	}
	else if (move_frames) {
		move_frames--;
		dir = move_dir;
	}
	else if (held & KIT_BTN_1) {
		dir = -1;
	}
	else if (held & KIT_BTN_2) {
		dir = 1;
	}
	paddle = (int16_t)(paddle + dir * PADDLE_STEP);
	if (paddle < 0) {
		paddle = 0;
	}
	if (paddle > GFX_W - PADDLE_W) {
		paddle = GFX_W - PADDLE_W;
	}

	if (state == 0) {						/* the ball rides on the paddle */
		bx = (int16_t)((paddle + PADDLE_W / 2 - BALL / 2) * 16);
		by = (int16_t)((PADDLE_Y - BALL) * 16);
	}
	else {
		fly();
	}
}

static void breakout_frame(uint32_t now_ms) {
	char buf[12];

	(void)now_ms;
	if (state == 2) {
		if (ui_autoplay && ++idle_frames > 40) {
			breakout_enter();
		}
	}
	else {
		step();
	}

	gfx_clear();
	ui_title("BREAKOUT", gfx_utoa(buf, score, 1));
	for (uint8_t i = 0; i < lives; i++) {		/* lives: small blocks in the title bar */
		gfx_fill(60 + i * 5, 3, 3, 3, 0);
	}
	for (uint8_t r = 0; r < BRICK_ROWS; r++) {
		for (uint8_t c = 0; c < BRICK_COLS; c++) {
			if ((bricks[r] >> c) & 1) {
				gfx_fill(BRICK_X0 + c * BRICK_W, BRICK_Y0 + r * BRICK_H, BRICK_W - 1, BRICK_H - 1, 1);
			}
		}
	}
	gfx_fill(paddle, PADDLE_Y, PADDLE_W, 2, 1);
	if (state != 2) {
		gfx_fill(bx / 16, by / 16, BALL, BALL, 1);
	}
	if (state == 0) {
		gfx_text_center(38, "B3 serve", 1);
	}
	else if (state == 2) {
		ui_game_over(28, best);
	}
}

const ui_screen_t scr_breakout = { "Breakout", breakout_enter, breakout_key, breakout_frame, 0 };
