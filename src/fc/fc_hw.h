/**
 ******************************************************************************
 * @brief:  Hardware side of the flight controller (fc build), implemented by
 *          the port (port/stm32l151/fc_hw.c): motor PWM, the IMU, and the
 *          control interrupt that runs fc_loop_step() FC_LOOP_HZ times a
 *          second above every other interrupt (the hard real-time layer of
 *          docs/drone-ak-mcu.md). Tasks talk to it only through these calls.
 *
 *  AK Base Kit 3.0, bench wiring (P2):
 *    motors   M1 rear right PA0 (TIM2_CH1, J13)   M2 front right PB10 (TIM2_CH3, J4)
 *             M3 rear left  PB11 (TIM2_CH4, J4)   M4 front left  PB9 (TIM4_CH4, J13)
 *             16 kHz PWM, active high, to the gates of logic-level N MOSFETs
 *    IMU      MPU-6050 family on I2C1 (J7 / J9), address 0x68 or 0x69
 ******************************************************************************
**/

#ifndef __FC_HW_H__
#define __FC_HW_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#include "fc_loop.h"

typedef struct {
	uint8_t state;			/* FC_* */
	uint8_t calibrated;
	uint8_t imu_ok;
	uint16_t limit;
	uint16_t motor[4];
	fc_imu_t imu;			/* body frame, bias removed */
	float roll, pitch;		/* estimate, sin of the angle */
	uint32_t ticks;
	uint32_t isr_max_cyc;	/* longest interrupt, cycles */
	uint32_t isr_last_cyc;
	uint32_t overruns;		/* periods that started before the last one ended */
	uint8_t test_motor;		/* 1..4 while a motor test runs */
} fc_hw_status_t;

/* PWM off, IMU found and set up, interrupt started; returns 1 if the IMU answered */
extern uint8_t fc_hw_start(uint16_t limit);
/* the radio command and the time of the packet it came in */
extern void fc_hw_cmd(const rc_cmd_t* cmd, uint8_t armed, uint32_t packet_ms);
extern void fc_hw_status(fc_hw_status_t* s);
extern void fc_hw_limit(uint16_t permille);
extern void fc_hw_recalibrate(void);
/* bench: one motor at permille for ms, only while disarmed; 0 if refused */
extern uint8_t fc_hw_motor_test(uint8_t motor, uint16_t permille, uint16_t ms);
/* called often from a task: stops the motors if the interrupt stopped */
extern void fc_hw_watch(void);

#ifdef __cplusplus
}
#endif

#endif /* __FC_HW_H__ */
