/**
 ******************************************************************************
 * @brief:  One period of the flight controller, step P2 of
 *          docs/drone-ak-mcu.md. Called from the control interrupt (the hard
 *          real-time layer, port/stm32l151/fc_hw.c); chip independent so that
 *          test_fc flies it in a simulated quadcopter.
 *
 *  Around fc_ctrl it adds what a real board needs:
 *    - IMU axes to the body frame (fc_orient_t)
 *    - gyro bias measured at start, while the board is still
 *    - the gate: motors run only when the radio says armed, the last command
 *      is fresh (< RC_FAILSAFE_MS), the IMU answers and the gyro is calibrated;
 *      the check is here, in the interrupt, not only in a task
 *    - a motor limit (permille of full PWM) for bench tests
 *  Commands come from a task (fc_loop_cmd) with interrupts masked around the
 *  call: the interrupt then always reads a whole command.
 ******************************************************************************
**/

#ifndef __FC_LOOP_H__
#define __FC_LOOP_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#include "fc_ctrl.h"
#include "rc_link.h"

#define FC_CAL_SAMPLES		(FC_LOOP_HZ)		/* 1 s of gyro at rest */
#define FC_CAL_STILL		(160)				/* gyro counts (~10 deg/s) from the first sample: moving */
#define FC_CAL_ACC_STILL	(300)				/* accelerometer counts from the first sample: tilting
												 * (a steady turn reads a steady gyro, but turns gravity) */
#define FC_CAL_BIAS_MAX		(500)				/* a bias above ~30 deg/s is no bias: a turn, or a bad chip */
#define FC_IMU_FAIL_MAX		(10)				/* missed IMU reads in a row before the motors stop */
#define FC_LIMIT_BENCH		(300)				/* default motor limit, permille: no props, still be careful */

/* body axis i takes IMU axis map[i] times sign[i] (+1 / -1) */
typedef struct {
	uint8_t map[3];
	int8_t sign[3];
} fc_orient_t;

/* MPU-6050 module flat, chip up, its X arrow to the front: the same frame */
#define FC_ORIENT_DEFAULT	{ { 0, 1, 2 }, { 1, 1, 1 } }

enum {
	FC_OFF = 0,				/* motors off: disarmed */
	FC_CALIBRATING,			/* measuring the gyro bias, keep the board still */
	FC_RUN,					/* motors follow the controller */
	FC_STOP_FAILSAFE,		/* armed, but the last command is too old */
	FC_STOP_IMU,			/* armed, but the IMU stopped answering */
};

typedef struct {
	fc_f_t ctrl;
	fc_orient_t orient;
	/* gyro bias */
	int32_t cal_sum[3];
	int16_t cal_first[3];
	int16_t cal_acc_first[3];
	uint16_t cal_n;
	int16_t bias[3];
	uint8_t calibrated;
	/* from the radio */
	rc_cmd_t cmd;
	uint8_t armed;
	uint32_t cmd_ms;
	/* output */
	uint16_t limit;			/* permille of full PWM */
	uint16_t motor[4];		/* what the motors get, permille */
	uint8_t state;			/* FC_* */
	uint8_t imu_fail;		/* reads missed in a row */
	fc_imu_t imu;			/* last sample, body frame, bias removed */
	uint32_t ticks;
} fc_loop_t;

extern void fc_loop_init(fc_loop_t* f, const fc_orient_t* orient, uint16_t limit);
/* from the radio task, interrupts masked around it */
extern void fc_loop_cmd(fc_loop_t* f, const rc_cmd_t* cmd, uint8_t armed, uint32_t now_ms);
/* one period: raw IMU (imu_ok = 0 if the read failed), motors out (permille) */
extern void fc_loop_step(fc_loop_t* f, const int16_t gyro[3], const int16_t acc[3], uint8_t imu_ok,
						 uint32_t now_ms, uint16_t motor[4]);
extern void fc_loop_recalibrate(fc_loop_t* f);
extern const char* fc_state_str(uint8_t state);

#ifdef __cplusplus
}
#endif

#endif /* __FC_LOOP_H__ */
