/**
 ******************************************************************************
 * @brief:  fc_hw.h for STM32L151 on the AK Base Kit 3.0 (fc build, P2 of
 *          docs/drone-ak-mcu.md). Built only with -DAPP_FC.
 *
 *  TIM6 interrupt at FC_LOOP_HZ, priority 0: read the IMU (bit-banged I2C1
 *  of kit.c, ~0.75 ms for 14 bytes; nothing else uses I2C1 in this build),
 *  fc_loop_step(), write the four PWM compare registers. It posts no
 *  message and shares only fc_loop_t with the tasks, whose calls mask
 *  interrupts around the few bytes they touch.
 ******************************************************************************
**/

#if defined(APP_FC)

#include <string.h>

#include "stm32l1xx.h"
#include "stm32l1xx_conf.h"

#include "hal.h"
#include "kit.h"
#include "fc_hw.h"

#define PWM_HZ				(16000U)
#define PWM_TOP				(32000000U / PWM_HZ)		/* 2000 steps */
#define IMU_REG_DATA		(0x3B)						/* ACCEL_XOUT_H .. GYRO_ZOUT_L */
#define WATCH_MS			(20)						/* no interrupt for this long: motors off */

static fc_loop_t loop;
static uint8_t imu_addr;
static volatile uint32_t isr_max, isr_last, overruns;
static volatile uint8_t test_motor;		/* 1..4 */
static volatile uint16_t test_pwm;
static volatile uint32_t test_until_ms;
static uint32_t watch_ticks, watch_ms;

static const fc_orient_t orient = FC_ORIENT_DEFAULT;

/*----------------------------------------------------------------------------
 * motors
 *--------------------------------------------------------------------------*/
static void pwm_pin(GPIO_TypeDef* port, uint16_t pin, uint8_t src, uint8_t af) {
	GPIO_InitTypeDef gpio;

	port->BSRRH = pin;
	GPIO_PinAFConfig(port, src, af);
	GPIO_StructInit(&gpio);
	gpio.GPIO_Mode = GPIO_Mode_AF;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_PuPd = GPIO_PuPd_DOWN;		/* gate low while the timer is not driving */
	gpio.GPIO_Speed = GPIO_Speed_2MHz;
	gpio.GPIO_Pin = pin;
	GPIO_Init(port, &gpio);
}

static void pwm_init(void) {
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | RCC_AHBPeriph_GPIOB, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM4, ENABLE);

	/* TIM2: CH1 PA0, CH3 PB10, CH4 PB11; TIM4: CH4 PB9. PWM mode 1,
	 * preloaded compare, output high while counter < CCR */
	TIM2->CR1 = 0;
	TIM2->PSC = 0;
	TIM2->ARR = PWM_TOP - 1;
	TIM2->CCR1 = TIM2->CCR3 = TIM2->CCR4 = 0;
	TIM2->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE;
	TIM2->CCMR2 = TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3PE
				| TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4PE;
	TIM2->CCER = TIM_CCER_CC1E | TIM_CCER_CC3E | TIM_CCER_CC4E;
	TIM2->EGR = TIM_EGR_UG;
	TIM2->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

	TIM4->CR1 = 0;
	TIM4->PSC = 0;
	TIM4->ARR = PWM_TOP - 1;
	TIM4->CCR4 = 0;
	TIM4->CCMR2 = TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4PE;
	TIM4->CCER = TIM_CCER_CC4E;
	TIM4->EGR = TIM_EGR_UG;
	TIM4->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

	pwm_pin(GPIOA, GPIO_Pin_0, GPIO_PinSource0, GPIO_AF_TIM2);
	pwm_pin(GPIOB, GPIO_Pin_10, GPIO_PinSource10, GPIO_AF_TIM2);
	pwm_pin(GPIOB, GPIO_Pin_11, GPIO_PinSource11, GPIO_AF_TIM2);
	pwm_pin(GPIOB, GPIO_Pin_9, GPIO_PinSource9, GPIO_AF_TIM4);
}

/* permille -> compare; motor order of the mixer: rear right, front right,
 * rear left, front left */
static void pwm_set(const uint16_t* m) {
	TIM2->CCR1 = (uint32_t)m[0] * PWM_TOP / FC_MOTOR_MAX;
	TIM2->CCR3 = (uint32_t)m[1] * PWM_TOP / FC_MOTOR_MAX;
	TIM2->CCR4 = (uint32_t)m[2] * PWM_TOP / FC_MOTOR_MAX;
	TIM4->CCR4 = (uint32_t)m[3] * PWM_TOP / FC_MOTOR_MAX;
}

static void pwm_off(void) {
	TIM2->CCR1 = TIM2->CCR3 = TIM2->CCR4 = 0;
	TIM4->CCR4 = 0;
}

/*----------------------------------------------------------------------------
 * IMU: MPU-6050 family
 *--------------------------------------------------------------------------*/
static uint8_t imu_init(void) {
	uint8_t who = 0;

	imu_addr = kit_i2c_probe(0x68) ? 0x68 : kit_i2c_probe(0x69) ? 0x69 : 0;
	if (!imu_addr || !kit_i2c_read(imu_addr, 0x75, &who, 1)) {
		return 0;
	}
	(void)kit_i2c_write(imu_addr, 0x6B, 0x01);		/* PWR_MGMT_1: awake, clock from the X gyro */
	hal_delay_ms(10);
	(void)kit_i2c_write(imu_addr, 0x19, 0x00);		/* SMPLRT_DIV: 1 kHz */
	(void)kit_i2c_write(imu_addr, 0x1A, 0x01);		/* CONFIG: DLPF 184 Hz (~2 ms) */
	(void)kit_i2c_write(imu_addr, 0x1B, 0x18);		/* GYRO_CONFIG: 2000 dps = 16.4 LSB per deg/s */
	(void)kit_i2c_write(imu_addr, 0x1C, 0x10);		/* ACCEL_CONFIG: 8 g = 4096 LSB per g */
	return 1;
}

static uint8_t imu_read(int16_t* g, int16_t* a) {
	uint8_t b[14];

	if (!imu_addr || !kit_i2c_read(imu_addr, IMU_REG_DATA, b, sizeof(b))) {
		return 0;
	}
	for (int i = 0; i < 3; i++) {
		a[i] = (int16_t)((b[2 * i] << 8) | b[2 * i + 1]);
		g[i] = (int16_t)((b[8 + 2 * i] << 8) | b[9 + 2 * i]);
	}
	return 1;
}

/*----------------------------------------------------------------------------
 * the control interrupt
 *--------------------------------------------------------------------------*/
void tim6_irq_handler(void) {
	uint32_t t0 = DWT->CYCCNT;
	int16_t g[3] = { 0, 0, 0 }, a[3] = { 0, 0, 0 };
	uint16_t m[4];
	uint8_t ok;
	uint32_t now = hal_millis();

	TIM6->SR = 0;
	ok = imu_read(g, a);
	fc_loop_step(&loop, g, a, ok, now, m);
	if (test_motor && loop.state == FC_OFF && !loop.armed && (int32_t)(now - test_until_ms) < 0) {
		memset(m, 0, sizeof(m));
		m[test_motor - 1] = test_pwm;
	}
	else {
		test_motor = 0;
	}
	pwm_set(m);
	isr_last = DWT->CYCCNT - t0;
	if (isr_last > isr_max) {
		isr_max = isr_last;
	}
	if (TIM6->SR & TIM_SR_UIF) {
		overruns++;			/* the next period is already due: this one took too long */
	}
}

uint8_t fc_hw_start(uint16_t limit) {
	uint8_t imu;

	pwm_init();
	fc_loop_init(&loop, &orient, limit);
	imu = imu_init();
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
	TIM6->CR1 = 0;
	TIM6->PSC = (uint16_t)(SystemCoreClock / 1000000UL - 1);	/* 1 MHz */
	TIM6->ARR = 1000000UL / FC_LOOP_HZ - 1;
	TIM6->EGR = TIM_EGR_UG;
	TIM6->SR = 0;
	TIM6->DIER = TIM_DIER_UIE;
	NVIC_SetPriority(TIM6_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
	NVIC_ClearPendingIRQ(TIM6_IRQn);
	NVIC_EnableIRQ(TIM6_IRQn);
	TIM6->CR1 = TIM_CR1_CEN;
	watch_ms = hal_millis();
	return imu;
}

void fc_hw_cmd(const rc_cmd_t* cmd, uint8_t armed, uint32_t packet_ms) {
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	fc_loop_cmd(&loop, cmd, armed, packet_ms);
	__set_PRIMASK(primask);
}

void fc_hw_status(fc_hw_status_t* s) {
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	s->state = loop.state;
	s->calibrated = loop.calibrated;
	s->imu_ok = (uint8_t)(imu_addr && loop.imu_fail == 0);
	s->limit = loop.limit;
	memcpy(s->motor, loop.motor, sizeof(s->motor));
	s->imu = loop.imu;
	s->roll = loop.ctrl.roll;
	s->pitch = loop.ctrl.pitch;
	s->ticks = loop.ticks;
	s->isr_max_cyc = isr_max;
	s->isr_last_cyc = isr_last;
	s->overruns = overruns;
	s->test_motor = test_motor;
	__set_PRIMASK(primask);
}

void fc_hw_limit(uint16_t permille) {
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	loop.limit = permille > FC_MOTOR_MAX ? FC_MOTOR_MAX : permille;
	__set_PRIMASK(primask);
}

void fc_hw_recalibrate(void) {
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	fc_loop_recalibrate(&loop);
	isr_max = 0;
	__set_PRIMASK(primask);
}

uint8_t fc_hw_motor_test(uint8_t motor, uint16_t permille, uint16_t ms) {
	uint32_t primask;

	if (motor < 1 || motor > 4 || loop.armed || loop.state != FC_OFF) {
		return 0;
	}
	primask = __get_PRIMASK();
	__disable_irq();
	test_pwm = permille > loop.limit ? loop.limit : permille;
	test_until_ms = hal_millis() + ms;
	test_motor = motor;
	__set_PRIMASK(primask);
	return 1;
}

void fc_hw_watch(void) {
	uint32_t now = hal_millis();
	uint32_t t = loop.ticks;

	if (t != watch_ticks) {
		watch_ticks = t;
		watch_ms = now;
	}
	else if (now - watch_ms > WATCH_MS) {
		/* the interrupt is not running: the timers would keep the last duty */
		pwm_off();
	}
}

#endif /* APP_FC */
