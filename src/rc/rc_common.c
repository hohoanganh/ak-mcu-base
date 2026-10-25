/**
 * Parts of the rc_tx / rc_rx builds both roles use: the EEPROM record,
 * buttons, buzzer, and the display task (each role draws its own screen in
 * rc_ui_draw()).
 */
#if defined(APP_RC_TX) || defined(APP_RC_RX)

#include "ak.h"
#include "task.h"
#include "timer.h"

#include "hal.h"
#include "gfx.h"
#include "kit.h"
#include "rc_app.h"
#include "task_list.h"

#define BTN_DEBOUNCE_MS		(15)
#define HOLD_1_MS			(2000)
#define HOLD_3_MS			(1000)

extern void rc_ui_draw(const rc_status_t* s);	/* rc_tx.c / rc_rx.c */

uint8_t rc_nvm_load(uint8_t rec[RC_NVM_SIZE]) {
	return hal_nvm_read(RC_NVM_OFFSET, rec, RC_NVM_SIZE) == 0;
}

void rc_nvm_store(const uint8_t rec[RC_NVM_SIZE]) {
	(void)hal_nvm_write(RC_NVM_OFFSET, rec, RC_NVM_SIZE);
}

void rc_beep(uint16_t freq_hz, uint16_t ms) {
	kit_buzzer(freq_hz);
	timer_set(TASK_RC_ID, RC_SIG_BEEP_OFF, ms, TIMER_ONE_SHOT);
}

void rc_buttons_poll(void) {
	static uint32_t last_ms, down_ms[3];
	static uint8_t stable, held_sent;
	uint32_t now = hal_millis();
	uint8_t sample, changed;
	static const uint8_t bit[3] = { KIT_BTN_1, KIT_BTN_2, KIT_BTN_3 };

	if (now - last_ms < BTN_DEBOUNCE_MS) {
		return;
	}
	last_ms = now;
	sample = kit_buttons();
	changed = (uint8_t)(sample ^ stable);
	stable = sample;
	for (int i = 0; i < 3; i++) {
		if (changed & bit[i]) {
			if (sample & bit[i]) {
				down_ms[i] = now;
			}
			else {
				if (!(held_sent & bit[i])) {
					task_post_pure_msg_try(TASK_RC_ID, (uint8_t)(RC_SIG_KEY_1 + i));
				}
				held_sent &= (uint8_t)~bit[i];
			}
		}
	}
	if ((stable & KIT_BTN_1) && !(held_sent & KIT_BTN_1) && now - down_ms[0] >= HOLD_1_MS) {
		held_sent |= KIT_BTN_1;
		task_post_pure_msg_try(TASK_RC_ID, RC_SIG_HOLD_1);
	}
	if ((stable & KIT_BTN_3) && !(held_sent & KIT_BTN_3) && now - down_ms[2] >= HOLD_3_MS) {
		held_sent |= KIT_BTN_3;
		task_post_pure_msg_try(TASK_RC_ID, RC_SIG_HOLD_3);
	}
}

/*----------------------------------------------------------------------------
 * display task: redraw into the frame buffer every RC_UI_DRAW_MS, push it out
 * in small pieces so that task_rc is never held up for long
 *--------------------------------------------------------------------------*/
static uint8_t lcd_ok;

void rc_ui_kick(void) {
	task_post_pure_msg(TASK_RC_UI_ID, RC_UI_SIG_INIT);
}

void task_rc_ui(ak_msg_t* msg) {
	switch (msg->sig) {
	case RC_UI_SIG_INIT:
		lcd_ok = rc_status()->lcd_ok;		/* task_rc ran kit_init() before the buttons were polled */
		gfx_clear();
		if (lcd_ok) {
			gfx_flush(1);
		}
		timer_set(TASK_RC_UI_ID, RC_UI_SIG_DRAW, RC_UI_DRAW_MS, TIMER_ONE_SHOT);
		break;

	case RC_UI_SIG_DRAW:
		if (lcd_ok) {
			rc_ui_draw(rc_status());
			task_post_pure_msg(TASK_RC_UI_ID, RC_UI_SIG_FLUSH);
		}
		timer_set(TASK_RC_UI_ID, RC_UI_SIG_DRAW, RC_UI_DRAW_MS, TIMER_ONE_SHOT);
		break;

	case RC_UI_SIG_FLUSH:
		if (gfx_flush_step(RC_UI_CHUNK)) {
			timer_set(TASK_RC_UI_ID, RC_UI_SIG_FLUSH, RC_UI_FLUSH_MS, TIMER_ONE_SHOT);
		}
		break;

	default:
		break;
	}
}

#endif /* APP_RC_TX || APP_RC_RX */
