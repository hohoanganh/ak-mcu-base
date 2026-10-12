/**
 * Block programs: the stack machine. See blk_vm.h.
 */

#include <string.h>

#include "blk_vm.h"

enum {
	OP_END = 0x00, OP_PUSH8, OP_PUSH16, OP_LOAD, OP_STORE,
	OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG,
	OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
	OP_AND, OP_OR, OP_NOT,
	OP_JMP, OP_JZ, OP_WAIT, OP_MIN,
	OP_LED, OP_BEEP, OP_BTN, OP_HIT, OP_CLS, OP_TEXT, OP_NUM, OP_PIXEL,
	OP_LINE, OP_RECT, OP_FILL, OP_TEMP, OP_HUMI, OP_RAND, OP_TIME, OP_MAX, OP_ABS, OP_ERASE,
};

#define TEXT_MAX	(21)		/* one line of the display at size 1 */

static uint8_t image[BLK_MAX_SIZE];
static uint16_t image_len;

static const blk_io_t* io;
static int32_t vars[BLK_VARS];
static int32_t stack[BLK_STACK];
static uint8_t sp;
static uint16_t pc;
static uint8_t state;
static uint8_t error;
static uint8_t hits;			/* buttons pressed and not asked for yet */
static uint32_t start_ms;
static uint32_t wake_ms;

uint8_t blk_load(const uint8_t* img, uint16_t len) {
	blk_stop();
	image_len = 0;
	state = BLK_EMPTY;
	if (len < BLK_HEADER + 1 || len > BLK_MAX_SIZE || img[0] != 'B' || img[1] != 'K'
			|| img[2] != BLK_VERSION || img[3] > BLK_VARS) {
		return BLK_ERR_IMAGE;
	}
	if (img != image) {
		memcpy(image, img, len);
	}
	image_len = len;
	state = BLK_ENDED;			/* loaded, not started */
	return BLK_OK;
}

uint8_t* blk_buffer(void) {
	blk_stop();
	image_len = 0;
	state = BLK_EMPTY;
	return image;
}

const uint8_t* blk_image(uint16_t* len) {
	*len = image_len;
	return image_len ? image : (const uint8_t*)0;
}

void blk_start(const blk_io_t* kit, uint32_t now_ms) {
	io = kit;
	error = BLK_OK;
	hits = 0;
	sp = 0;
	pc = BLK_HEADER;
	start_ms = now_ms;
	memset(vars, 0, sizeof(vars));
	state = image_len ? BLK_RUNNING : BLK_EMPTY;
}

void blk_stop(void) {
	if (state == BLK_RUNNING || state == BLK_WAITING) {
		state = BLK_ENDED;
	}
}

void blk_key(uint8_t mask) {
	hits |= mask;
}

uint8_t blk_state(void) {
	return state;
}

uint8_t blk_error(void) {
	return error;
}

uint16_t blk_pc(void) {
	return pc;
}

static void fail(uint8_t why, uint16_t at) {
	error = why;
	pc = at;
	state = BLK_ERROR;
}

/* "-123" into buf (12 bytes) */
static void itoa32(char* buf, int32_t v) {
	char tmp[11];
	uint32_t u = (v < 0) ? (uint32_t)(-(v + 1)) + 1u : (uint32_t)v;
	uint8_t n = 0;

	do {
		tmp[n++] = (char)('0' + u % 10u);
		u /= 10u;
	} while (u);
	if (v < 0) {
		*buf++ = '-';
	}
	while (n) {
		*buf++ = tmp[--n];
	}
	*buf = 0;
}

static uint8_t text_size(int32_t v) {
	return (uint8_t)(v < 1 ? 1 : v > 4 ? 4 : v);
}

void blk_run(uint32_t now_ms) {
	uint16_t steps = BLK_STEPS_PER_RUN;

	if (state == BLK_WAITING) {
		if ((int32_t)(now_ms - wake_ms) < 0) {
			return;
		}
		state = BLK_RUNNING;
	}

/* a = the n values an instruction takes, a[0] written first */
#define NEED(n)		if (sp < (n)) { fail(BLK_ERR_STACK, at); return; } sp = (uint8_t)(sp - (n)); a = &stack[sp]
#define PUSH(v)		do { if (sp >= BLK_STACK) { fail(BLK_ERR_STACK, at); return; } stack[sp++] = (int32_t)(v); } while (0)
#define OPERAND(n)	if ((uint32_t)pc + (n) > image_len) { fail(BLK_ERR_RANGE, at); return; }

	while (state == BLK_RUNNING && steps--) {
		const uint16_t at = pc;
		const int32_t* a;
		uint8_t op;

		if (pc >= image_len) {				/* ran off the end: same as END */
			state = BLK_ENDED;
			return;
		}
		op = image[pc++];

		switch (op) {
		case OP_END:
			state = BLK_ENDED;
			pc = at;
			return;

		case OP_PUSH8:
			OPERAND(1);
			PUSH((int8_t)image[pc]);
			pc++;
			break;

		case OP_PUSH16:
			OPERAND(2);
			PUSH((int16_t)(image[pc] | (image[pc + 1] << 8)));
			pc = (uint16_t)(pc + 2);
			break;

		case OP_LOAD:
			OPERAND(1);
			if (image[pc] >= BLK_VARS) { fail(BLK_ERR_RANGE, at); return; }
			PUSH(vars[image[pc]]);
			pc++;
			break;

		case OP_STORE:
			OPERAND(1);
			if (image[pc] >= BLK_VARS) { fail(BLK_ERR_RANGE, at); return; }
			NEED(1);
			vars[image[pc]] = a[0];
			pc++;
			break;

		/* unsigned arithmetic: wraps instead of being undefined */
		case OP_ADD:	NEED(2); PUSH((uint32_t)a[0] + (uint32_t)a[1]);	break;
		case OP_SUB:	NEED(2); PUSH((uint32_t)a[0] - (uint32_t)a[1]);	break;
		case OP_MUL:	NEED(2); PUSH((uint32_t)a[0] * (uint32_t)a[1]);	break;

		case OP_DIV:						/* by zero, and the one overflow: 0 */
			NEED(2);
			PUSH((a[1] == 0 || (a[0] == INT32_MIN && a[1] == -1)) ? 0 : a[0] / a[1]);
			break;

		case OP_MOD:
			NEED(2);
			PUSH((a[1] == 0 || a[1] == -1) ? 0 : a[0] % a[1]);
			break;

		case OP_NEG:	NEED(1); PUSH(0u - (uint32_t)a[0]);		break;
		case OP_EQ:		NEED(2); PUSH(a[0] == a[1]);			break;
		case OP_NE:		NEED(2); PUSH(a[0] != a[1]);			break;
		case OP_LT:		NEED(2); PUSH(a[0] < a[1]);				break;
		case OP_LE:		NEED(2); PUSH(a[0] <= a[1]);			break;
		case OP_GT:		NEED(2); PUSH(a[0] > a[1]);				break;
		case OP_GE:		NEED(2); PUSH(a[0] >= a[1]);			break;
		case OP_AND:	NEED(2); PUSH(a[0] && a[1]);			break;
		case OP_OR:		NEED(2); PUSH(a[0] || a[1]);			break;
		case OP_NOT:	NEED(1); PUSH(!a[0]);					break;
		case OP_MIN:	NEED(2); PUSH(a[0] < a[1] ? a[0] : a[1]);	break;
		case OP_MAX:	NEED(2); PUSH(a[0] > a[1] ? a[0] : a[1]);	break;
		case OP_ABS:	NEED(1); PUSH(a[0] < 0 ? 0u - (uint32_t)a[0] : (uint32_t)a[0]);	break;

		case OP_JMP:
		case OP_JZ: {
			int32_t to;

			OPERAND(2);
			to = (int32_t)pc + 2 + (int16_t)(image[pc] | (image[pc + 1] << 8));
			pc = (uint16_t)(pc + 2);
			if (op == OP_JZ) {
				NEED(1);
				if (a[0]) {
					break;
				}
			}
			if (to < BLK_HEADER || to > (int32_t)image_len) { fail(BLK_ERR_RANGE, at); return; }
			pc = (uint16_t)to;
		}
			break;

		case OP_WAIT:
			NEED(1);
			wake_ms = now_ms + (uint32_t)(a[0] < 0 ? 0 : a[0] > 3600000 ? 3600000 : a[0]);
			state = BLK_WAITING;
			return;

		case OP_LED:
			NEED(1);
			io->led(a[0] ? 1 : 0);
			break;

		case OP_BEEP:
			NEED(2);
			if (a[0] > 0 && a[1] > 0) {
				io->beep((uint16_t)(a[0] > 20000 ? 20000 : a[0]), (uint16_t)(a[1] > 10000 ? 10000 : a[1]));
			}
			break;

		case OP_BTN:
		case OP_HIT: {
			uint8_t bit;

			OPERAND(1);
			bit = (uint8_t)(1u << ((image[pc] - 1u) & 7u));
			pc++;
			if (op == OP_BTN) {
				PUSH((io->buttons() & bit) ? 1 : 0);
			}
			else {
				PUSH((hits & bit) ? 1 : 0);
				hits &= (uint8_t)~bit;
			}
		}
			break;

		case OP_CLS:
			io->cls();
			break;

		case OP_TEXT: {
			char s[TEXT_MAX + 1];
			uint8_t n;

			OPERAND(1);
			n = image[pc];
			OPERAND(1u + n);
			NEED(3);
			memcpy(s, &image[pc + 1], n > TEXT_MAX ? TEXT_MAX : n);
			s[n > TEXT_MAX ? TEXT_MAX : n] = 0;
			pc = (uint16_t)(pc + 1 + n);
			io->text((int)a[0], (int)a[1], s, text_size(a[2]));
		}
			break;

		case OP_NUM: {
			char s[12];

			NEED(4);
			itoa32(s, a[0]);
			io->text((int)a[1], (int)a[2], s, text_size(a[3]));
		}
			break;

		case OP_PIXEL:	NEED(3); io->pixel((int)a[0], (int)a[1], a[2] ? 1 : 0);				break;
		case OP_LINE:	NEED(4); io->line((int)a[0], (int)a[1], (int)a[2], (int)a[3]);		break;
		case OP_RECT:	NEED(4); io->rect((int)a[0], (int)a[1], (int)a[2], (int)a[3], 0);	break;
		case OP_FILL:	NEED(4); io->rect((int)a[0], (int)a[1], (int)a[2], (int)a[3], 1);	break;
		case OP_ERASE:	NEED(4); io->rect((int)a[0], (int)a[1], (int)a[2], (int)a[3], 2);	break;
		case OP_TEMP:	PUSH(io->temp());	break;
		case OP_HUMI:	PUSH(io->humi());	break;

		case OP_RAND: {						/* a..b inclusive, either order */
			int32_t lo, hi;

			NEED(2);
			lo = a[0] < a[1] ? a[0] : a[1];
			hi = a[0] < a[1] ? a[1] : a[0];
			PUSH((uint32_t)lo + io->rand() % ((uint32_t)hi - (uint32_t)lo + 1u ? (uint32_t)hi - (uint32_t)lo + 1u : 1u));
		}
			break;

		case OP_TIME:
			OPERAND(1);
			PUSH(image[pc] == 3 ? (int32_t)((now_ms - start_ms) / 1000u) : io->time(image[pc]));
			pc++;
			break;

		default:
			fail(BLK_ERR_OPCODE, at);
			return;
		}
	}
}
