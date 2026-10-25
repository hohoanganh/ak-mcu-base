/**
 * Flight control core (src/fc) on host: signs of the mixer, Mahony converges
 * to the real tilt and integrates the gyro the right way round, the Q16 rate
 * loop gives the same motors as the float one, failsafe-ish edges (throttle
 * 0, saturation). Timing is measured on the kit (fcb, port/stm32l151/fc_bench.c).
 */
#include <math.h>
#include <string.h>

#include "fc_ctrl.h"
#include "tiny_test.h"

static const fc_rc_t rc_hover = { 0, 0, 0, 500 };

static fc_imu_t imu_level(void) {
	fc_imu_t m;

	memset(&m, 0, sizeof(m));
	m.acc[2] = 4096;		/* 1 g at +-8 g */
	return m;
}

static void test_inv_sqrt(void) {
	static const float xs[] = { 1e-4f, 0.25f, 1.0f, 2.0f, 4096.0f * 4096.0f, 3e7f };

	for (unsigned i = 0; i < sizeof(xs) / sizeof(xs[0]); i++) {
		float r = fc_inv_sqrt(xs[i]) * sqrtf(xs[i]);

		CHECK(fabsf(r - 1.0f) < 1e-5f);
	}
}

/* level, sticks centred: all four motors at throttle, exactly */
static void test_hover_balanced(void) {
	fc_f_t f;
	fc_q_t q;
	fc_imu_t m = imu_level();
	uint16_t mf[4], mq[4];

	fc_f_init(&f);
	fc_q_init(&q);
	for (int i = 0; i < 100; i++) {
		fc_f_step(&f, &m, &rc_hover, 1, mf);
		fc_q_step(&q, &m, &rc_hover, mq);
	}
	for (int k = 0; k < 4; k++) {
		CHECK_EQ(mf[k], 500);
		CHECK_EQ(mq[k], 500);
	}
}

/* roll right asked, nothing moving yet: left motors (2, 3) speed up, right
 * ones (0, 1) slow down; pitch forward (nose down) = rear (0, 2) up */
static void test_mixer_signs(void) {
	fc_f_t f;
	fc_imu_t m = imu_level();
	fc_rc_t rc = rc_hover;
	uint16_t mo[4];

	fc_f_init(&f);
	rc.roll = 200;
	fc_f_step(&f, &m, &rc, 0, mo);
	CHECK(mo[2] > 500 && mo[3] > 500);
	CHECK(mo[0] < 500 && mo[1] < 500);

	fc_f_init(&f);
	rc.roll = 0;
	rc.pitch = 200;
	fc_f_step(&f, &m, &rc, 0, mo);
	CHECK(mo[0] > 500 && mo[2] > 500);
	CHECK(mo[1] < 500 && mo[3] < 500);

	/* yaw: diagonal pairs move against each other, the sum stays */
	fc_f_init(&f);
	rc.pitch = 0;
	rc.yaw = 200;
	fc_f_step(&f, &m, &rc, 0, mo);
	CHECK(mo[0] == mo[3] && mo[1] == mo[2] && mo[0] != mo[1]);
	CHECK_EQ(mo[0] + mo[1] + mo[2] + mo[3], 2000);
}

/* the body already turns the asked way: the rate loop backs off (P sees no
 * error, D opposes the change) */
static void test_rate_loop_feedback(void) {
	fc_f_t f;
	fc_imu_t m = imu_level();
	fc_rc_t rc = rc_hover;
	uint16_t mo[4];

	fc_f_init(&f);
	rc.roll = 100;							/* 2 rad/s */
	m.gyro[0] = (int16_t)(2.0f / 0.0010642f);
	for (int i = 0; i < 50; i++) {
		fc_f_step(&f, &m, &rc, 0, mo);
	}
	CHECK(mo[3] >= 495 && mo[3] <= 505);	/* no error left: back near throttle */
}

/* throttle 0 = motors off whatever the sticks say; saturation stays in range */
static void test_motor_limits(void) {
	fc_f_t f;
	fc_q_t q;
	fc_imu_t m = imu_level();
	fc_rc_t rc = { 500, -500, 500, 0 };
	uint16_t mf[4], mq[4];

	fc_f_init(&f);
	fc_q_init(&q);
	fc_f_step(&f, &m, &rc, 0, mf);
	fc_q_step(&q, &m, &rc, mq);
	for (int k = 0; k < 4; k++) {
		CHECK_EQ(mf[k], 0);
		CHECK_EQ(mq[k], 0);
	}
	rc.thr = 1000;
	m.gyro[0] = -32768;
	m.gyro[1] = 32767;
	for (int i = 0; i < 300; i++) {
		fc_f_step(&f, &m, &rc, 0, mf);
		fc_q_step(&q, &m, &rc, mq);
	}
	for (int k = 0; k < 4; k++) {
		CHECK(mf[k] <= FC_MOTOR_MAX);
		CHECK(mq[k] <= FC_MOTOR_MAX);
	}
}

/* still, tilted 0.3 rad to the right: the estimate converges to it */
static void test_mahony_converges(void) {
	fc_f_t f;
	fc_imu_t m = imu_level();
	uint16_t mo[4];

	fc_f_init(&f);
	m.acc[1] = (int16_t)(4096.0f * sinf(0.3f));
	m.acc[2] = (int16_t)(4096.0f * cosf(0.3f));
	for (int i = 0; i < 20000; i++) {		/* 20 s: correction time constant ~2 s */
		fc_f_step(&f, &m, &rc_hover, 1, mo);
	}
	CHECK(fabsf(f.roll - sinf(0.3f)) < 0.01f);
	CHECK(fabsf(f.pitch) < 0.01f);

	/* nose down 0.2 rad */
	fc_f_init(&f);
	m = imu_level();
	m.acc[0] = (int16_t)(-4096.0f * sinf(0.2f));
	m.acc[2] = (int16_t)(4096.0f * cosf(0.2f));
	for (int i = 0; i < 20000; i++) {		/* 20 s: correction time constant ~2 s */
		fc_f_step(&f, &m, &rc_hover, 1, mo);
	}
	CHECK(fabsf(f.pitch - sinf(0.2f)) < 0.01f);
}

/* no accelerometer: the gyro alone turns the estimate, in the gyro's direction */
static void test_mahony_gyro_sign(void) {
	fc_f_t f;
	fc_imu_t m;
	uint16_t mo[4];

	memset(&m, 0, sizeof(m));
	fc_f_init(&f);
	m.gyro[0] = (int16_t)(1.0f / 0.0010642f);	/* 1 rad/s roll right */
	for (int i = 0; i < 300; i++) {
		fc_f_step(&f, &m, &rc_hover, 1, mo);
	}
	/* 0.3 s minus the filter lag of a few ms */
	CHECK(f.roll > sinf(0.28f) && f.roll < sinf(0.31f));
	CHECK(fabsf(f.pitch) < 0.01f);
}

/* angle mode: tilted right without a stick, it asks to roll back left */
static void test_angle_mode_levels(void) {
	fc_f_t f;
	fc_imu_t m = imu_level();
	uint16_t mo[4];

	fc_f_init(&f);
	m.acc[1] = (int16_t)(4096.0f * sinf(0.3f));
	m.acc[2] = (int16_t)(4096.0f * cosf(0.3f));
	for (int i = 0; i < 3000; i++) {
		fc_f_step(&f, &m, &rc_hover, 1, mo);
	}
	CHECK(mo[0] > 500 && mo[1] > 500);		/* right side up */
	CHECK(mo[2] < 500 && mo[3] < 500);
}

/* the fixed point rate loop tracks the float one within 2 permille over a
 * noisy flight-like input */
static void test_q16_matches_float(void) {
	fc_f_t f;
	fc_q_t q;
	fc_imu_t m = imu_level();
	fc_rc_t rc = rc_hover;
	uint16_t mf[4], mq[4];
	uint32_t x = 12345;
	int worst = 0;

	fc_f_init(&f);
	fc_q_init(&q);
	for (int i = 0; i < 20000; i++) {
		for (int a = 0; a < 3; a++) {
			x = x * 1103515245UL + 12345UL;
			m.gyro[a] = (int16_t)(((int32_t)(x >> 16) & 0x3FF) - 512 + (int32_t)(3000.0f * sinf(i * 0.002f * (a + 1))));
		}
		rc.roll = (int16_t)(300.0f * sinf(i * 0.001f));
		rc.pitch = (int16_t)(-200.0f * cosf(i * 0.0013f));
		rc.yaw = (int16_t)(100.0f * sinf(i * 0.0007f));
		rc.thr = (uint16_t)(450 + (i / 50) % 100);
		fc_f_step(&f, &m, &rc, 0, mf);
		fc_q_step(&q, &m, &rc, mq);
		for (int k = 0; k < 4; k++) {
			int d = (int)mf[k] - (int)mq[k];

			d = d < 0 ? -d : d;
			worst = d > worst ? d : worst;
		}
	}
	printf("    worst float/Q16 difference: %d permille\n", worst);
	CHECK(worst <= 2);
}

TT_MAIN_BEGIN("test_fc")
	RUN_TEST(test_inv_sqrt);
	RUN_TEST(test_hover_balanced);
	RUN_TEST(test_mixer_signs);
	RUN_TEST(test_rate_loop_feedback);
	RUN_TEST(test_motor_limits);
	RUN_TEST(test_mahony_converges);
	RUN_TEST(test_mahony_gyro_sign);
	RUN_TEST(test_angle_mode_levels);
	RUN_TEST(test_q16_matches_float);
TT_MAIN_END()
