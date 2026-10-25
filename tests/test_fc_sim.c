/**
 * The flight controller (src/fc: fc_loop + fc_ctrl) flown in a simulated
 * quadcopter on host, step P2 of docs/drone-ak-mcu.md. Built twice: at the
 * default 1 kHz and at the 500 Hz of the fc build (FC_LOOP_HZ).
 *
 * Model: 65 mm brushed whoop, 30 g, 4 x 0.12 N, motor lag 20 ms, rigid body
 * (rotation only: the attitude loop is what P2 is about), gyro noise and
 * bias, accelerometer = gravity in the body frame. Body frame x forward,
 * y left, z up; motors as the QUADX mixer of fc_ctrl.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fc_loop.h"
#include "tiny_test.h"

#define DT				(1.0f / FC_LOOP_HZ)
#define GYRO_LSB		(0.0010642f)	/* rad/s per count, 2000 dps */
#define ACC_1G			(4096)
#define MASS			(0.030f)
#define T_MAX			(0.12f)			/* N per motor at full PWM */
#define LEVER			(0.023f)		/* m: 32.5 mm arm at 45 degrees */
#define YAW_K			(0.006f)		/* N m of reaction per N of thrust */
#define TAU_MOTOR		(0.020f)		/* s */
#define IXX				(1.6e-5f)
#define IYY				(1.6e-5f)
#define IZZ				(2.8e-5f)

static const float pos_x[4] = { -1, +1, -1, +1 };	/* rear right, front right, rear left, front left */
static const float pos_y[4] = { -1, -1, +1, +1 };
static const float spin[4] = { +1, -1, -1, +1 };		/* reaction torque sign, as the yaw column of the mixer */

typedef struct {
	float q[4];			/* body -> world */
	float w[3];			/* body rates, rad/s */
	float thrust[4];	/* N, lagging the command */
	int16_t bias[3];	/* gyro bias, counts */
	uint32_t rng;
} sim_quad_t;

static float noise(sim_quad_t* s, float amp) {
	s->rng = s->rng * 1103515245UL + 12345UL;
	return amp * ((float)((s->rng >> 16) & 0x7FFF) / 16384.0f - 1.0f);
}

static void quad_init(sim_quad_t* s, float roll, float pitch) {
	memset(s, 0, sizeof(*s));
	/* small rotation: roll about x, then pitch about y */
	s->q[0] = cosf(roll / 2) * cosf(pitch / 2);
	s->q[1] = sinf(roll / 2) * cosf(pitch / 2);
	s->q[2] = cosf(roll / 2) * sinf(pitch / 2);
	s->q[3] = -sinf(roll / 2) * sinf(pitch / 2);
	s->bias[0] = 37;
	s->bias[1] = -52;
	s->bias[2] = 18;
	s->rng = 99;
}

/* up vector in the body frame: y = sin(roll), x = -sin(pitch) */
static void up_body(const sim_quad_t* s, float* u) {
	const float* q = s->q;

	u[0] = 2 * (q[1] * q[3] - q[0] * q[2]);
	u[1] = 2 * (q[0] * q[1] + q[2] * q[3]);
	u[2] = q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3];
}

static float roll_of(const sim_quad_t* s) {
	float u[3];

	up_body(s, u);
	return atan2f(u[1], u[2]);
}

static float pitch_of(const sim_quad_t* s) {
	float u[3];

	up_body(s, u);
	return asinf(u[0] < -1 ? 1 : u[0] > 1 ? -1 : -u[0]);
}

static void sensors(sim_quad_t* s, int16_t* g, int16_t* a) {
	float u[3];

	up_body(s, u);
	for (int i = 0; i < 3; i++) {
		g[i] = (int16_t)(s->w[i] / GYRO_LSB + s->bias[i] + noise(s, 12.0f));
		a[i] = (int16_t)(u[i] * ACC_1G + noise(s, 40.0f));
	}
}

static void physics(sim_quad_t* s, const uint16_t* motor) {
	float tq[3] = { 0, 0, 0 };
	float* q = s->q;
	float w0 = s->w[0], w1 = s->w[1], w2 = s->w[2];
	float n;

	for (int k = 0; k < 4; k++) {
		float target = T_MAX * motor[k] / (float)FC_MOTOR_MAX;

		s->thrust[k] += (target - s->thrust[k]) * (DT / (TAU_MOTOR + DT));
		tq[0] += pos_y[k] * LEVER * s->thrust[k];
		tq[1] += -pos_x[k] * LEVER * s->thrust[k];
		tq[2] += spin[k] * YAW_K * s->thrust[k];
	}
	s->w[0] += DT * (tq[0] - (IZZ - IYY) * w1 * w2) / IXX;
	s->w[1] += DT * (tq[1] - (IXX - IZZ) * w0 * w2) / IYY;
	s->w[2] += DT * (tq[2] - (IYY - IXX) * w0 * w1) / IZZ;
	/* q += 0.5 q x (0, w) dt */
	{
		float a = q[0], b = q[1], c = q[2], d = q[3];
		float hx = 0.5f * DT * s->w[0], hy = 0.5f * DT * s->w[1], hz = 0.5f * DT * s->w[2];

		q[0] = a - b * hx - c * hy - d * hz;
		q[1] = b + a * hx + c * hz - d * hy;
		q[2] = c + a * hy - b * hz + d * hx;
		q[3] = d + a * hz + b * hy - c * hx;
	}
	n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	for (int i = 0; i < 4; i++) {
		q[i] /= n;
	}
}

static const fc_orient_t orient = FC_ORIENT_DEFAULT;
static const uint16_t hover_thr = (uint16_t)(MASS * 9.81f / (4 * T_MAX) * FC_MOTOR_MAX + 0.5f);

/* t seconds of flight; the radio sends cmd every period while send = 1 */
static void fly(fc_loop_t* f, sim_quad_t* s, const rc_cmd_t* cmd, uint8_t armed, float t, uint32_t* now_ms, uint8_t send) {
	uint32_t n = (uint32_t)(t * FC_LOOP_HZ);
	uint16_t m[4];
	int16_t g[3], a[3];

	for (uint32_t i = 0; i < n; i++) {
		uint32_t ms = (uint32_t)(((uint64_t)f->ticks * 1000U) / FC_LOOP_HZ);

		*now_ms = ms;
		if (send) {
			fc_loop_cmd(f, cmd, armed, ms);
		}
		sensors(s, g, a);
		fc_loop_step(f, g, a, 1, ms, m);
		physics(s, m);
	}
}

/* on the ground, still and level: calibration, then armed */
static void take_off(fc_loop_t* f, sim_quad_t* s, uint32_t* now) {
	rc_cmd_t idle = { 0, 0, 0, 0, RC_F_ANGLE };

	fc_loop_init(f, &orient, FC_MOTOR_MAX);
	quad_init(s, 0, 0);
	fly(f, s, &idle, 0, 2.5f, now, 1);			/* calibration (1 s) + the estimate settles */
	idle.flags |= RC_F_ARM;
	fly(f, s, &idle, 1, 0.05f, now, 1);
}

static void test_gyro_calibration(void) {
	fc_loop_t f;
	sim_quad_t s;
	uint32_t now = 0;
	rc_cmd_t idle = { 0, 0, 0, 0, 0 };

	fc_loop_init(&f, &orient, FC_LIMIT_BENCH);
	quad_init(&s, 0, 0);
	fly(&f, &s, &idle, 0, 0.5f, &now, 1);
	CHECK(!f.calibrated);
	CHECK_EQ(f.state, FC_CALIBRATING);
	fly(&f, &s, &idle, 0, 1.0f, &now, 1);
	CHECK(f.calibrated);
	for (int a = 0; a < 3; a++) {
		CHECK(abs(f.bias[a] - s.bias[a]) <= 2);
	}
	CHECK_EQ(f.state, FC_OFF);

	/* moving during calibration: it starts again, never takes a wrong bias */
	fc_loop_recalibrate(&f);
	s.w[0] = 1.0f;									/* ~60 deg/s */
	fly(&f, &s, &idle, 0, 1.5f, &now, 1);
	CHECK(!f.calibrated);
}

static void test_gate(void) {
	fc_loop_t f;
	sim_quad_t s;
	uint32_t now = 0;
	rc_cmd_t c = { 0, 0, 0, 400, RC_F_ARM | RC_F_ANGLE };
	int16_t g[3] = { 0, 0, 0 }, a[3] = { 0, 0, ACC_1G };
	uint16_t m[4];

	/* armed before the gyro is calibrated: motors stay off, calibration waits */
	fc_loop_init(&f, &orient, FC_MOTOR_MAX);
	quad_init(&s, 0, 0);
	fly(&f, &s, &c, 1, 2.0f, &now, 1);
	CHECK(!f.calibrated);
	CHECK(f.motor[0] == 0 && f.motor[3] == 0);

	take_off(&f, &s, &now);
	fly(&f, &s, &c, 1, 0.1f, &now, 1);
	CHECK_EQ(f.state, FC_RUN);
	CHECK(f.motor[0] > 0);

	/* the radio goes quiet: off once the last command is RC_FAILSAFE_MS old */
	fly(&f, &s, &c, 1, (RC_FAILSAFE_MS - 20) / 1000.0f, &now, 0);
	CHECK_EQ(f.state, FC_RUN);
	fly(&f, &s, &c, 1, 0.04f, &now, 0);
	CHECK_EQ(f.state, FC_STOP_FAILSAFE);
	CHECK(f.motor[0] == 0 && f.motor[1] == 0 && f.motor[2] == 0 && f.motor[3] == 0);

	/* back, then the IMU stops answering */
	fly(&f, &s, &c, 1, 0.1f, &now, 1);
	CHECK_EQ(f.state, FC_RUN);
	for (int i = 0; i < FC_IMU_FAIL_MAX; i++) {
		fc_loop_cmd(&f, &c, 1, now);
		fc_loop_step(&f, g, a, 0, now, m);
	}
	CHECK_EQ(f.state, FC_STOP_IMU);
	CHECK(m[0] == 0 && m[3] == 0);

	/* the bench limit scales every motor */
	fc_loop_init(&f, &orient, 250);
	quad_init(&s, 0, 0);
	take_off(&f, &s, &now);
	f.limit = 250;
	c.thr = 1000;
	fly(&f, &s, &c, 1, 0.05f, &now, 1);
	for (int k = 0; k < 4; k++) {
		CHECK(f.motor[k] <= 250);
	}
}

/* hovering, a gust spins it at 6 rad/s around roll and 4 around pitch: angle
 * mode brings it back level, quickly and without oscillating */
static void test_recovers_from_gust(void) {
	fc_loop_t f;
	sim_quad_t s;
	uint32_t now = 0;
	rc_cmd_t c = { 0, 0, 0, 0, RC_F_ARM | RC_F_ANGLE };
	float wmax = 0;

	take_off(&f, &s, &now);
	c.thr = hover_thr;
	fly(&f, &s, &c, 1, 0.5f, &now, 1);
	CHECK(fabsf(roll_of(&s)) < 0.05f && fabsf(pitch_of(&s)) < 0.05f);

	s.w[0] = 6.0f;
	s.w[1] = -4.0f;
	for (int i = 0; i < 30; i++) {
		fly(&f, &s, &c, 1, 0.1f, &now, 1);
		for (int a = 0; a < 3; a++) {
			wmax = fabsf(s.w[a]) > wmax ? fabsf(s.w[a]) : wmax;
		}
		if (i == 9) {
			printf("    1 s after the gust: roll %.3f pitch %.3f rad\n", (double)roll_of(&s), (double)pitch_of(&s));
			CHECK(fabsf(roll_of(&s)) < 0.05f);
			CHECK(fabsf(pitch_of(&s)) < 0.05f);
		}
	}
	printf("    3 s after: roll %.3f pitch %.3f rad, rates %.2f %.2f %.2f, peak rate %.1f rad/s\n",
		   (double)roll_of(&s), (double)pitch_of(&s), (double)s.w[0], (double)s.w[1], (double)s.w[2], (double)wmax);
	CHECK(fabsf(roll_of(&s)) < 0.03f && fabsf(pitch_of(&s)) < 0.03f);
	CHECK(fabsf(s.w[0]) < 0.3f && fabsf(s.w[1]) < 0.3f && fabsf(s.w[2]) < 0.3f);
	CHECK(wmax < 10.0f);
}

/* stick to 0.3 rad of roll and back: it follows, and holds 0 on release */
static void test_angle_step(void) {
	fc_loop_t f;
	sim_quad_t s;
	uint32_t now = 0;
	rc_cmd_t c = { 0, 0, 0, 0, RC_F_ARM | RC_F_ANGLE };

	take_off(&f, &s, &now);
	c.thr = hover_thr;
	fly(&f, &s, &c, 1, 0.3f, &now, 1);
	c.roll = 250;									/* 0.3 rad */
	c.pitch = -125;									/* -0.15 rad */
	fly(&f, &s, &c, 1, 1.0f, &now, 1);
	printf("    stick roll 0.3 / pitch -0.15 rad: roll %.3f pitch %.3f\n", (double)roll_of(&s), (double)pitch_of(&s));
	CHECK(fabsf(roll_of(&s) - 0.3f) < 0.06f);
	CHECK(fabsf(pitch_of(&s) + 0.15f) < 0.06f);
	c.roll = c.pitch = 0;
	fly(&f, &s, &c, 1, 1.0f, &now, 1);
	CHECK(fabsf(roll_of(&s)) < 0.05f && fabsf(pitch_of(&s)) < 0.05f);
}

/* acro: the sticks are rates */
static void test_acro_rates(void) {
	fc_loop_t f;
	sim_quad_t s;
	uint32_t now = 0;
	rc_cmd_t c = { 0, 0, 0, 0, RC_F_ARM };

	take_off(&f, &s, &now);
	c.thr = hover_thr;
	fly(&f, &s, &c, 1, 0.3f, &now, 1);
	c.roll = 150;									/* 3 rad/s */
	fly(&f, &s, &c, 1, 0.25f, &now, 1);
	printf("    acro roll 3 rad/s: %.2f\n", (double)s.w[0]);
	CHECK(fabsf(s.w[0] - 3.0f) < 0.6f);
	c.roll = 0;
	c.yaw = 100;									/* 2 rad/s */
	fly(&f, &s, &c, 1, 0.5f, &now, 1);
	printf("    acro yaw 2 rad/s: %.2f (roll %.2f)\n", (double)s.w[2], (double)s.w[0]);
	CHECK(fabsf(s.w[2] - 2.0f) < 0.5f);
	CHECK(fabsf(s.w[0]) < 0.5f);
}

TT_MAIN_BEGIN("test_fc_sim")
	printf("  loop %d Hz, hover throttle %u\n", FC_LOOP_HZ, hover_thr);
	RUN_TEST(test_gyro_calibration);
	RUN_TEST(test_gate);
	RUN_TEST(test_recovers_from_gust);
	RUN_TEST(test_angle_step);
	RUN_TEST(test_acro_rates);
TT_MAIN_END()
