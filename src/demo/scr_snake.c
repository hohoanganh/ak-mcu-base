/**
 * Snake on a 31 x 10 grid of 4-pixel cells.
 *   B1: turn left, B2: turn right (seen from the snake), B3: pause
 * The snake speeds up as it grows. Hitting the wall or itself ends the game;
 * B1 or B2 starts a new one.
 */
#include "ui.h"

#define CELL		(4)
#define GRID_W		(31)
#define GRID_H		(10)
#define FIELD_X		(2)
#define FIELD_Y		(10)
#define MAX_LEN		(160)

typedef struct {
	int8_t x, y;
} cell_t;

static cell_t* body;				/* ring buffer, head at body[head]; in ui_scratch() */

typedef char snake_body_fits[(MAX_LEN * sizeof(cell_t) <= UI_SCRATCH_SIZE) ? 1 : -1];
static uint16_t head, len;
static int8_t dx, dy;
static int8_t turn;				/* -1 left, +1 right, applied at the next step */
static cell_t food;
static uint16_t score, best;
static uint8_t frames, paused, dead;
static uint8_t dead_frames;

static cell_t seg(uint16_t i) {		/* i = 0 is the head */
	return body[(head + MAX_LEN - i) % MAX_LEN];
}

static uint8_t on_snake(int8_t x, int8_t y) {
	for (uint16_t i = 0; i < len; i++) {
		cell_t c = seg(i);
		if (c.x == x && c.y == y) {
			return 1;
		}
	}
	return 0;
}

static void place_food(void) {
	do {
		food.x = (int8_t)(ui_rand() % GRID_W);
		food.y = (int8_t)(ui_rand() % GRID_H);
	} while (on_snake(food.x, food.y));
}

static void snake_enter(void) {
	body = (cell_t*)ui_scratch();
	head = 2;
	len = 3;
	for (uint8_t i = 0; i < 3; i++) {
		body[i].x = (int8_t)(4 + i);
		body[i].y = GRID_H / 2;
	}
	dx = 1;
	dy = 0;
	turn = 0;
	score = 0;
	frames = 0;
	paused = 0;
	dead = 0;
	dead_frames = 0;
	place_food();
}

static uint8_t cell_free(int8_t x, int8_t y) {
	return x >= 0 && x < GRID_W && y >= 0 && y < GRID_H && !on_snake(x, y);
}

/* Autoplay: of the three moves (straight, left, right) take the one that
 * stays alive, does not enter a dead end and gets closer to the food. Good
 * for a minute of play, not a perfect player. */
static int8_t autopilot(void) {
	static const int8_t turns[3] = { 0, -1, 1 };
	cell_t h = seg(0);
	int best_cost = 0x7FFF;
	int8_t best_turn = 0;

	for (uint8_t i = 0; i < 3; i++) {
		int8_t ndx = dx, ndy = dy;
		int8_t nx, ny;
		int cost, room;

		if (turns[i] < 0) {
			ndx = dy; ndy = (int8_t)-dx;
		}
		else if (turns[i] > 0) {
			ndx = (int8_t)-dy; ndy = dx;
		}
		nx = (int8_t)(h.x + ndx);
		ny = (int8_t)(h.y + ndy);
		if (!cell_free(nx, ny)) {
			continue;
		}
		room = cell_free((int8_t)(nx + 1), ny) + cell_free((int8_t)(nx - 1), ny) +
			   cell_free(nx, (int8_t)(ny + 1)) + cell_free(nx, (int8_t)(ny - 1));
		cost = (nx > food.x ? nx - food.x : food.x - nx) + (ny > food.y ? ny - food.y : food.y - ny);
		if (room == 0 && cost != 0) {
			cost += 100;		/* a dead end, unless the food is right there */
		}
		if (cost < best_cost) {
			best_cost = cost;
			best_turn = turns[i];
		}
	}
	return best_turn;
}

static void snake_key(uint8_t btn) {
	if (dead) {
		if (btn != KIT_BTN_3) {
			snake_enter();
		}
		return;
	}
	if (btn == KIT_BTN_1) {
		turn = -1;
	}
	else if (btn == KIT_BTN_2) {
		turn = 1;
	}
	else {
		paused = !paused;
	}
}

static void step(void) {
	cell_t h = seg(0);
	int8_t t;

	/* left: (dx, dy) -> (dy, -dx), right: (dx, dy) -> (-dy, dx) (y grows downwards) */
	if (turn < 0) {
		t = dx; dx = dy; dy = (int8_t)-t;
	}
	else if (turn > 0) {
		t = dx; dx = (int8_t)-dy; dy = t;
	}
	turn = 0;

	h.x = (int8_t)(h.x + dx);
	h.y = (int8_t)(h.y + dy);
	if (h.x < 0 || h.x >= GRID_W || h.y < 0 || h.y >= GRID_H || on_snake(h.x, h.y)) {
		dead = 1;
		if (score > best) {
			best = score;
		}
		ui_beep(300, 400);
		return;
	}

	head = (uint16_t)((head + 1) % MAX_LEN);
	body[head] = h;
	if (h.x == food.x && h.y == food.y) {
		score++;
		if (len < MAX_LEN - 1) {
			len++;
		}
		ui_beep(2600, 30);
		place_food();
	}
}

static void snake_frame(uint32_t now_ms) {
	char buf[12];
	/* one step every 6 frames at the start, down to every 2 frames */
	uint8_t period = (uint8_t)(score >= 20 ? 2 : 6 - score / 5);

	(void)now_ms;
	if (ui_autoplay) {
		if (dead) {
			if (++dead_frames >= 40) {		/* show the score for two seconds, then again */
				snake_enter();
			}
		}
		else if (!paused && frames + 1 >= period) {
			turn = autopilot();
		}
	}
	if (!paused && !dead && ++frames >= period) {
		frames = 0;
		step();
	}

	gfx_clear();
	ui_title("SNAKE", gfx_utoa(buf, score, 1));
	gfx_rect(FIELD_X - 2, FIELD_Y - 1, GRID_W * CELL + 4, GRID_H * CELL + 3, 1);
	for (uint16_t i = 0; i < len; i++) {
		cell_t c = seg(i);
		gfx_fill(FIELD_X + c.x * CELL, FIELD_Y + c.y * CELL, CELL - 1, CELL - 1, 1);
	}
	/* food: a small cross */
	gfx_fill(FIELD_X + food.x * CELL + 1, FIELD_Y + food.y * CELL, 1, 3, 1);
	gfx_fill(FIELD_X + food.x * CELL, FIELD_Y + food.y * CELL + 1, 3, 1, 1);

	if (dead) {
		ui_game_over(18, best);
		ui_footer("NEW GAME", 0, "hold:MENU");
	}
	else if (paused) {
		gfx_fill(40, 24, 48, 12, 0);
		gfx_rect(40, 24, 48, 12, 1);
		gfx_text_center(26, "PAUSE", 1);
		ui_footer("LEFT", "RIGHT", "GO");
	}
	else {
		ui_footer("LEFT", "RIGHT", "PAUSE");
	}
}

const ui_screen_t scr_snake = { "Snake", snake_enter, snake_key, snake_frame, 0 };
