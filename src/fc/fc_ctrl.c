#include <string.h>

#include "fc_ctrl.h"

/* The two implementations share every constant, so fc_q_step() and the acro
 * path of fc_f_step() give the same motors (test_fc checks it). */
#define FC_DT				(1.0f / FC_LOOP_HZ)
#define GYRO_RAD_PER_LSB	(0.0010642f)	/* (pi / 180) / 16.4 */
#define STICK_RATE			(0.02f)			/* rad/s per stick unit: 500 -> 10 rad/s */
#define STICK_ANGLE			(0.0012f)		/* rad per stick unit: 500 -> 0.6 rad (34 deg) */
#define GYRO_ALPHA			(0.3612f)		/* PT1 at 90 Hz, 1 kHz */
#define DTERM_ALPHA			(0.3859f)		/* PT1 at 100 Hz, 1 kHz */
#define I_LIMIT				(200.0f)		/* permille */
#define PID_LIMIT			(400.0f)		/* permille */
#define MAHONY_TWO_KP		(1.0f)
#define ANGLE_KP			(6.0f)			/* rad/s per rad */

static const float pid_gains[3][3] = {
	/* kp (permille per rad/s), ki (per rad), kd (permille per rad/s^2) */
	{ 40.0f, 40.0f, 0.4f },		/* roll */
	{ 40.0f, 40.0f, 0.4f },		/* pitch */
	{ 60.0f, 30.0f, 0.0f },		/* yaw */
};

/* QUADX, Betaflight order: rear right, front right, rear left, front left.
 * Roll + (right down) needs the left motors up, pitch + (nose down) the rear
 * ones; yaw follows the Betaflight table for its default prop direction. */
static const int8_t mix[4][3] = {
	{ -1, +1, +1 },
	{ -1, -1, -1 },
	{ +1, +1, -1 },
	{ +1, -1, +1 },
};

float fc_inv_sqrt(float x) {
	float half = 0.5f * x;
	float y = x;
	int32_t i;

	memcpy(&i, &y, sizeof(i));
	i = 0x5F3759DF - (i >> 1);
	memcpy(&y, &i, sizeof(y));
	y = y * (1.5f - half * y * y);
	y = y * (1.5f - half * y * y);
	return y;
}

static float clampf(float v, float lim) {
	return v > lim ? lim : v < -lim ? -lim : v;
}

static uint16_t motor_out(float v) {
	if (v <= 0.0f) {
		return 0;
	}
	if (v >= (float)FC_MOTOR_MAX) {
		return FC_MOTOR_MAX;
	}
	return (uint16_t)(v + 0.5f);
}

/*----------------------------------------------------------------------------
 * float
 *--------------------------------------------------------------------------*/
void fc_f_init(fc_f_t* s) {
	memset(s, 0, sizeof(*s));
	s->q0 = 1.0f;
	for (int a = 0; a < 3; a++) {
		s->rate[a].kp = pid_gains[a][0];
		s->rate[a].ki = pid_gains[a][1];
		s->rate[a].kd = pid_gains[a][2];
	}
	s->angle_kp = ANGLE_KP;
}

/* Mahony (Madgwick's formulation): gyro integration corrected towards the
 * direction of gravity the accelerometer sees */
static void mahony(fc_f_t* s, float gx, float gy, float gz, const int16_t* acc) {
	float ax = acc[0], ay = acc[1], az = acc[2];
	float q0 = s->q0, q1 = s->q1, q2 = s->q2, q3 = s->q3;
	float n2 = ax * ax + ay * ay + az * az;

	if (n2 > 0.0f) {
		float r = fc_inv_sqrt(n2);
		float hvx, hvy, hvz;

		ax *= r;
		ay *= r;
		az *= r;
		hvx = q1 * q3 - q0 * q2;
		hvy = q0 * q1 + q2 * q3;
		hvz = q0 * q0 - 0.5f + q3 * q3;
		gx += MAHONY_TWO_KP * (ay * hvz - az * hvy);
		gy += MAHONY_TWO_KP * (az * hvx - ax * hvz);
		gz += MAHONY_TWO_KP * (ax * hvy - ay * hvx);
	}
	gx *= 0.5f * FC_DT;
	gy *= 0.5f * FC_DT;
	gz *= 0.5f * FC_DT;
	s->q0 = q0 + (-q1 * gx - q2 * gy - q3 * gz);
	s->q1 = q1 + (q0 * gx + q2 * gz - q3 * gy);
	s->q2 = q2 + (q0 * gy - q1 * gz + q3 * gx);
	s->q3 = q3 + (q0 * gz + q1 * gy - q2 * gx);
	n2 = fc_inv_sqrt(s->q0 * s->q0 + s->q1 * s->q1 + s->q2 * s->q2 + s->q3 * s->q3);
	s->q0 *= n2;
	s->q1 *= n2;
	s->q2 *= n2;
	s->q3 *= n2;
	/* up vector in the body frame: y component = sin(roll), x = -sin(pitch);
	 * small angle is enough for self level */
	s->roll = 2.0f * (s->q0 * s->q1 + s->q2 * s->q3);
	s->pitch = -2.0f * (s->q1 * s->q3 - s->q0 * s->q2);
}

static float pid_f(fc_pid_f_t* p, float sp, float meas) {
	float err = sp - meas;
	float d;

	p->i = clampf(p->i + p->ki * FC_DT * err, I_LIMIT);
	d = -(meas - p->prev_meas) * (p->kd * FC_LOOP_HZ);	/* D on measurement: no kick on stick moves */
	p->prev_meas = meas;
	p->d_lpf += DTERM_ALPHA * (d - p->d_lpf);
	return clampf(p->kp * err + p->i + p->d_lpf, PID_LIMIT);
}

void fc_f_step(fc_f_t* s, const fc_imu_t* imu, const fc_rc_t* rc, uint8_t angle, uint16_t motor[4]) {
	float sp[3], out[3];
	const int16_t* stick = &rc->roll;

	for (int a = 0; a < 3; a++) {
		s->gyro[a] += GYRO_ALPHA * ((float)imu->gyro[a] * GYRO_RAD_PER_LSB - s->gyro[a]);
	}
	if (angle) {
		mahony(s, s->gyro[0], s->gyro[1], s->gyro[2], imu->acc);
		sp[0] = s->angle_kp * ((float)stick[0] * STICK_ANGLE - s->roll);
		sp[1] = s->angle_kp * ((float)stick[1] * STICK_ANGLE - s->pitch);
	}
	else {
		sp[0] = (float)stick[0] * STICK_RATE;
		sp[1] = (float)stick[1] * STICK_RATE;
	}
	sp[2] = (float)stick[2] * STICK_RATE;
	for (int a = 0; a < 3; a++) {
		out[a] = pid_f(&s->rate[a], sp[a], s->gyro[a]);
	}
	for (int m = 0; m < 4; m++) {
		motor[m] = rc->thr == 0 ? 0 :
			motor_out((float)rc->thr + mix[m][0] * out[0] + mix[m][1] * out[1] + mix[m][2] * out[2]);
	}
}

/*----------------------------------------------------------------------------
 * Q16.16 fixed point (acro)
 *--------------------------------------------------------------------------*/
#define Q16(x)				((q16_t)((x) * 65536.0f + ((x) >= 0 ? 0.5f : -0.5f)))

static q16_t qmul(q16_t a, q16_t b) {
	int64_t p = ((int64_t)a * b) >> 16;

	return p > INT32_MAX ? INT32_MAX : p < INT32_MIN ? INT32_MIN : (q16_t)p;
}

static q16_t qclamp(q16_t v, q16_t lim) {
	return v > lim ? lim : v < -lim ? -lim : v;
}

void fc_q_init(fc_q_t* s) {
	memset(s, 0, sizeof(*s));
	for (int a = 0; a < 3; a++) {
		s->rate[a].kp = Q16(pid_gains[a][0]);
		s->rate[a].ki = Q16(pid_gains[a][1] * FC_DT);	/* ki * dt folded in */
		s->rate[a].kd = Q16(pid_gains[a][2] * FC_LOOP_HZ);	/* kd / dt folded in */
	}
}

static q16_t pid_q(fc_pid_q_t* p, q16_t sp, q16_t meas) {
	q16_t err = sp - meas;
	q16_t d;

	p->i = qclamp(p->i + qmul(p->ki, err), Q16(I_LIMIT));
	d = -qmul(p->kd, meas - p->prev_meas);
	p->prev_meas = meas;
	p->d_lpf += qmul(Q16(DTERM_ALPHA), d - p->d_lpf);
	return qclamp(qmul(p->kp, err) + p->i + p->d_lpf, Q16(PID_LIMIT));
}

void fc_q_step(fc_q_t* s, const fc_imu_t* imu, const fc_rc_t* rc, uint16_t motor[4]) {
	q16_t out[3];
	const int16_t* stick = &rc->roll;

	for (int a = 0; a < 3; a++) {
		/* raw * 0.0010642 rad/s in Q16 = raw * 69.746 = (raw * 17855) >> 8 */
		q16_t g = ((int32_t)imu->gyro[a] * 17855) >> 8;

		s->gyro[a] += qmul(Q16(GYRO_ALPHA), g - s->gyro[a]);
		out[a] = pid_q(&s->rate[a], (q16_t)stick[a] * Q16(STICK_RATE), s->gyro[a]);
	}
	for (int m = 0; m < 4; m++) {
		int32_t v = ((int32_t)rc->thr << 16) + mix[m][0] * out[0] + mix[m][1] * out[1] + mix[m][2] * out[2];

		v = (v + 0x8000) >> 16;
		motor[m] = rc->thr == 0 ? 0 : (uint16_t)(v < 0 ? 0 : v > FC_MOTOR_MAX ? FC_MOTOR_MAX : v);
	}
}
