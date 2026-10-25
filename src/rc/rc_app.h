/**
 ******************************************************************************
 * @brief:  Tasks of the rc_tx (remote) and rc_rx (receiver) builds, step P1
 *          of docs/drone-ak-mcu.md. One of APP_RC_TX / APP_RC_RX.
 *
 *  task_rc     (priority 5) the radio: a packet every RC_PERIOD_MS, sticks,
 *              arming, bind, calibration. Its handlers stay short (< 1 ms)
 *              so that packets leave on time: the receiver tolerates a few ms.
 *  task_rc_ui  (priority 2) the OLED, redrawn every 100 ms and pushed out in
 *              pieces of RC_UI_CHUNK bytes (gfx_flush_step), ~1.3 ms each.
 *  task_poll_rc   buttons; on the receiver also the radio FIFO.
 ******************************************************************************
**/

#ifndef __RC_APP_H__
#define __RC_APP_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#include "ak.h"
#include "rc_link.h"

enum {
	RC_SIG_INIT = AK_USER_DEFINE_SIG,
	RC_SIG_TICK,			/* remote: next packet; receiver: once a second */
	RC_SIG_KEY_1,
	RC_SIG_KEY_2,
	RC_SIG_KEY_3,
	RC_SIG_HOLD_1,			/* held 2 s */
	RC_SIG_HOLD_3,			/* held 1 s */
	RC_SIG_BEEP_OFF,
	RC_SIG_CAL_START,		/* from the shell: sticks centred now, then 10 s of ends */
	RC_SIG_CAL_END,
	RC_SIG_MODE,			/* from the shell, common message: 1 byte, RC_F_ANGLE or 0 */
	RC_SIG_PC_STICK,		/* from the shell, common message: 4 x int16 (r, p, y, t); empty = off */
	RC_SIG_BOUND,			/* receiver, from the poll: a remote was bound */
	RC_SIG_STATE,			/* receiver, from the poll: armed or failsafe changed */
};

enum {
	RC_UI_SIG_INIT = AK_USER_DEFINE_SIG,
	RC_UI_SIG_DRAW,
	RC_UI_SIG_FLUSH,
};

#define RC_UI_DRAW_MS		(100)
#define RC_UI_FLUSH_MS		(5)
#define RC_UI_CHUNK			(24)		/* bytes per gfx_flush_step: ~1.3 ms on the bit-banged I2C */

/* what task_rc shows to the display task (written by task_rc only) */
typedef struct {
	uint8_t radio_ok;
	uint8_t lcd_ok;
	uint8_t binding;
	uint8_t calibrating;
	uint8_t adc_ok;			/* remote: ADS1115 answers */
	uint8_t pc_sticks;		/* remote: sticks come from the shell */
	uint8_t link_ok;		/* remote: an ACK within RC_LOST_MS; receiver: not in failsafe */
	uint8_t quality;		/* remote: acknowledged packets, % of the last second */
	uint16_t rate;			/* packets per second: remote sent, receiver good */
	uint32_t id;
	rc_cmd_t cmd;			/* remote: what it sends; receiver: what the motors get */
	rc_tele_t tele;			/* remote: last telemetry */
	uint8_t armed;			/* receiver: armed now; remote: as the receiver reports */
	uint8_t failsafe;		/* receiver: in failsafe now */
} rc_status_t;

extern const rc_status_t* rc_status(void);

extern void task_rc(ak_msg_t* msg);
extern void task_rc_ui(ak_msg_t* msg);
extern void task_poll_rc(void);
extern void cmd_link(const char* args);

/* shared by both roles (rc_common.c) */
extern uint8_t rc_nvm_load(uint8_t rec[RC_NVM_SIZE]);	/* 1 if read */
extern void rc_nvm_store(const uint8_t rec[RC_NVM_SIZE]);
/* debounced buttons -> RC_SIG_KEY_n on release, RC_SIG_HOLD_1 / _3 once held */
extern void rc_buttons_poll(void);
extern void rc_beep(uint16_t freq_hz, uint16_t ms);
extern void rc_ui_kick(void);			/* start the display task */

#ifdef __cplusplus
}
#endif

#endif /* __RC_APP_H__ */
