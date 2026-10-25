#include <string.h>

#include "fc_loop.h"

void fc_loop_init(fc_loop_t* f, const fc_orient_t* orient, uint16_t limit) {
	memset(f, 0, sizeof(*f));
	fc_f_init(&f->ctrl);
	f->orient = *orient;
	f->limit = limit > FC_MOTOR_MAX ? FC_MOTOR_MAX : limit;
	f->state = FC_CALIBRATING;
}

void fc_loop_cmd(fc_loop_t* f, const rc_cmd_t* cmd, uint8_t armed, uint32_t now_ms) {
	f->cmd = *cmd;
	f->armed = armed;
	f->cmd_ms = now_ms;
}

void fc_loop_recalibrate(fc_loop_t* f) {
	f->calibrated = 0;
	f->cal_n = 0;
}

const char* fc_state_str(uint8_t state) {
	switch (state) {
	case FC_OFF: return "off";
	case FC_CALIBRATING: return "calibrating gyro";
	case FC_RUN: return "RUN";
	case FC_STOP_FAILSAFE: return "stopped: failsafe";
	case FC_STOP_IMU: return "stopped: IMU";
	default: return "?";
	}
}

static int16_t pick(const int16_t* v, const fc_orient_t* o, int i) {
	int32_t x = v[o->map[i]] * o->sign[i];

	return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
}

static uint8_t near(int32_t a, int32_t b, int32_t tol) {
	return a - b <= tol && b - a <= tol;
}

/* gyro bias: average of FC_CAL_SAMPLES samples while gyro and accelerometer
 * stay near their first sample; a move starts it again, and so does an
 * average too large to be a bias */
static void calibrate(fc_loop_t* f, const int16_t* g, const int16_t* acc) {
	if (f->cal_n == 0) {
		memset(f->cal_sum, 0, sizeof(f->cal_sum));
		memcpy(f->cal_first, g, sizeof(f->cal_first));
		memcpy(f->cal_acc_first, acc, sizeof(f->cal_acc_first));
	}
	for (int a = 0; a < 3; a++) {
		if (!near(g[a], f->cal_first[a], FC_CAL_STILL) || !near(acc[a], f->cal_acc_first[a], FC_CAL_ACC_STILL)) {
			f->cal_n = 0;
			return;
		}
	}
	for (int a = 0; a < 3; a++) {
		f->cal_sum[a] += g[a];
	}
	if (++f->cal_n >= FC_CAL_SAMPLES) {
		int16_t b[3];

		f->cal_n = 0;
		for (int a = 0; a < 3; a++) {
			b[a] = (int16_t)(f->cal_sum[a] / (int32_t)FC_CAL_SAMPLES);
			if (!near(b[a], 0, FC_CAL_BIAS_MAX)) {
				return;
			}
		}
		memcpy(f->bias, b, sizeof(f->bias));
		f->calibrated = 1;
	}
}

void fc_loop_step(fc_loop_t* f, const int16_t gyro[3], const int16_t acc[3], uint8_t imu_ok,
				  uint32_t now_ms, uint16_t motor[4]) {
	fc_rc_t rc;
	uint16_t m[4];
	uint8_t fresh;

	f->ticks++;
	if (!imu_ok) {
		if (f->imu_fail < 255) {
			f->imu_fail++;
		}
	}
	else {
		int16_t g[3];

		f->imu_fail = 0;
		for (int a = 0; a < 3; a++) {
			g[a] = pick(gyro, &f->orient, a);
			f->imu.acc[a] = pick(acc, &f->orient, a);
		}
		/* calibration only while disarmed: an armed board may be moving */
		if (!f->calibrated && !f->armed) {
			calibrate(f, g, f->imu.acc);
		}
		for (int a = 0; a < 3; a++) {
			int32_t v = g[a] - f->bias[a];

			f->imu.gyro[a] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
		}
	}

	rc.roll = f->cmd.roll;
	rc.pitch = f->cmd.pitch;
	rc.yaw = f->cmd.yaw;
	rc.thr = f->cmd.thr;
	/* the estimate keeps running while disarmed, so it has settled at arming */
	fc_f_step(&f->ctrl, &f->imu, &rc, (uint8_t)((f->cmd.flags & RC_F_ANGLE) != 0), m);

	fresh = (uint8_t)(f->cmd_ms != 0 && (uint32_t)(now_ms - f->cmd_ms) < RC_FAILSAFE_MS);
	if (!f->calibrated) {
		f->state = FC_CALIBRATING;
	}
	else if (!f->armed) {
		f->state = FC_OFF;
	}
	else if (!fresh) {
		f->state = FC_STOP_FAILSAFE;
	}
	else if (f->imu_fail >= FC_IMU_FAIL_MAX) {
		f->state = FC_STOP_IMU;
	}
	else {
		f->state = FC_RUN;
	}

	if (f->state != FC_RUN || rc.thr == 0) {
		/* no I wind-up on the ground, no D kick at take-off */
		fc_f_reset_pid(&f->ctrl);
		memset(m, 0, sizeof(m));
	}
	for (int k = 0; k < 4; k++) {
		f->motor[k] = (uint16_t)((uint32_t)m[k] * f->limit / FC_MOTOR_MAX);
		motor[k] = f->motor[k];
	}
}
