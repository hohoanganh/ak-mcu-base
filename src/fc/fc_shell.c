/**
 * "fc" shell command of the fc build (P2 of docs/drone-ak-mcu.md):
 *
 *   fc                 state, IMU in the body frame, attitude, motors, timing
 *   fc cal             measure the gyro bias again (keep the board still, disarmed)
 *   fc limit <n>       motor limit, permille of full PWM (bench default 300)
 *   fc motor <1-4> <n> one motor at n permille for 2 s, disarmed only: which
 *                      one turns, and which way (1 RR, 2 FR, 3 RL, 4 FL)
 */
#if defined(APP_FC)

#include <string.h>

#include "xprintf.h"
#include "fc_hw.h"

#define CYC_PER_US			(32U)

static uint32_t num(const char** p, uint8_t* ok) {
	uint32_t v = 0;

	while (**p == ' ') {
		(*p)++;
	}
	*ok = (uint8_t)(**p >= '0' && **p <= '9');
	while (**p >= '0' && **p <= '9') {
		v = v * 10U + (uint32_t)(*(*p)++ - '0');
	}
	return v;
}

/* sin of the angle (estimate) as milliradians, good to ~30 deg */
static int mrad(float s) {
	return (int)(s * 1000.0f);
}

void cmd_fc(const char* args) {
	fc_hw_status_t s;
	uint8_t ok;

	while (*args == ' ') {
		args++;
	}
	if (strncmp(args, "cal", 3) == 0) {
		fc_hw_recalibrate();
		xprintf("gyro bias: keep the board still for 1 s\n");
		return;
	}
	if (strncmp(args, "limit", 5) == 0) {
		const char* p = args + 5;
		uint32_t v = num(&p, &ok);

		if (!ok || v > 1000) {
			xprintf("usage: fc limit <0..1000>\n");
			return;
		}
		fc_hw_limit((uint16_t)v);
		xprintf("motor limit %u permille\n", v);
		return;
	}
	if (strncmp(args, "motor", 5) == 0) {
		const char* p = args + 5;
		uint32_t m = num(&p, &ok);
		uint32_t v = ok ? num(&p, &ok) : 0;

		if (!ok || m < 1 || m > 4 || v > 1000) {
			xprintf("usage: fc motor <1..4> <0..1000>  (1 rear right, 2 front right, 3 rear left, 4 front left)\n");
			return;
		}
		if (!fc_hw_motor_test((uint8_t)m, (uint16_t)v, 2000)) {
			xprintf("refused: only while disarmed and with the gyro calibrated\n");
			return;
		}
		xprintf("motor %u at %u permille (capped by the limit) for 2 s; props OFF\n", m, v);
		return;
	}
	fc_hw_status(&s);
	xprintf("fc %s, %d Hz, IMU %s, gyro %s, limit %u\n", fc_state_str(s.state), FC_LOOP_HZ,
			s.imu_ok ? "ok" : "MISSING", s.calibrated ? "calibrated" : "not calibrated", s.limit);
	xprintf("gyro %d %d %d  acc %d %d %d  (body: x front, y left, z up)\n", s.imu.gyro[0], s.imu.gyro[1],
			s.imu.gyro[2], s.imu.acc[0], s.imu.acc[1], s.imu.acc[2]);
	xprintf("attitude roll %d pitch %d mrad (right side down / nose down positive)\n", mrad(s.roll), mrad(s.pitch));
	xprintf("motors RR %u FR %u RL %u FL %u permille%s\n", s.motor[0], s.motor[1], s.motor[2], s.motor[3],
			s.test_motor ? " (motor test)" : "");
	xprintf("interrupt: last %u us, longest %u us of %u, overruns %u, periods %u\n", s.isr_last_cyc / CYC_PER_US,
			s.isr_max_cyc / CYC_PER_US, 1000000U / FC_LOOP_HZ, s.overruns, s.ticks);
	xprintf("fc cal | fc limit <n> | fc motor <1-4> <n>\n");
}

#endif /* APP_FC */
