/**
 * Receiver (rc_rx build), the radio half of the future flight controller:
 * follows the hopping of the bound remote, acknowledges every control packet
 * with telemetry, arms and fails safe (rc_rx_t in rc_link.c). P1 drives no
 * motors: the display and the shell show what the motors would get.
 *
 *   B3 held 1 s   bind mode (also: no remote bound yet, or "link bind")
 *
 * The radio FIFO is polled from task_poll_rc, every pass of the kernel loop
 * (at least once per ms): 3 packets of FIFO cover a handler of ~12 ms.
 */
#if defined(APP_RC_RX)

#include <string.h>

#include "ak.h"
#include "task.h"
#include "timer.h"

#include "hal.h"
#include "xprintf.h"
#include "gfx.h"
#include "kit.h"
#include "rc_app.h"
#include "rc_radio.h"
#include "task_list.h"

static rc_status_t st;
static rc_rx_t rx;
static rc_hop_rx_t hop;
static uint8_t chans[RC_HOPS];
static uint8_t bind_mode;
static uint16_t win_good;
static uint32_t win_ms;
static uint8_t last_seq;
static uint8_t was_armed, was_failsafe;
static uint8_t hop_pending;		/* channel change waiting for the ACK to be out */
static uint32_t hop_at_ms;

const rc_status_t* rc_status(void) {
	return &st;
}

static void radio_normal(void) {
	uint8_t a[5];

	bind_mode = 0;
	st.binding = 0;
	rc_addr(st.id, a);
	rc_hop_table(st.id, chans);
	rc_hop_rx_init(&hop, hal_millis());
	if (st.radio_ok) {
		rc_radio_config(a, chans[hop.hop], 1);
	}
}

static void radio_bind(void) {
	uint8_t a[5];

	bind_mode = 1;
	st.binding = 1;
	rc_addr(0, a);
	if (st.radio_ok) {
		rc_radio_config(a, RC_BIND_CH, 1);
	}
}

static void tele_preload(void) {
	rc_tele_t t;
	uint8_t pkt[RC_RADIO_MAX];

	t.seq = last_seq;
	t.vbat_mv = 0;			/* P1: no battery divider wired yet */
	t.state = (uint8_t)((rx.armed ? RC_S_ARMED : 0) | (rx.failsafe_seen ? RC_S_FAILSAFE : 0));
	t.rate_half = (uint8_t)(st.rate / 2U > 255U ? 255U : st.rate / 2U);
	rc_radio_ack(pkt, rc_tele_pack(pkt, &t));
}

static void got_bind(const uint8_t* p, uint8_t n) {
	uint32_t id;
	uint8_t rec[RC_NVM_SIZE];

	if (!rc_bind_unpack(p, n, &id)) {
		return;
	}
	st.id = id;
	/* the chip sends the ACK ~0.3 ms after the packet: changing the address
	 * before that would cut it, and the remote would never know */
	hal_delay_ms(2);
	if (!rc_nvm_load(rec)) {
		memset(rec, 0, sizeof(rec));
	}
	rc_nvm_put_id(rec, id);
	rc_nvm_store(rec);
	/* the remote saw the ACK of this packet and goes to its own channels */
	radio_normal();
	task_post_pure_msg_try(TASK_RC_ID, RC_SIG_BOUND);	/* beep + log in the task */
}

void task_poll_rc(void) {
	uint8_t p[RC_RADIO_MAX];
	uint8_t n;
	uint32_t now = hal_millis();

	rc_buttons_poll();
	if (!st.radio_ok) {
		return;
	}
	while ((n = rc_radio_recv(p)) != 0) {
		rc_cmd_t c;
		uint8_t seq, h;

		if (bind_mode) {
			got_bind(p, n);
			return;
		}
		if (!rc_ctrl_unpack(p, n, &seq, &h, &c)) {
			continue;
		}
		last_seq = seq;
		rc_rx_packet(&rx, &c, now);
		rc_hop_rx_got(&hop, h, now);
		tele_preload();
		win_good++;
		/* the ACK of this packet is still on the air for ~0.3 ms: change
		 * channel on a later pass, at least 1 ms from now (the next packet
		 * comes 4 ms after this one) */
		hop_pending = 1;
		hop_at_ms = now + 2;
	}
	if (!bind_mode) {
		if (hop_pending && (int32_t)(now - hop_at_ms) >= 0) {
			hop_pending = 0;
			rc_radio_channel(chans[hop.hop]);
		}
		if (rc_hop_rx_tick(&hop, now)) {
			hop_pending = 0;
			rc_radio_channel(chans[hop.hop]);
		}
		rc_rx_tick(&rx, now);
		rc_rx_output(&rx, &st.cmd);
		st.armed = rx.armed;
		st.failsafe = rx.failsafe;
		st.link_ok = (uint8_t)!rx.failsafe;
		/* edges for the task: beeps and a log line */
		if (rx.armed != was_armed || rx.failsafe != was_failsafe) {
			was_armed = rx.armed;
			was_failsafe = rx.failsafe;
			task_post_pure_msg_try(TASK_RC_ID, RC_SIG_STATE);
		}
	}
}

void task_rc(ak_msg_t* msg) {
	uint8_t rec[RC_NVM_SIZE];

	switch (msg->sig) {
	case RC_SIG_INIT:
		st.lcd_ok = kit_init();
		st.radio_ok = rc_radio_init();
		rc_rx_init(&rx);
		was_failsafe = 1;
		if (rc_nvm_load(rec) && rc_nvm_get_id(rec, &st.id)) {
			radio_normal();
			xprintf("rc: receiver, bound to %08X, radio %s\n", (unsigned)st.id,
					st.radio_ok ? "ok" : "NOT ANSWERING (nRF24 in J6?)");
		}
		else {
			radio_bind();
			xprintf("rc: receiver, no remote bound: bind mode (radio %s)\n", st.radio_ok ? "ok" : "NOT ANSWERING");
		}
		if (st.radio_ok) {
			tele_preload();
		}
		win_ms = hal_millis();
		rc_ui_kick();
		timer_set(TASK_RC_ID, RC_SIG_TICK, 1000, TIMER_ONE_SHOT);
		break;

	case RC_SIG_TICK:
		timer_set(TASK_RC_ID, RC_SIG_TICK, 1000, TIMER_ONE_SHOT);
		st.rate = win_good;
		win_good = 0;
		hal_led_set(rx.armed);
		break;

	case RC_SIG_BOUND:
		if (!bind_mode) {
			kit_buzzer(2400);
			timer_set(TASK_RC_ID, RC_SIG_BEEP_OFF, 150, TIMER_ONE_SHOT);
			xprintf("rc: bound to %08X\n", (unsigned)st.id);
		}
		break;

	case RC_SIG_STATE:
		xprintf("rc: %s%s\n", rx.armed ? "ARMED" : "disarmed", rx.failsafe ? ", FAILSAFE (no packets)" : "");
		kit_buzzer(rx.armed ? 2600 : rx.failsafe ? 600 : 1000);
		timer_set(TASK_RC_ID, RC_SIG_BEEP_OFF, rx.failsafe ? 500 : 200, TIMER_ONE_SHOT);
		break;

	case RC_SIG_HOLD_3:
		if (!rx.armed) {
			radio_bind();
			xprintf("rc: bind mode: start the bind on the remote (B1 held 2 s, or link bind)\n");
		}
		break;

	case RC_SIG_BEEP_OFF:
		kit_buzzer(0);
		break;

	default:
		break;
	}
}

/*----------------------------------------------------------------------------
 * display
 *--------------------------------------------------------------------------*/
static void bar(int y, const char* name, int v, int lo, int hi) {
	int w = GFX_W - 18;
	int x = 16 + (v - lo) * (w - 1) / (hi - lo);

	gfx_text(0, y, name, 1);
	gfx_rect(16, y, w, 7, 1);
	gfx_fill(x - 1, y + 1, 3, 5, 1);
}

void rc_ui_draw(const rc_status_t* s) {
	char buf[12];

	gfx_clear();
	if (!s->radio_ok) {
		gfx_text(0, 0, "NO RADIO (J6)", 1);
	}
	else if (s->binding) {
		gfx_text(0, 0, "BIND MODE", 1);
	}
	else if (s->failsafe) {
		gfx_text(0, 0, "FAILSAFE", 1);
	}
	else {
		gfx_text(0, 0, "RX ", 1);
		gfx_utoa(buf, s->rate, 1);
		gfx_text(18, 0, buf, 1);
		gfx_text(18 + gfx_text_width(buf, 1), 0, "/s", 1);
	}
	gfx_text(GFX_W - 6 * GFX_FONT_W, 0, s->armed ? " ARMED" : "  SAFE", 1);
	gfx_invert(0, 0, GFX_W, 8);
	gfx_text(0, 10, (s->cmd.flags & RC_F_ANGLE) ? "ANGLE" : "ACRO", 1);
	if (s->cmd.flags & RC_F_BEEP) {
		gfx_text(36, 10, "BEEP", 1);
	}
	bar(22, "R", s->cmd.roll, -RC_STICK_MAX, RC_STICK_MAX);
	bar(32, "P", s->cmd.pitch, -RC_STICK_MAX, RC_STICK_MAX);
	bar(42, "Y", s->cmd.yaw, -RC_STICK_MAX, RC_STICK_MAX);
	bar(52, "T", s->cmd.thr, 0, RC_THR_MAX);
}

/*----------------------------------------------------------------------------
 * shell: link | link bind
 *--------------------------------------------------------------------------*/
void cmd_link(const char* args) {
	while (*args == ' ') {
		args++;
	}
	if (strncmp(args, "bind", 4) == 0) {
		task_post_pure_msg(TASK_RC_ID, RC_SIG_HOLD_3);
		return;
	}
	xprintf("receiver, bound to %08X, radio %s%s\n", (unsigned)st.id, st.radio_ok ? "ok" : "MISSING",
			st.binding ? ", BIND MODE" : "");
	xprintf("%u good packets/s, %s%s, channel index %u\n", st.rate, st.armed ? "ARMED" : "disarmed",
			st.failsafe ? ", FAILSAFE" : "", hop.hop);
	xprintf("motors get: r %d p %d y %d t %u, %s%s\n", st.cmd.roll, st.cmd.pitch, st.cmd.yaw, st.cmd.thr,
			(st.cmd.flags & RC_F_ANGLE) ? "angle" : "acro", (st.cmd.flags & RC_F_BEEP) ? ", beeper" : "");
	xprintf("link bind\n");
}

#endif /* APP_RC_RX */
