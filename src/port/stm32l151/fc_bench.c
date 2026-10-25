/**
 ******************************************************************************
 * @brief:  "fcb" shell command, step P0 of docs/drone-ak-mcu.md: can this
 *          STM32L151 (Cortex-M3 32 MHz, no FPU) run a 1 kHz flight control
 *          loop? Built only with -DAPP_FC_BENCH (env kit_tools).
 *
 *    fcb                 cost of one control step (src/fc), DWT cycle counter:
 *                        float angle mode, float acro, Q16 acro
 *    fcb imu             find an IMU on I2C1 (J7/J9), time 14-byte reads
 *    fcb jit <s> [load]  TIM6 interrupt at 1 kHz, top priority, for s seconds
 *                        while the demo keeps running: how far each period
 *                        strays from 1000 us. "load" also runs the float angle
 *                        step inside the interrupt, like a real FC. Returns at
 *                        once; "fcb jit" alone prints the result.
 *
 *  The TIM6 interrupt is the "hard real-time layer" of the plan: it does not
 *  post messages and touches only its own counters.
 ******************************************************************************
**/

#if defined(APP_FC_BENCH)

#include <string.h>

#include "stm32l1xx.h"
#include "stm32l1xx_conf.h"

#include "hal.h"
#include "xprintf.h"
#include "kit.h"
#include "fc_ctrl.h"

#define CPU_HZ				(32000000UL)
#define CYC_PER_US			(CPU_HZ / 1000000UL)
#define LOOP_CYC			(CPU_HZ / FC_LOOP_HZ)
#define CPU_RUNS			(500)		/* per variant: ~0.5 s for all three, well below the 3 s hog limit */
#define IMU_READS			(200)
#define SAMPLES				(16)		/* 12 B each: RAM of kit_tools is tight */

static fc_imu_t samples[SAMPLES];
static uint8_t samples_ready;

/* one float state for both "fcb" and the interrupt of "fcb jit": they never
 * run at the same time (cmd_cpu refuses while the interrupt runs) */
static fc_f_t f_state;
static fc_q_t q_state;

typedef struct {
	uint32_t min, max, sum, n;
} stat_t;

static void stat_reset(stat_t* s) {
	s->min = 0xFFFFFFFFUL;
	s->max = 0;
	s->sum = 0;
	s->n = 0;
}

static void stat_add(stat_t* s, uint32_t v) {
	s->min = v < s->min ? v : s->min;
	s->max = v > s->max ? v : s->max;
	s->sum += v;
	s->n++;
}

static void dwt_on(void) {
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* cycles as microseconds with one decimal */
static void print_us(uint32_t cyc) {
	uint32_t t = cyc * 10U / CYC_PER_US;

	xprintf("%u.%u", t / 10U, t % 10U);
}

/* flight-like input: noise on a slow triangle, no libm */
static void make_samples(void) {
	uint32_t x = 0x2545F491UL;

	for (int i = 0; i < SAMPLES; i++) {
		int32_t tri = (i < SAMPLES / 2 ? i : SAMPLES - i) * 400 - 1600;

		for (int a = 0; a < 3; a++) {
			x = x * 1103515245UL + 12345UL;
			samples[i].gyro[a] = (int16_t)(tri * (a + 1) + (int32_t)((x >> 16) & 0x1FF) - 256);
			x = x * 1103515245UL + 12345UL;
			samples[i].acc[a] = (int16_t)((a == 2 ? 4096 : tri / 4) + (int32_t)((x >> 16) & 0x7F) - 64);
		}
	}
	samples_ready = 1;
}

/*----------------------------------------------------------------------------
 * fcb imu
 *--------------------------------------------------------------------------*/
static const char* imu_name(uint8_t who, uint8_t* kind) {
	*kind = 1;
	switch (who) {
	case 0x68: return "MPU-6050";
	case 0x70: return "MPU-6500";
	case 0x71: return "MPU-9250";
	case 0x73: return "MPU-9255";
	case 0x98: return "ICM-20689";
	case 0x12: return "ICM-20602";
	case 0x47: *kind = 2; return "ICM-42688-P";
	default: *kind = 0; return "unknown";
	}
}

static void cmd_imu(void) {
	uint8_t addr = 0, who = 0, kind, reg, buf[14];
	stat_t st;

	if (kit_i2c_probe(0x68)) {
		addr = 0x68;
	}
	else if (kit_i2c_probe(0x69)) {
		addr = 0x69;
	}
	if (!addr) {
		xprintf("no IMU at 0x68 / 0x69 on I2C1 (J7 or J9: SCL PB6, SDA PB7)\n");
		return;
	}
	(void)kit_i2c_read(addr, 0x75, &who, 1);
	xprintf("IMU at 0x%02X, WHO_AM_I 0x%02X: %s\n", addr, who, imu_name(who, &kind));
	if (kind == 1) {
		(void)kit_i2c_write(addr, 0x6B, 0x00);		/* PWR_MGMT_1: wake up */
		hal_delay_ms(5);
		(void)kit_i2c_write(addr, 0x1A, 0x01);		/* CONFIG: DLPF 184 Hz */
		(void)kit_i2c_write(addr, 0x1B, 0x18);		/* GYRO_CONFIG: 2000 dps */
		(void)kit_i2c_write(addr, 0x1C, 0x10);		/* ACCEL_CONFIG: 8 g */
		reg = 0x3B;									/* ACCEL_XOUT_H .. GYRO_ZOUT_L, 14 bytes */
	}
	else if (kind == 2) {
		(void)kit_i2c_write(addr, 0x4E, 0x0F);		/* PWR_MGMT0: gyro + accel low noise */
		reg = 0x1D;									/* TEMP_DATA1 .. GYRO_DATA_Z0, 14 bytes */
	}
	else {
		reg = 0x3B;
	}
	hal_delay_ms(50);

	stat_reset(&st);
	for (uint32_t i = 0; i < IMU_READS; i++) {
		uint32_t t0 = DWT->CYCCNT;

		if (!kit_i2c_read(addr, reg, buf, sizeof(buf))) {
			xprintf("read %u failed\n", i);
			return;
		}
		stat_add(&st, DWT->CYCCNT - t0);
	}
	xprintf("14-byte read, %u times, bit-banged I2C of the kit: min ", IMU_READS);
	print_us(st.min);
	xprintf(" avg ");
	print_us(st.sum / st.n);
	xprintf(" max ");
	print_us(st.max);
	xprintf(" us\n");
	/* address + register + address + 14 bytes, 9 clocks each, plus start/stop */
	xprintf("hardware I2C at 400 kHz would take about %u us; SPI at 8 MHz about 16 us\n", (17U * 9U + 4U) * 10U / 4U);
	if (kind == 1) {
		xprintf("last: acc %d %d %d  gyro %d %d %d\n",
				(int16_t)(buf[0] << 8 | buf[1]), (int16_t)(buf[2] << 8 | buf[3]), (int16_t)(buf[4] << 8 | buf[5]),
				(int16_t)(buf[8] << 8 | buf[9]), (int16_t)(buf[10] << 8 | buf[11]), (int16_t)(buf[12] << 8 | buf[13]));
	}
	else if (kind == 2) {
		xprintf("last: acc %d %d %d  gyro %d %d %d\n",
				(int16_t)(buf[2] << 8 | buf[3]), (int16_t)(buf[4] << 8 | buf[5]), (int16_t)(buf[6] << 8 | buf[7]),
				(int16_t)(buf[8] << 8 | buf[9]), (int16_t)(buf[10] << 8 | buf[11]), (int16_t)(buf[12] << 8 | buf[13]));
	}
}

/*----------------------------------------------------------------------------
 * fcb jit: 1 kHz interrupt
 *--------------------------------------------------------------------------*/
static volatile struct {
	uint32_t last;
	uint32_t n;				/* interrupts so far */
	uint32_t target;
	uint32_t pmin, pmax;	/* period, cycles */
	uint32_t over10, over50, over100;	/* periods off by more than 10 / 50 / 100 us */
	uint32_t isr_max;		/* cycles spent in the interrupt (load) */
	uint8_t load;
	uint8_t running;
	uint8_t done;
} jit;

void tim6_irq_handler(void) {
	uint32_t t = DWT->CYCCNT;

	TIM6->SR = 0;
	if (jit.n) {
		uint32_t p = t - jit.last;
		uint32_t dev = p > LOOP_CYC ? p - LOOP_CYC : LOOP_CYC - p;

		jit.pmin = p < jit.pmin ? p : jit.pmin;
		jit.pmax = p > jit.pmax ? p : jit.pmax;
		if (dev > 10U * CYC_PER_US) {
			jit.over10++;
		}
		if (dev > 50U * CYC_PER_US) {
			jit.over50++;
		}
		if (dev > 100U * CYC_PER_US) {
			jit.over100++;
		}
	}
	jit.last = t;
	jit.n++;
	if (jit.load) {
		fc_rc_t rc = { 0, 0, 0, 450 };
		uint16_t mo[4];
		uint32_t d;

		fc_f_step(&f_state, &samples[jit.n % SAMPLES], &rc, 1, mo);
		d = DWT->CYCCNT - t;
		jit.isr_max = d > jit.isr_max ? d : jit.isr_max;
	}
	if (jit.n > jit.target) {
		TIM6->CR1 = 0;
		TIM6->DIER = 0;
		jit.running = 0;
		jit.done = 1;
	}
}

static void jit_start(uint32_t seconds, uint8_t load) {
	NVIC_DisableIRQ(TIM6_IRQn);
	memset((void*)&jit, 0, sizeof(jit));
	jit.pmin = 0xFFFFFFFFUL;
	jit.target = seconds * FC_LOOP_HZ;
	jit.load = load;
	fc_f_init(&f_state);
	jit.running = 1;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
	TIM6->CR1 = 0;
	TIM6->PSC = (uint16_t)(SystemCoreClock / 1000000UL - 1);	/* 1 MHz, like TIM3 in kit.c */
	TIM6->ARR = 1000000UL / FC_LOOP_HZ - 1;
	TIM6->EGR = TIM_EGR_UG;
	TIM6->SR = 0;
	TIM6->DIER = TIM_DIER_UIE;
	/* above everything else, SysTick and UARTs included; only code with
	 * interrupts masked (kernel critical sections) can delay it */
	NVIC_SetPriority(TIM6_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
	NVIC_ClearPendingIRQ(TIM6_IRQn);
	NVIC_EnableIRQ(TIM6_IRQn);
	TIM6->CR1 = TIM_CR1_CEN;
	xprintf("1 kHz interrupt for %u s%s; keep the demo busy (a game, ui stream), then: fcb jit\n",
			seconds, load ? ", float angle step inside" : "");
}

static void jit_report(void) {
	if (jit.running) {
		xprintf("running: %u of %u periods\n", jit.n, jit.target);
		return;
	}
	if (!jit.done) {
		xprintf("usage: fcb jit <seconds> [load], then fcb jit\n");
		return;
	}
	xprintf("%u periods of 1000 us%s: shortest ", jit.n - 1, jit.load ? " (with load)" : "");
	print_us(jit.pmin);
	xprintf(" longest ");
	print_us(jit.pmax);
	xprintf(" us\n  off by > 10 us: %u, > 50 us: %u, > 100 us: %u\n", jit.over10, jit.over50, jit.over100);
	if (jit.load) {
		xprintf("  longest interrupt (step included): ");
		print_us(jit.isr_max);
		xprintf(" us = %u%% of the period\n", jit.isr_max * 100U / LOOP_CYC);
	}
	xprintf("budget: P0 passes if longest - 1000 <= 50 us\n");
}

/*----------------------------------------------------------------------------
 * fcb: cost of one step
 *--------------------------------------------------------------------------*/

static void cpu_run(const char* name, uint8_t kind) {
	fc_rc_t rc = { 120, -80, 40, 450 };
	uint16_t mo[4];
	stat_t st;

	stat_reset(&st);
	fc_f_init(&f_state);
	fc_q_init(&q_state);
	for (uint32_t i = 0; i < CPU_RUNS; i++) {
		const fc_imu_t* m = &samples[i % SAMPLES];
		uint32_t t0 = DWT->CYCCNT;

		if (kind == 0) {
			fc_f_step(&f_state, m, &rc, 1, mo);
		}
		else if (kind == 1) {
			fc_f_step(&f_state, m, &rc, 0, mo);
		}
		else {
			fc_q_step(&q_state, m, &rc, mo);
		}
		stat_add(&st, DWT->CYCCNT - t0);
		/* a step far slower than planned must not reach the 3 s hog limit
		 * of task_system: stop this variant after ~0.6 s */
		if (st.sum > CPU_HZ / 10U * 6U) {
			xprintf("  (stopped after %u runs: too slow)\n", st.n);
			break;
		}
	}
	xprintf("  %-18s min %6u avg %6u max %6u cycles = ", name, st.min, st.sum / st.n, st.max);
	print_us(st.sum / st.n);
	xprintf(" us avg, %u%% of a 1 ms loop\n", (st.sum / st.n) * 100U / LOOP_CYC);
}

static void cmd_cpu(void) {
	if (jit.running) {
		xprintf("fcb jit is running, wait for it\n");
		return;
	}
	xprintf("one control step, %u runs each (min = no interrupt in between):\n", CPU_RUNS);
	cpu_run("float angle", 0);
	cpu_run("float acro", 1);
	cpu_run("Q16 acro", 2);
	xprintf("budget: P0 passes if float angle avg <= 40%% (12800 cycles)\n");
}

/*----------------------------------------------------------------------------
 * shell
 *--------------------------------------------------------------------------*/
static uint32_t parse_u(const char** p) {
	uint32_t v = 0;

	while (**p == ' ') {
		(*p)++;
	}
	while (**p >= '0' && **p <= '9') {
		v = v * 10U + (uint32_t)(**p - '0');
		(*p)++;
	}
	return v;
}

void cmd_fcb(const char* args) {
	dwt_on();
	if (!samples_ready) {
		make_samples();
	}
	while (*args == ' ') {
		args++;
	}
	if (args[0] == 0 || strncmp(args, "cpu", 3) == 0) {
		cmd_cpu();
	}
	else if (strncmp(args, "imu", 3) == 0) {
		cmd_imu();
	}
	else if (strncmp(args, "jit", 3) == 0) {
		const char* p = args + 3;
		uint32_t s = parse_u(&p);

		while (*p == ' ') {
			p++;
		}
		if (s == 0) {
			jit_report();
		}
		else if (jit.running) {
			xprintf("already running\n");
		}
		else {
			jit_start(s > 600 ? 600 : s, strncmp(p, "load", 4) == 0);
		}
	}
	else {
		xprintf("usage: fcb | fcb imu | fcb jit <seconds> [load] | fcb jit\n");
	}
}

#endif /* APP_FC_BENCH */
