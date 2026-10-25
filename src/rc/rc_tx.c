/**
 * Remote (rc_tx build): sticks from an ADS1115 on I2C1 (J7) or from the
 * shell, one control packet every RC_PERIOD_MS over the nRF24, telemetry
 * from the ACKs, arming, bind, calibration. See rc_app.h and
 * docs/drone-ak-mcu.md (P1).
 *
 *   B1 short  angle <-> acro          B1 held 2 s  bind (3 s of bind packets)
 *   B2 short  beeper of the receiver  B3 short     disarm
 *   B3 held 1 s  arm (only with the throttle at the bottom)
 *
 * ADS1115 (address 0x48): AIN0 roll, AIN1 pitch, AIN2 yaw, AIN3 throttle,
 * single shot at 860 SPS, +-4.096 V. One channel per packet: the four sticks
 * are fresh every 16 ms.
 */
#if defined(APP_RC_TX)

#include <string.h>

#include "ak.h"
#include "task.h"
#include "timer.h"

#include "hal.h"
#include "xprintf.h"
#include "crc.h"
#include "gfx.h"
#include "kit.h"
#include "rc_app.h"
#include "rc_radio.h"
#include "message.h"
#include "task_list.h"

#define ADS_ADDR			(0x48)
#define ADS_FULL			(26400)		/* 3.3 V on the +-4.096 V range */
#define BIND_TICKS			(3000 / RC_PERIOD_MS)
#define CAL_MS				(10000)
#define PC_STICK_MS			(500)		/* shell sticks older than this: throttle 0, disarm */
#define LOST_BEEP_TICKS		(1000 / RC_PERIOD_MS)

static rc_status_t st;
static rc_cal_t cal;
static uint8_t chans[RC_HOPS];
static uint8_t addr[5];
static rc_hop_tx_t hop;
static uint8_t seq;
static uint8_t in_flight;
static uint8_t first_packet;
static uint16_t bind_left;
static uint32_t last_ack_ms;
static uint16_t win_sent, win_acked;
static uint32_t win_ms;
static uint16_t lost_beep;

/* sticks */
static uint16_t raw[4];
static uint8_t ads_ch;
static int16_t pc_val[4];
static uint32_t pc_ms;
static uint8_t arm_req;
static uint8_t flags_mode = RC_F_ANGLE;
static uint8_t beep_req;

const rc_status_t* rc_status(void) {
	return &st;
}

/*----------------------------------------------------------------------------
 * identity and configuration
 *--------------------------------------------------------------------------*/
/* 96-bit unique ID of the STM32L1 (cat. 1/2 devices: 0x1FF80050) folded into
 * 32 bits; stored in the EEPROM the first time, so it never changes */
static uint32_t make_id(void) {
	const uint32_t* uid = (const uint32_t*)0x1FF80050UL;
	uint32_t w[3] = { uid[0], uid[1], uid[2] };
	uint32_t id = crc32_update(CRC32_INIT, w, sizeof(w)) ^ hal_millis();

	return id ? id : 0x5A17C0DEUL;
}

static void load_config(void) {
	uint8_t rec[RC_NVM_SIZE];
	uint8_t dirty = 0;

	if (!rc_nvm_load(rec)) {
		memset(rec, 0, sizeof(rec));
	}
	if (!rc_nvm_get_id(rec, &st.id)) {
		st.id = make_id();
		rc_nvm_put_id(rec, st.id);
		dirty = 1;
	}
	if (!rc_nvm_get_cal(rec, &cal)) {
		rc_cal_default(&cal, ADS_FULL);
	}
	if (dirty) {
		rc_nvm_store(rec);
	}
}

static void save_cal(void) {
	uint8_t rec[RC_NVM_SIZE];

	if (!rc_nvm_load(rec)) {
		memset(rec, 0, sizeof(rec));
	}
	rc_nvm_put_id(rec, st.id);
	rc_nvm_put_cal(rec, &cal);
	rc_nvm_store(rec);
}

static void radio_normal(void) {
	rc_addr(st.id, addr);
	rc_hop_table(st.id, chans);
	rc_hop_tx_init(&hop);
	first_packet = 1;
	if (st.radio_ok) {
		rc_radio_config(addr, chans[0], 0);
	}
}

static void radio_bind(void) {
	uint8_t a[5];

	rc_addr(0, a);
	if (st.radio_ok) {
		rc_radio_config(a, RC_BIND_CH, 0);
	}
	bind_left = BIND_TICKS;
	st.binding = 1;
}

/*----------------------------------------------------------------------------
 * sticks
 *--------------------------------------------------------------------------*/
static uint8_t ads_start(uint8_t ch) {
	/* OS=1, MUX=1xx (AINch vs GND), PGA=001 (+-4.096 V), MODE=1 (single);
	 * DR=111 (860 SPS), comparator off */
	uint8_t cfg[2] = { (uint8_t)(0x80 | ((4 + ch) << 4) | (1 << 1) | 1), 0xE3 };

	return kit_i2c_write_n(ADS_ADDR, 0x01, cfg, 2);
}

static void ads_step(void) {
	uint8_t b[2];

	if (!st.adc_ok) {
		return;
	}
	if (kit_i2c_read(ADS_ADDR, 0x00, b, 2)) {
		int16_t v = (int16_t)((b[0] << 8) | b[1]);

		raw[ads_ch] = (uint16_t)(v < 0 ? 0 : v);
	}
	ads_ch = (uint8_t)((ads_ch + 1) & 3);
	(void)ads_start(ads_ch);
}

static void sticks_update(uint32_t now) {
	rc_cmd_t* c = &st.cmd;

	st.pc_sticks = pc_ms != 0;
	if (st.pc_sticks) {
		if (now - pc_ms > PC_STICK_MS) {
			/* the PC stopped talking: nobody holds the sticks */
			c->roll = c->pitch = c->yaw = 0;
			c->thr = 0;
			arm_req = 0;
		}
		else {
			c->roll = pc_val[0];
			c->pitch = pc_val[1];
			c->yaw = pc_val[2];
			c->thr = (uint16_t)pc_val[3];
		}
	}
	else if (st.adc_ok) {
		rc_cal_apply(&cal, raw, c);
	}
	else {
		c->roll = c->pitch = c->yaw = 0;
		c->thr = 0;
	}
	if (st.calibrating) {
		for (int a = 0; a < 4; a++) {
			cal.axis[a].min = raw[a] < cal.axis[a].min ? raw[a] : cal.axis[a].min;
			cal.axis[a].max = raw[a] > cal.axis[a].max ? raw[a] : cal.axis[a].max;
		}
		c->thr = 0;
		arm_req = 0;
	}
	c->flags = (uint8_t)((arm_req ? RC_F_ARM : 0) | flags_mode | (beep_req ? RC_F_BEEP : 0));
}

/*----------------------------------------------------------------------------
 * one period
 *--------------------------------------------------------------------------*/
static void tick(void) {
	uint32_t now = hal_millis();
	uint8_t acked = 0;
	uint8_t pkt[RC_RADIO_MAX];

	/* the packet sent last period: decided long ago (within ~1.8 ms) */
	if (in_flight) {
		uint8_t ack[RC_RADIO_MAX], n = 0;
		int8_t r = rc_radio_tx_done(ack, &n);
		rc_tele_t t;

		in_flight = 0;
		acked = r == RC_TX_ACKED;
		if (acked) {
			win_acked++;
			last_ack_ms = now;
			if (st.binding) {
				st.binding = 0;						/* the receiver took the id */
				bind_left = 0;
				radio_normal();
				rc_beep(2400, 150);
				xprintf("rc: bound, id %08X\n", (unsigned)st.id);
			}
			else if (n && rc_tele_unpack(ack, n, &t)) {
				st.tele = t;
				st.armed = (uint8_t)(t.state & RC_S_ARMED);
			}
		}
	}

	ads_step();
	sticks_update(now);

	if (st.binding) {
		if (bind_left == 0) {
			st.binding = 0;
			radio_normal();
			xprintf("rc: bind timed out (no receiver in bind mode)\n");
		}
		else {
			bind_left--;
			if (st.radio_ok) {
				rc_radio_send(pkt, rc_bind_pack(pkt, st.id));
				in_flight = 1;
			}
		}
	}
	else if (st.radio_ok) {
		uint8_t h = first_packet ? 0 : rc_hop_tx_next(&hop, acked);

		first_packet = 0;
		rc_radio_channel(chans[h]);
		rc_radio_send(pkt, rc_ctrl_pack(pkt, seq++, h, &st.cmd));
		in_flight = 1;
		win_sent++;
	}

	/* link state, once a second the quality */
	st.link_ok = (uint8_t)(last_ack_ms && now - last_ack_ms < RC_LOST_MS);
	if (!st.link_ok) {
		st.armed = 0;
		if (++lost_beep >= LOST_BEEP_TICKS && last_ack_ms) {
			lost_beep = 0;
			rc_beep(3000, 60);						/* the link was up and is gone */
		}
	}
	if (now - win_ms >= 1000) {
		st.quality = (uint8_t)(win_sent ? (uint32_t)win_acked * 100U / win_sent : 0);
		st.rate = win_sent;
		win_sent = win_acked = 0;
		win_ms = now;
	}
}

void task_rc(ak_msg_t* msg) {
	switch (msg->sig) {
	case RC_SIG_INIT:
		st.lcd_ok = kit_init();
		load_config();
		st.radio_ok = rc_radio_init();
		st.adc_ok = ads_start(0);
		radio_normal();
		xprintf("rc: remote id %08X, radio %s, sticks %s\n", (unsigned)st.id,
				st.radio_ok ? "ok" : "NOT ANSWERING (nRF24 in J6?)",
				st.adc_ok ? "ADS1115" : "none (no ADS1115 at 0x48: use 'link stick')");
		win_ms = hal_millis();
		rc_ui_kick();
		timer_set(TASK_RC_ID, RC_SIG_TICK, RC_PERIOD_MS, TIMER_ONE_SHOT);
		break;

	case RC_SIG_TICK:
		/* re-armed first: the period does not stretch by this handler */
		timer_set(TASK_RC_ID, RC_SIG_TICK, RC_PERIOD_MS, TIMER_ONE_SHOT);
		tick();
		break;

	case RC_SIG_KEY_1:
		flags_mode = flags_mode ? 0 : RC_F_ANGLE;
		rc_beep(flags_mode ? 2000 : 1500, 40);
		break;

	case RC_SIG_KEY_2:
		beep_req = !beep_req;
		rc_beep(1800, 40);
		break;

	case RC_SIG_KEY_3:
		if (arm_req) {
			arm_req = 0;
			rc_beep(1000, 300);
		}
		break;

	case RC_SIG_HOLD_3:
		if (!arm_req && st.cmd.thr <= RC_THR_ARM_MAX && !st.calibrating) {
			arm_req = 1;
			rc_beep(2600, 300);
		}
		else if (!arm_req) {
			rc_beep(400, 500);						/* throttle not at the bottom */
		}
		break;

	case RC_SIG_HOLD_1:
		if (!arm_req) {
			radio_bind();
			rc_beep(2200, 100);
		}
		break;

	case RC_SIG_BEEP_OFF:
		kit_buzzer(0);
		break;

	case RC_SIG_CAL_START:
		if (!st.adc_ok) {
			xprintf("rc: no ADS1115, nothing to calibrate\n");
			break;
		}
		/* sticks centred and throttle down now: these are the mids */
		for (int a = 0; a < 4; a++) {
			cal.axis[a].mid = raw[a];
			cal.axis[a].min = raw[a];
			cal.axis[a].max = raw[a];
		}
		st.calibrating = 1;
		timer_set(TASK_RC_ID, RC_SIG_CAL_END, CAL_MS, TIMER_ONE_SHOT);
		xprintf("rc: calibrating for %u s: move every stick to all its ends\n", CAL_MS / 1000U);
		break;

	case RC_SIG_MODE:
		flags_mode = get_data_common_msg(msg)[0] ? RC_F_ANGLE : 0;
		break;

	case RC_SIG_PC_STICK:
		if (get_data_len_common_msg(msg) != sizeof(pc_val)) {
			pc_ms = 0;
			xprintf("rc: sticks from the ADS1115 again\n");
		}
		else {
			memcpy(pc_val, get_data_common_msg(msg), sizeof(pc_val));
			pc_ms = hal_millis() | 1U;				/* 0 means "not from the PC" */
		}
		break;

	case RC_SIG_CAL_END:
		st.calibrating = 0;
		save_cal();
		xprintf("rc: calibration saved\n");
		for (int a = 0; a < 4; a++) {
			xprintf("  axis %d: min %u mid %u max %u\n", a, cal.axis[a].min, cal.axis[a].mid, cal.axis[a].max);
		}
		rc_beep(2400, 150);
		break;

	default:
		break;
	}
}

void task_poll_rc(void) {
	rc_buttons_poll();
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
	char buf[24];

	gfx_clear();
	if (!s->radio_ok) {
		gfx_text(0, 0, "NO RADIO (J6)", 1);
	}
	else if (s->binding) {
		gfx_text(0, 0, "BINDING...", 1);
	}
	else if (s->link_ok) {
		gfx_text(0, 0, "LINK ", 1);
		gfx_utoa(buf, s->quality, 1);
		gfx_text(30, 0, buf, 1);
		gfx_text(30 + gfx_text_width(buf, 1), 0, "%", 1);
	}
	else {
		gfx_text(0, 0, "LINK LOST", 1);
	}
	gfx_text(GFX_W - 6 * GFX_FONT_W, 0, s->armed ? " ARMED" : (s->cmd.flags & RC_F_ARM) ? "ARM..." : "  SAFE", 1);
	gfx_invert(0, 0, GFX_W, 8);

	gfx_text(0, 10, (s->cmd.flags & RC_F_ANGLE) ? "ANGLE" : "ACRO", 1);
	if (s->cmd.flags & RC_F_BEEP) {
		gfx_text(36, 10, "BEEP", 1);
	}
	if (s->calibrating) {
		gfx_text(66, 10, "CAL", 1);
	}
	else if (s->pc_sticks) {
		gfx_text(66, 10, "PC", 1);
	}
	else if (!s->adc_ok) {
		gfx_text(66, 10, "NO ADC", 1);
	}
	if (s->link_ok && s->tele.vbat_mv) {
		gfx_utoa(buf, s->tele.vbat_mv, 1);
		gfx_text(GFX_W - gfx_text_width(buf, 1) - 12, 10, buf, 1);
		gfx_text(GFX_W - 12, 10, "mV", 1);
	}

	bar(22, "R", s->cmd.roll, -RC_STICK_MAX, RC_STICK_MAX);
	bar(32, "P", s->cmd.pitch, -RC_STICK_MAX, RC_STICK_MAX);
	bar(42, "Y", s->cmd.yaw, -RC_STICK_MAX, RC_STICK_MAX);
	bar(52, "T", s->cmd.thr, 0, RC_THR_MAX);
}

/*----------------------------------------------------------------------------
 * shell: link [bind | cal | arm | disarm | angle | acro | stick r p y t | stick off]
 *--------------------------------------------------------------------------*/
static int32_t next_int(const char** p, uint8_t* ok) {
	int32_t v = 0, sign = 1;
	const char* s = *p;

	while (*s == ' ') {
		s++;
	}
	if (*s == '-') {
		sign = -1;
		s++;
	}
	*ok = (uint8_t)(*s >= '0' && *s <= '9');
	while (*s >= '0' && *s <= '9') {
		v = v * 10 + (*s++ - '0');
	}
	*p = s;
	return v * sign;
}

void cmd_link(const char* args) {
	while (*args == ' ') {
		args++;
	}
	if (strncmp(args, "bind", 4) == 0) {
		task_post_pure_msg(TASK_RC_ID, RC_SIG_HOLD_1);
		xprintf("bind packets for 3 s: put the receiver in bind mode (link bind there)\n");
	}
	else if (strncmp(args, "cal", 3) == 0) {
		task_post_pure_msg(TASK_RC_ID, RC_SIG_CAL_START);
	}
	else if (strncmp(args, "arm", 3) == 0) {
		task_post_pure_msg(TASK_RC_ID, RC_SIG_HOLD_3);
	}
	else if (strncmp(args, "disarm", 6) == 0) {
		task_post_pure_msg(TASK_RC_ID, RC_SIG_KEY_3);
	}
	else if (strncmp(args, "angle", 5) == 0 || strncmp(args, "acro", 4) == 0) {
		uint8_t m = args[1] == 'n' ? 1 : 0;

		task_post_common_msg(TASK_RC_ID, RC_SIG_MODE, &m, 1);
	}
	else if (strncmp(args, "stick", 5) == 0) {
		const char* p = args + 5;
		uint8_t ok = 1;
		int32_t v[4];
		int16_t s16[4];

		while (*p == ' ') {
			p++;
		}
		if (strncmp(p, "off", 3) == 0) {
			task_post_common_msg(TASK_RC_ID, RC_SIG_PC_STICK, (uint8_t*)s16, 0);
			return;
		}
		for (int i = 0; i < 4 && ok; i++) {
			v[i] = next_int(&p, &ok);
		}
		if (!ok || v[3] < 0 || v[3] > RC_THR_MAX) {
			xprintf("usage: link stick <roll> <pitch> <yaw> <throttle>  (-500..500, 0..1000); send again within %u ms\n",
					PC_STICK_MS);
			return;
		}
		for (int i = 0; i < 3; i++) {
			s16[i] = (int16_t)(v[i] < -RC_STICK_MAX ? -RC_STICK_MAX : v[i] > RC_STICK_MAX ? RC_STICK_MAX : v[i]);
		}
		s16[3] = (int16_t)v[3];
		task_post_common_msg(TASK_RC_ID, RC_SIG_PC_STICK, (uint8_t*)s16, sizeof(s16));
	}
	else {
		xprintf("remote id %08X, radio %s, sticks %s%s\n", (unsigned)st.id, st.radio_ok ? "ok" : "MISSING",
				st.pc_sticks ? "from the shell" : st.adc_ok ? "ADS1115" : "none", st.calibrating ? " (calibrating)" : "");
		xprintf("link %s, %u%% acknowledged, %u packets/s; %s, %s\n", st.link_ok ? "up" : "DOWN", st.quality, st.rate,
				arm_req ? (st.armed ? "ARMED" : "arm asked, receiver not armed") : "safe",
				flags_mode ? "angle" : "acro");
		xprintf("sticks r %d p %d y %d t %u, raw %u %u %u %u\n", st.cmd.roll, st.cmd.pitch, st.cmd.yaw, st.cmd.thr,
				raw[0], raw[1], raw[2], raw[3]);
		if (st.link_ok) {
			xprintf("receiver: %u packets/s, battery %u mV, %s%s\n", st.tele.rate_half * 2U, st.tele.vbat_mv,
					(st.tele.state & RC_S_ARMED) ? "armed" : "disarmed",
					(st.tele.state & RC_S_FAILSAFE) ? ", had a failsafe" : "");
		}
		xprintf("link bind | cal | arm | disarm | angle | acro | stick <r> <p> <y> <t> | stick off\n");
	}
}

#endif /* APP_RC_TX */
