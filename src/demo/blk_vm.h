/**
 ******************************************************************************
 * @brief:  Block programs: a small stack machine that runs what the block
 *          editor in the browser (docs/play/blocks.html) compiles.
 *
 *  A program is drawn with blocks, compiled to bytecode in the browser and
 *  sent to the kit; nothing is compiled or flashed on the way. The machine
 *  fits the kernel: blk_run() executes a bounded number of instructions and
 *  returns, "wait" just notes a time. It is called once per UI frame.
 *
 *  Image:  'B' 'K' version nvars  code...
 *  Values are 32 bit signed; 16 variables; stack of 16.
 *
 *  Code (operands follow the opcode; s16 little endian, jumps are relative to
 *  the byte after the operand):
 *    00 END                       10 GE          20 LINE   x0 y0 x1 y1
 *    01 PUSH8  s8                 11 AND         21 RECT   x y w h
 *    02 PUSH16 s16                12 OR          22 FILL   x y w h
 *    03 LOAD   var                13 NOT         23 TEMP   -> degrees C
 *    04 STORE  var                14 JMP  s16    24 HUMI   -> percent
 *    05 ADD  06 SUB  07 MUL       15 JZ   s16    25 RAND   a b -> a..b
 *    08 DIV  09 MOD  0A NEG       16 WAIT ms     26 TIME   k -> 0 hour 1 minute
 *    0B EQ   0C NE   0D LT        17 MIN         2 second 3 seconds since start
 *    0E LE   0F GT                18 LED  on     27 MAX    28 ABS
 *                                                29 ERASE  x y w h
 *                                 19 BEEP hz ms
 *                                 1A BTN  n   -> held now (n = 1..3)
 *                                 1B HIT  n   -> pressed since last asked
 *                                 1C CLS
 *                                 1D TEXT len chars   x y size
 *                                 1E NUM  value x y size
 *                                 1F PIXEL x y on
 *  Arguments are popped in the order written (the last one is on top).
 ******************************************************************************
**/

#ifndef __BLK_VM_H__
#define __BLK_VM_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define BLK_VERSION			(1)
#define BLK_HEADER			(4)
#define BLK_MAX_SIZE		(768)		/* whole image */
#define BLK_VARS			(16)
#define BLK_STACK			(16)
#define BLK_STEPS_PER_RUN	(400)		/* instructions per call of blk_run() */

/* state */
#define BLK_EMPTY			(0)			/* no program */
#define BLK_RUNNING			(1)
#define BLK_WAITING			(2)			/* inside "wait" */
#define BLK_ENDED			(3)			/* ran to its end */
#define BLK_ERROR			(4)			/* stopped: see blk_error() */

/* error */
#define BLK_OK				(0)
#define BLK_ERR_IMAGE		(1)			/* not a program, wrong version, too long */
#define BLK_ERR_OPCODE		(2)
#define BLK_ERR_STACK		(3)
#define BLK_ERR_RANGE		(4)			/* jump or variable outside the program */

/* What the machine needs from the kit. scr_blocks.c gives the real thing,
 * the tests give a recorder. */
typedef struct {
	void (*led)(uint8_t on);
	void (*beep)(uint16_t hz, uint16_t ms);
	uint8_t (*buttons)(void);					/* mask, bit 0 = button 1 */
	void (*cls)(void);
	void (*text)(int x, int y, const char* s, uint8_t size);
	void (*pixel)(int x, int y, uint8_t on);
	void (*line)(int x0, int y0, int x1, int y1);
	void (*rect)(int x, int y, int w, int h, uint8_t how);		/* 0 outline, 1 filled, 2 erased */
	int (*temp)(void);							/* degrees C */
	int (*humi)(void);							/* percent */
	uint32_t (*rand)(void);
	int (*time)(uint8_t what);					/* 0 hour, 1 minute, 2 second */
} blk_io_t;

/* Takes a program (copies it). Returns BLK_OK or BLK_ERR_IMAGE; the program
 * that was there is gone either way. */
extern uint8_t blk_load(const uint8_t* image, uint16_t len);

/* For a program that arrives in pieces: drops the loaded program and returns
 * BLK_MAX_SIZE bytes to fill; blk_load(blk_buffer(), len) then takes them
 * without copying. */
extern uint8_t* blk_buffer(void);

/* The loaded image, 0 if none. */
extern const uint8_t* blk_image(uint16_t* len);

/* (Re)starts the loaded program from its first block. */
extern void blk_start(const blk_io_t* io, uint32_t now_ms);

/* Runs until the program waits, ends, fails or used up its share. */
extern void blk_run(uint32_t now_ms);

/* A button went down (KIT_BTN_* mask): remembered for "was pressed". */
extern void blk_key(uint8_t mask);

extern void blk_stop(void);

extern uint8_t blk_state(void);
extern uint8_t blk_error(void);
extern uint16_t blk_pc(void);				/* where it is, or where it failed */

#ifdef __cplusplus
}
#endif

#endif /* __BLK_VM_H__ */
