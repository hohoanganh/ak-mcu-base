/**
 * Radio link of the remote (src/rc/rc_link.c) on host: packets, addresses
 * and hop tables, the two ends hopping through simulated packet loss and
 * outages, arming and failsafe of the receiver, stick calibration, the
 * EEPROM record. The nRF24 driver and the tasks run on the kit only.
 */
#include <string.h>

#include "rc_link.h"
#include "tiny_test.h"

static uint32_t rng = 1;

static uint32_t rnd(void) {
	rng = rng * 1103515245UL + 12345UL;
	return rng >> 8;
}

/* 1 with probability pct / 1000 */
static int chance(uint32_t permille) {
	return (rnd() % 1000U) < permille;
}

static void test_ctrl_packet(void) {
	static const rc_cmd_t cases[] = {
		{ 0, 0, 0, 0, 0 },
		{ 500, -500, 500, 1000, RC_F_ARM | RC_F_ANGLE },
		{ -500, 500, -500, 1000, RC_F_BEEP },
		{ 123, -77, 1, 431, RC_F_ARM },
	};
	uint8_t buf[32], seq, hop;
	rc_cmd_t c;

	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		CHECK_EQ(rc_ctrl_pack(buf, (uint8_t)(i * 37), (uint8_t)(i % RC_HOPS), &cases[i]), RC_CTRL_LEN);
		CHECK(rc_ctrl_unpack(buf, RC_CTRL_LEN, &seq, &hop, &c));
		CHECK_EQ(seq, (uint8_t)(i * 37));
		CHECK_EQ(hop, i % RC_HOPS);
		CHECK_EQ(c.roll, cases[i].roll);
		CHECK_EQ(c.pitch, cases[i].pitch);
		CHECK_EQ(c.yaw, cases[i].yaw);
		CHECK_EQ(c.thr, cases[i].thr);
		CHECK_EQ(c.flags, cases[i].flags);
	}
	/* out of range in: clamped, not wrapped */
	{
		rc_cmd_t big = { 900, -900, 600, 1500, 0 };

		rc_ctrl_pack(buf, 1, 0, &big);
		CHECK(rc_ctrl_unpack(buf, RC_CTRL_LEN, &seq, &hop, &c));
		CHECK_EQ(c.roll, 500);
		CHECK_EQ(c.pitch, -500);
		CHECK_EQ(c.thr, 1000);
	}
	/* every single bit flip is caught, and a wrong length or hop index */
	rc_ctrl_pack(buf, 9, 3, &cases[3]);
	for (int b = 0; b < RC_CTRL_LEN * 8; b++) {
		buf[b / 8] ^= (uint8_t)(1 << (b % 8));
		CHECK(!rc_ctrl_unpack(buf, RC_CTRL_LEN, &seq, &hop, &c));
		buf[b / 8] ^= (uint8_t)(1 << (b % 8));
	}
	CHECK(!rc_ctrl_unpack(buf, RC_CTRL_LEN - 1, &seq, &hop, &c));
	rc_ctrl_pack(buf, 9, RC_HOPS, &cases[0]);
	CHECK(!rc_ctrl_unpack(buf, RC_CTRL_LEN, &seq, &hop, &c));
}

static void test_tele_and_bind(void) {
	rc_tele_t t = { 200, 3712, RC_S_ARMED | RC_S_FAILSAFE, 125 }, u;
	uint8_t buf[32];
	uint32_t id;

	CHECK_EQ(rc_tele_pack(buf, &t), RC_TELE_LEN);
	CHECK(rc_tele_unpack(buf, RC_TELE_LEN, &u));
	CHECK_EQ(u.seq, 200);
	CHECK_EQ(u.vbat_mv, 3712);
	CHECK_EQ(u.state, RC_S_ARMED | RC_S_FAILSAFE);
	CHECK_EQ(u.rate_half, 125);
	buf[3] ^= 0x10;
	CHECK(!rc_tele_unpack(buf, RC_TELE_LEN, &u));
	/* a control packet is not telemetry and the other way round */
	{
		rc_cmd_t c = { 0, 0, 0, 0, 0 };
		uint8_t seq, hop;

		rc_ctrl_pack(buf, 1, 1, &c);
		CHECK(!rc_tele_unpack(buf, RC_TELE_LEN, &u));
		rc_tele_pack(buf, &t);
		CHECK(!rc_ctrl_unpack(buf, RC_TELE_LEN, &seq, &hop, &c));
	}

	CHECK_EQ(rc_bind_pack(buf, 0xC0FFEE42UL), RC_BIND_LEN);
	CHECK(rc_bind_unpack(buf, RC_BIND_LEN, &id));
	CHECK_EQ(id, 0xC0FFEE42UL);
	rc_bind_pack(buf, 0);
	CHECK(!rc_bind_unpack(buf, RC_BIND_LEN, &id));		/* id 0 is the bind address itself */
}

static void test_addr_and_hops(void) {
	uint8_t a[5], b[5], ch[RC_HOPS], ch2[RC_HOPS];
	int differ = 0;

	rc_addr(0, a);
	CHECK(memcmp(a, "BCRKA", 5) == 0);
	rc_addr(0x12345678UL, a);
	rc_addr(0x12345678UL, b);
	CHECK(memcmp(a, b, 5) == 0);
	rc_addr(0x12345679UL, b);
	CHECK(memcmp(a, b, 5) != 0);

	for (uint32_t id = 1; id < 2000; id += 7) {
		rc_addr(id, a);
		CHECK(a[4] != 0x55 && a[4] != 0xAA && a[4] != 0xFF && a[4] != 0x00);
		rc_hop_table(id, ch);
		for (int i = 0; i < RC_HOPS; i++) {
			CHECK(ch[i] >= 2 && ch[i] <= 80 && ch[i] != RC_BIND_CH);
			for (int j = 0; j < i; j++) {
				int d = (int)ch[i] - (int)ch[j];

				CHECK(d >= 3 || d <= -3);
			}
		}
		rc_hop_table(id + 1, ch2);
		differ += memcmp(ch, ch2, RC_HOPS) != 0;
	}
	CHECK(differ > 250);
}

/* One remote, one receiver, a period per step. A packet arrives when the
 * receiver listens on its channel and neither direction loses it. */
typedef struct {
	uint32_t good, sent, max_gap_ms;
} sim_t;

/* jitter_ms: a packet of the remote may leave up to this much late (a slow
 * handler before its timer message) */
static uint32_t sim_jitter_ms;

static void sim_run(uint32_t periods, uint32_t loss_data, uint32_t loss_ack,
					uint32_t outage_from, uint32_t outage_len, uint32_t* resync_ms, sim_t* out) {
	rc_hop_tx_t tx;
	rc_hop_rx_t rx;
	uint32_t now = 0, last_good = 0;
	uint8_t acked = 1, first = 1;

	rc_hop_tx_init(&tx);
	rc_hop_rx_init(&rx, 0);
	memset(out, 0, sizeof(*out));
	*resync_ms = 0;
	for (uint32_t p = 0; p < periods; p++) {
		uint8_t hop = first ? 0 : rc_hop_tx_next(&tx, acked);
		int dead = p >= outage_from && p < outage_from + outage_len;

		uint32_t period = RC_PERIOD_MS + (sim_jitter_ms ? rnd() % (sim_jitter_ms + 1) : 0);

		first = 0;
		/* the receiver gets on with its own clock during the period */
		for (uint32_t ms = 0; ms < period; ms++) {
			rc_hop_rx_tick(&rx, now + ms);
		}
		now += period;
		out->sent++;
		acked = 0;
		if (!dead && rx.hop == hop && !chance(loss_data)) {
			rc_hop_rx_got(&rx, hop, now);
			acked = !chance(loss_ack);
			if (now - last_good > out->max_gap_ms && out->good) {
				out->max_gap_ms = now - last_good;
			}
			if (outage_len && p >= outage_from + outage_len && *resync_ms == 0) {
				*resync_ms = (p - (outage_from + outage_len) + 1) * RC_PERIOD_MS;
			}
			last_good = now;
			out->good++;
		}
	}
}

static void test_hopping_through_loss(void) {
	sim_t s;
	uint32_t r;

	rng = 7;
	sim_run(50000, 0, 0, 0, 0, &r, &s);
	CHECK_EQ(s.good, s.sent);					/* clean air: every packet */

	sim_run(100000, 50, 50, 0, 0, &r, &s);		/* 5 % lost each way */
	printf("    5%% loss each way: %u of %u packets, longest gap %u ms\n", s.good, s.sent, s.max_gap_ms);
	CHECK(s.good * 100U >= s.sent * 85U);		/* hopping adds little to the raw loss */
	CHECK(s.max_gap_ms < RC_FAILSAFE_MS);

	sim_run(100000, 200, 100, 0, 0, &r, &s);	/* bad: 20 % / 10 % */
	printf("    20%%/10%% loss: %u of %u packets, longest gap %u ms\n", s.good, s.sent, s.max_gap_ms);
	CHECK(s.good * 100U >= s.sent * 60U);
	CHECK(s.max_gap_ms < RC_FAILSAFE_MS);

	/* a remote whose packets leave up to 3 ms late (period 4..7 ms): the
	 * remote keeps its radio handler that punctual (rc_tx.c) */
	sim_jitter_ms = 3;
	sim_run(100000, 0, 0, 0, 0, &r, &s);
	printf("    jitter 0..3 ms, no loss: %u of %u packets, longest gap %u ms\n", s.good, s.sent, s.max_gap_ms);
	CHECK_EQ(s.good, s.sent);					/* following by index: timing does not matter */
	sim_run(100000, 50, 50, 0, 0, &r, &s);
	printf("    jitter 0..3 ms, 5%% loss: %u of %u packets, longest gap %u ms\n", s.good, s.sent, s.max_gap_ms);
	CHECK(s.good * 100U >= s.sent * 85U);
	CHECK(s.max_gap_ms < RC_FAILSAFE_MS);
	/* far worse than designed for (0..6 ms): still no failsafe at 5 % loss */
	sim_jitter_ms = 6;
	sim_run(100000, 50, 50, 0, 0, &r, &s);
	printf("    jitter 0..6 ms, 5%% loss: %u of %u packets, longest gap %u ms\n", s.good, s.sent, s.max_gap_ms);
	CHECK(s.max_gap_ms < RC_FAILSAFE_MS);
	sim_jitter_ms = 0;
}

/* after an outage of any length the receiver is back within a dwell and a
 * round of the remote (no loss otherwise) */
static void test_resync_after_outage(void) {
	sim_t s;
	uint32_t r, worst = 0;
	const uint32_t bound = RC_DWELL_MS + 2U * RC_HOPS * RC_PERIOD_MS;

	for (uint32_t len = 1; len < 400; len += 3) {
		for (uint32_t from = 100; from < 120; from++) {
			sim_run(from + len + 200, 0, 0, from, len, &r, &s);
			CHECK(r > 0);
			worst = r > worst ? r : worst;
		}
	}
	printf("    worst resync after an outage: %u ms (bound %u ms, failsafe %u ms)\n", worst, bound, RC_FAILSAFE_MS);
	CHECK(worst <= bound);
	CHECK(bound < RC_FAILSAFE_MS);
}

static void test_arming_and_failsafe(void) {
	rc_rx_t r;
	rc_cmd_t c = { 0, 0, 0, 0, 0 }, o;
	uint32_t t = 1000;

	rc_rx_init(&r);
	CHECK(r.failsafe && !r.armed);
	rc_rx_output(&r, &o);
	CHECK_EQ(o.thr, 0);

	/* arm switch on with the throttle up: no */
	c.thr = 300;
	c.flags = RC_F_ARM;
	rc_rx_packet(&r, &c, t);
	CHECK(!r.armed);
	/* still on, throttle down: still no - it needs a rising edge */
	c.thr = 0;
	rc_rx_packet(&r, &c, t += 4);
	CHECK(!r.armed);
	/* off, then on with the throttle down: armed */
	c.flags = 0;
	rc_rx_packet(&r, &c, t += 4);
	c.flags = RC_F_ARM;
	rc_rx_packet(&r, &c, t += 4);
	CHECK(r.armed);
	c.thr = 600;
	rc_rx_packet(&r, &c, t += 4);
	rc_rx_output(&r, &o);
	CHECK_EQ(o.thr, 600);
	CHECK(o.flags & RC_F_ARM);

	/* silence just below the failsafe time: still armed */
	rc_rx_tick(&r, t + RC_FAILSAFE_MS - 1);
	CHECK(r.armed && !r.failsafe);
	/* at the failsafe time: off, throttle 0 */
	rc_rx_tick(&r, t + RC_FAILSAFE_MS);
	CHECK(!r.armed && r.failsafe && r.failsafe_seen);
	rc_rx_output(&r, &o);
	CHECK_EQ(o.thr, 0);
	CHECK_EQ(o.flags, 0);

	/* the remote comes back with the switch still on: stays disarmed */
	t += 1000;
	c.thr = 0;
	rc_rx_packet(&r, &c, t);
	CHECK(!r.armed && !r.failsafe);
	c.flags = 0;
	rc_rx_packet(&r, &c, t += 4);
	c.flags = RC_F_ARM;
	rc_rx_packet(&r, &c, t += 4);
	CHECK(r.armed && !r.failsafe_seen);

	/* switch off: disarmed at once */
	c.flags = 0;
	rc_rx_packet(&r, &c, t += 4);
	CHECK(!r.armed);
}

static void test_sticks(void) {
	rc_cal_t cal;
	rc_cmd_t c;
	uint16_t raw[4];

	rc_cal_default(&cal, 26400);			/* ADS1115 at +-4.096 V, pots on 3.3 V */
	raw[0] = raw[1] = raw[2] = 13200;
	raw[3] = cal.axis[3].min;
	rc_cal_apply(&cal, raw, &c);
	CHECK_EQ(c.roll, 0);
	CHECK_EQ(c.yaw, 0);
	CHECK_EQ(c.thr, 0);

	raw[0] = cal.axis[0].max;
	raw[1] = cal.axis[1].min;
	raw[2] = 26400;							/* beyond the calibrated end */
	raw[3] = cal.axis[3].max;
	rc_cal_apply(&cal, raw, &c);
	CHECK_EQ(c.roll, 500);
	CHECK_EQ(c.pitch, -500);
	CHECK_EQ(c.yaw, 500);
	CHECK_EQ(c.thr, 1000);

	/* inside the dead band: 0; just past it: small, same sign */
	raw[0] = (uint16_t)(13200 + 12 * (cal.axis[0].max - 13200) / 500);
	raw[1] = (uint16_t)(13200 - 30 * (13200 - cal.axis[1].min) / 500);
	rc_cal_apply(&cal, raw, &c);
	CHECK_EQ(c.roll, 0);
	CHECK(c.pitch < 0 && c.pitch > -30);

	/* a broken calibration gives 0, never a division by zero */
	memset(&cal, 0, sizeof(cal));
	rc_cal_apply(&cal, raw, &c);
	CHECK_EQ(c.roll, 0);
	CHECK_EQ(c.thr, 0);
}

static void test_nvm_record(void) {
	uint8_t rec[RC_NVM_SIZE];
	rc_cal_t cal, back;
	uint32_t id;

	memset(rec, 0, sizeof(rec));			/* erased EEPROM of the L1 reads 0 */
	CHECK(!rc_nvm_get_id(rec, &id));
	CHECK(!rc_nvm_get_cal(rec, &back));

	rc_nvm_put_id(rec, 0xA1B2C3D4UL);
	rc_cal_default(&cal, 4095);
	cal.axis[2].mid = 2100;
	rc_nvm_put_cal(rec, &cal);
	CHECK(rc_nvm_get_id(rec, &id));
	CHECK_EQ(id, 0xA1B2C3D4UL);
	CHECK(rc_nvm_get_cal(rec, &back));
	CHECK(memcmp(&cal, &back, sizeof(cal)) == 0);

	rec[2] ^= 1;
	CHECK(!rc_nvm_get_id(rec, &id));
	rec[2] ^= 1;
	rec[12] ^= 0x40;
	CHECK(!rc_nvm_get_cal(rec, &back));
}

TT_MAIN_BEGIN("test_rc")
	RUN_TEST(test_ctrl_packet);
	RUN_TEST(test_tele_and_bind);
	RUN_TEST(test_addr_and_hops);
	RUN_TEST(test_hopping_through_loss);
	RUN_TEST(test_resync_after_outage);
	RUN_TEST(test_arming_and_failsafe);
	RUN_TEST(test_sticks);
	RUN_TEST(test_nvm_record);
TT_MAIN_END()
