/**
 ******************************************************************************
 * @brief:  Flight control core for a small brushed quadcopter (P0 of
 *          docs/drone-ak-mcu.md): gyro filter, Mahony attitude estimate,
 *          angle and rate PIDs, quad-X mixer. Chip independent, no libm.
 *
 *  Two implementations of the same rate loop, to measure what a Cortex-M3
 *  without FPU can afford at 1 kHz:
 *    fc_f_step()    float (soft float on the STM32L151), acro or angle mode
 *    fc_q_step()    Q16.16 fixed point, acro (rate) mode only
 *
 *  Units: gyro raw at +-2000 dps (16.4 LSB per deg/s), accelerometer raw at
 *  any range, sticks -500..500, throttle 0..1000, motors 0..1000 (permille of
 *  full PWM). Motor order and signs as the Betaflight QUADX mixer:
 *    0 rear right, 1 front right, 2 rear left, 3 front left (props in).
 *  Positive roll = right side down, positive pitch = nose up, positive yaw =
 *  nose right (clockwise from above).
 ******************************************************************************
**/

#ifndef __FC_CTRL_H__
#define __FC_CTRL_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#ifndef FC_LOOP_HZ
#define FC_LOOP_HZ			(1000)	/* the fc build runs 500: the IMU is read over bit-banged I2C */
#endif
#define FC_MOTOR_MAX		(1000)

typedef struct {
	int16_t gyro[3];		/* roll, pitch, yaw rate */
	int16_t acc[3];			/* x forward, y left, z up (1 g at rest, any scale) */
} fc_imu_t;

typedef struct {
	int16_t roll, pitch, yaw;	/* -500..500 */
	uint16_t thr;				/* 0..1000 */
} fc_rc_t;

/* float */
typedef struct {
	float kp, ki, kd;
	float i;
	float prev_meas;
	float d_lpf;
} fc_pid_f_t;

typedef struct {
	float q0, q1, q2, q3;		/* attitude quaternion */
	float ix, iy, iz;			/* Mahony integral feedback */
	float gyro[3];				/* filtered gyro, rad/s */
	fc_pid_f_t rate[3];
	float angle_kp;				/* angle error (rad) -> rate setpoint (rad/s) */
	float roll, pitch;			/* last estimate, rad (small angle) */
} fc_f_t;

/* Q16.16 fixed point */
typedef int32_t q16_t;

typedef struct {
	q16_t kp, ki, kd;
	q16_t i;
	q16_t prev_meas;
	q16_t d_lpf;
} fc_pid_q_t;

typedef struct {
	q16_t gyro[3];
	fc_pid_q_t rate[3];
} fc_q_t;

extern void fc_f_init(fc_f_t* s);
/* one step of 1 / FC_LOOP_HZ s; angle = 1: sticks are angles (self level), 0: rates (acro) */
extern void fc_f_step(fc_f_t* s, const fc_imu_t* imu, const fc_rc_t* rc, uint8_t angle, uint16_t motor[4]);

/* integrators and D history to zero, attitude kept (disarmed, throttle 0) */
extern void fc_f_reset_pid(fc_f_t* s);

extern void fc_q_init(fc_q_t* s);
extern void fc_q_step(fc_q_t* s, const fc_imu_t* imu, const fc_rc_t* rc, uint16_t motor[4]);

/* 1 / sqrt(x) without libm (bit trick + two Newton steps, ~1e-6 relative) */
extern float fc_inv_sqrt(float x);

#ifdef __cplusplus
}
#endif

#endif /* __FC_CTRL_H__ */
