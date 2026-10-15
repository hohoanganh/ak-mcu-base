/**
 ******************************************************************************
 * @brief:  Demo for the AK Base Kit: menu, digital clock, two games and a
 *          system monitor on the 128x64 display, three buttons, buzzer.
 *          Later additions: Dino runner, rotating 3D solids, a jukebox,
 *          a ray-cast maze, video from the SPI flash, a weather station,
 *          Tetris, Breakout, Invaders, screen savers, Pong over RS485.
 *
 *  How it sits on the kernel:
 *   - task_poll_buttons (polling) debounces the buttons and posts key signals;
 *   - task_ui owns the screen: a periodic timer posts UI_SIG_FRAME, the
 *     active screen updates and draws into the frame buffer, gfx_flush()
 *     sends the pages that changed;
 *   - a beep is "buzzer on" + a one-shot timer that posts UI_SIG_BEEP_OFF.
 *  Nothing waits: every handler returns within one frame.
 *
 *  Buttons (left to right under the display): B1, B2, B3.
 *   menu:    B1 down, B2 up, B3 open
 *   screens: B1 / B2 as written in the footer, B3 short = third action,
 *            B3 held = back to the menu
 ******************************************************************************
**/

#ifndef __UI_H__
#define __UI_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#include "ak.h"
#include "message.h"
#include "kit.h"
#include "gfx.h"

#define UI_FRAME_MS			(50)		/* 20 frames per second, fewer if a frame takes longer */
#define UI_HOLD_MS			(700)		/* B3 held this long = back to the menu */

/* TASK_UI_ID */
enum {
	UI_SIG_INIT = AK_USER_DEFINE_SIG,
	UI_SIG_FRAME,
	UI_SIG_KEY_1,			/* B1 pressed */
	UI_SIG_KEY_2,			/* B2 pressed */
	UI_SIG_KEY_3,			/* B3 released before UI_HOLD_MS */
	UI_SIG_BACK,			/* B3 held */
	UI_SIG_BEEP_OFF,
	UI_SIG_NOTE,			/* music: the current piece of the song is over */
};

typedef struct {
	const char* name;						/* menu entry */
	void (*enter)(void);					/* screen opened */
	void (*key)(uint8_t btn);				/* KIT_BTN_1 / _2 / _3, short press */
	void (*frame)(uint32_t now_ms);			/* every UI_FRAME_MS: update + draw */
	void (*leave)(void);					/* screen closed (may be 0) */
} ui_screen_t;

extern const ui_screen_t scr_clock;
extern const ui_screen_t scr_snake;
extern const ui_screen_t scr_flappy;
extern const ui_screen_t scr_dino;
extern const ui_screen_t scr_cube;
extern const ui_screen_t scr_music;
extern const ui_screen_t scr_maze;
extern const ui_screen_t scr_video;
extern const ui_screen_t scr_weather;
extern const ui_screen_t scr_tetris;
extern const ui_screen_t scr_breakout;
extern const ui_screen_t scr_invaders;
extern const ui_screen_t scr_saver;
extern const ui_screen_t scr_pong;
extern const ui_screen_t scr_plot;
extern const ui_screen_t scr_blocks;
extern const ui_screen_t scr_system;

/* menu (scr_menu.c): returns the chosen screen on B3, else 0 */
extern void menu_enter(void);
extern const ui_screen_t* menu_key(uint8_t btn);
extern void menu_frame(uint32_t now_ms);
/* Screen whose menu name contains the text (any case), "3d" -> "3D solids";
 * 0 if none does. Also moves the menu selection there. */
extern const ui_screen_t* menu_find(const char* text);
/* Opens that screen directly, "menu" goes back to the menu. Returns 0 if
 * no screen matches. Shell: "ui open <name>". */
extern uint8_t ui_open(const char* text);

/* helpers for the screens */
extern void ui_beep(uint16_t freq_hz, uint16_t ms);		/* silent while a song plays */
/* The status LED for a screen: from the first call the heartbeat leaves the LED
 * alone, until ui_led_release() gives it back. */
extern void ui_led(uint8_t on);
extern void ui_led_release(void);
/* play an RTTTL song on the buzzer without blocking (music.h) / stop it */
extern void ui_music_play(const char* rtttl);
extern void ui_music_stop(void);
extern uint32_t ui_rand(void);

/* RAM shared by the screens for their big buffers (snake body, saver
 * fields): only one screen is open at a time. Valid from the enter() of the
 * open screen to its leave(); after another screen ran it holds that screen's
 * data, so enter() sets it up again. Not for anything that must outlive the
 * screen (high scores, background jobs). 8-byte aligned. */
#define UI_SCRATCH_SIZE		(320)
extern void* ui_scratch(void);
/* 127 * sin / cos of an angle in 1/256 of a turn (table, no floating point) */
extern int ui_sin(uint8_t a);
extern int ui_cos(uint8_t a);
extern void ui_footer(const char* b1, const char* b2, const char* b3);
extern void ui_title(const char* left, const char* right);
extern void ui_game_over(int y, uint32_t best);

/* RS485 for a screen that talks to another kit (Pong). Open takes the port
 * away from Modbus until close. getc: one received byte or -1; write blocks
 * for the time the bytes need on the wire. */
extern void ui_link_open(void);
extern void ui_link_close(void);
extern int ui_link_getc(void);
extern void ui_link_write(const uint8_t* data, uint8_t len);
/* Pong reads the link between frames too, to answer the other kit at once. */
extern void pong_poll(void);

/* One display page as a line of text, for the screen mirror on the PC
 * (tools/ak_screen.py): "@P<page> <hex> <crc16>\n". The hex is the 128 bytes
 * of the page, PackBits packed as in video.h; the CRC is over the 128 bytes.
 * Returns the number of characters written. */
extern uint16_t ui_dump_page(uint8_t page, void (*out)(uint8_t c));

/* Which pages of the screen still have to go to the PC. */
#define UI_STREAM_OFF		(0)
#define UI_STREAM_ON		(1)		/* every page that changes, until switched off */
#define UI_STREAM_ONCE		(2)		/* the screen as it is, once ("ui dump"), then off */

typedef struct {
	uint8_t mode;		/* UI_STREAM_* */
	uint8_t dirty;		/* ON: pages the PC has not seen in their present state */
	uint8_t todo;		/* ONCE: pages of the dump still to send */
	uint8_t next;		/* page the next batch starts with */
} ui_stream_t;

/* Sends pages until max_chars are out (at least one page if any is waiting)
 * and returns the number of characters written, 0 if there was nothing to
 * send.
 *
 * The pages take turns: a batch starts where the last one stopped. Starting
 * at page 0 every time never reaches the last pages on a screen whose first
 * pages change in every frame (seen on the kit: "ui dump" of the System
 * monitor never delivered page 7, and never ended).
 * A dump sends each page exactly once and ends, whatever changes meanwhile. */
extern uint16_t ui_stream_batch(ui_stream_t* st, uint16_t max_chars, void (*out)(uint8_t c));

/* 1: the games play themselves (shell "ui auto", also used to record the
 * pictures in the documentation). Any button gives control back. */
extern uint8_t ui_autoplay;

/* frame statistics of the last flush, shown by the system screen */
extern uint8_t ui_last_pages;
extern uint16_t ui_last_frame_ms;

/* shell: "ui" = status, "ui 1|2|3" = press a button, "ui back" = hold B3,
 * "ui auto" = games play themselves, "ui open <name>" = go to a screen,
 * "ui dump" = the screen as text once,
 * "ui stream" = the screen as text whenever it changes (on / off) */
extern void cmd_ui(const char* args);
/* shell: "blk" = state of the block program, "blk run" / "blk stop" */
extern void cmd_blk(const char* args);
/* shell: "plot <number>" = one more point on the scope screen */
extern void cmd_plot(const char* args);
/* shell: "th" = temperature, humidity and the graph of the weather screen as CSV */
extern void cmd_th(const char* args);
/* shell: "i2c" = scan the I2C1 header, "i2c <addr> <reg> [n]" = read registers */
extern void cmd_i2c(const char* args);
/* shell: "spi start [mode]" / "spi" / "spi dump [from]" / "spi stop" = SPI sniffer on J6 */
extern void cmd_spi(const char* args);
/* fw_proto extension (console): commands 0x40.. load files into the media
 * store, see tools/ak_video.py. Returns the status byte, fills the response. */
extern uint8_t ui_proto_ext(uint8_t cmd, const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t* resp_len);

extern void task_ui(ak_msg_t* msg);
extern void task_poll_buttons(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_H__ */
