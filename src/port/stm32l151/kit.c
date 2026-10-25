/**
 ******************************************************************************
 * @brief:  demo/kit.h for the AK Base Kit 3I0 (STM32L151CB).
 *
 *   OLED SSD1309 128x64   I2C 0x3C, bit-banged: SCL PB13, SDA PB12, RES PA15
 *   buttons               PB3, PC13, PB4 - to GND, internal pull-up
 *   buzzer (passive)      PB0 = TIM3_CH3
 *   RTC PCF85063          I2C 0x51, bit-banged on the I2C1 pins: SCL PB6, SDA PB7
 *   SHT45                 I2C 0x44, same pins as the RTC
 *   media store           SPI NOR below the OTA staging area (0 .. PORT_STAGING_ADDR)
 *
 *  PA15, PB3 and PB4 are JTAG pins after reset; as plain GPIO they only cost
 *  JTAG, SWD (PA13/PA14) keeps working.
 ******************************************************************************
**/

#include "stm32l1xx.h"
#include "stm32l1xx_conf.h"

#include "hal.h"
#include "kit.h"
#include "port_cfg.h"
#include "spi_nor.h"

/*----------------------------------------------------------------------------
 * bit-banged I2C master (open drain pins, about 400 kHz at 32 MHz)
 *--------------------------------------------------------------------------*/
typedef struct {
	GPIO_TypeDef* port;
	uint16_t scl;
	uint16_t sda;
} i2c_bus_t;

static const i2c_bus_t bus_lcd = { GPIOB, GPIO_Pin_13, GPIO_Pin_12 };
static const i2c_bus_t bus_rtc = { GPIOB, GPIO_Pin_6, GPIO_Pin_7 };

#define LCD_ADDR		(0x3C)
#ifndef KIT_LCD_COL_OFFSET
#define KIT_LCD_COL_OFFSET	(0)
#endif
#define RTC_ADDR		(0x51)
#define SHT_ADDR		(0x44)

static inline void i2c_delay(void) {
	for (volatile uint32_t i = 0; i < 3; i++) {
	}
}

static inline void scl_hi(const i2c_bus_t* b) { b->port->BSRRL = b->scl; i2c_delay(); }
static inline void scl_lo(const i2c_bus_t* b) { b->port->BSRRH = b->scl; i2c_delay(); }
static inline void sda_hi(const i2c_bus_t* b) { b->port->BSRRL = b->sda; }
static inline void sda_lo(const i2c_bus_t* b) { b->port->BSRRH = b->sda; }

static void i2c_pins(const i2c_bus_t* b) {
	GPIO_InitTypeDef gpio;

	b->port->BSRRL = b->scl | b->sda;		/* released = high before becoming outputs */
	GPIO_StructInit(&gpio);
	gpio.GPIO_Mode = GPIO_Mode_OUT;
	gpio.GPIO_OType = GPIO_OType_OD;
	gpio.GPIO_PuPd = GPIO_PuPd_UP;
	gpio.GPIO_Speed = GPIO_Speed_10MHz;
	gpio.GPIO_Pin = b->scl | b->sda;
	GPIO_Init(b->port, &gpio);
}

static void i2c_start(const i2c_bus_t* b) {
	sda_hi(b);
	scl_hi(b);
	sda_lo(b);
	i2c_delay();
	scl_lo(b);
}

static void i2c_stop(const i2c_bus_t* b) {
	sda_lo(b);
	scl_hi(b);
	sda_hi(b);
	i2c_delay();
}

/* returns 1 if the device acknowledged */
static uint8_t i2c_tx(const i2c_bus_t* b, uint8_t v) {
	uint8_t ack;

	for (uint8_t i = 0; i < 8; i++, v <<= 1) {
		if (v & 0x80) {
			sda_hi(b);
		}
		else {
			sda_lo(b);
		}
		scl_hi(b);
		scl_lo(b);
	}
	sda_hi(b);
	scl_hi(b);
	ack = !(b->port->IDR & b->sda);
	scl_lo(b);
	return ack;
}

static uint8_t i2c_rx(const i2c_bus_t* b, uint8_t send_ack) {
	uint8_t v = 0;

	sda_hi(b);
	for (uint8_t i = 0; i < 8; i++) {
		scl_hi(b);
		v = (uint8_t)((v << 1) | ((b->port->IDR & b->sda) ? 1 : 0));
		scl_lo(b);
	}
	if (send_ack) {
		sda_lo(b);
	}
	scl_hi(b);
	scl_lo(b);
	sda_hi(b);
	return v;
}

/*----------------------------------------------------------------------------
 * OLED SSD1309
 *--------------------------------------------------------------------------*/
/* init sequence of U8g2's SSD1309 128x64 "noname0" (the type this module
 * runs with in the kit's test firmware), page addressing mode */
static const uint8_t lcd_init_seq[] = {
	0xAE,			/* display off */
	0xD5, 0xA0,		/* clock divide / oscillator */
	0x40,			/* start line 0 */
	0x20, 0x02,		/* page addressing mode */
	0xA1,			/* segment remap */
	0xC8,			/* scan direction */
	0xDA, 0x12,		/* COM pins */
	0x81, 0x6F,		/* contrast */
#if defined(KIT_LCD_SSD1306)
	/* 0.96" SSD1306 module (AK Base Kit 2.1): the panel voltage comes from
	 * the charge pump of the chip, which is off after reset - without this
	 * the display answers on the bus and stays dark */
	0xD9, 0xF1,		/* pre-charge, for the internal pump */
	0x8D, 0x14,		/* charge pump on */
#else
	0xD9, 0xD3,		/* pre-charge */
#endif
	0xDB, 0x20,		/* VCOMH */
	0x2E,			/* scroll off */
	0xA4,			/* show RAM */
	0xA6,			/* not inverted */
	0xAF,			/* display on */
};

static uint8_t lcd_cmds(const uint8_t* cmd, uint8_t n) {
	uint8_t ok;

	i2c_start(&bus_lcd);
	ok = i2c_tx(&bus_lcd, LCD_ADDR << 1);
	ok &= i2c_tx(&bus_lcd, 0x00);		/* control byte: commands follow */
	while (n--) {
		ok &= i2c_tx(&bus_lcd, *cmd++);
	}
	i2c_stop(&bus_lcd);
	return ok;
}

/* does address a acknowledge? bus 0 = display (SCL PB13, SDA PB12), 1 = the
 * same pins swapped, 2 = I2C1 header (SCL PB6, SDA PB7), 3 = that swapped */
uint8_t kit_bus_probe(uint8_t bus, uint8_t a) {
	static const i2c_bus_t lcd_swapped = { GPIOB, GPIO_Pin_12, GPIO_Pin_13 };
	static const i2c_bus_t rtc_swapped = { GPIOB, GPIO_Pin_7, GPIO_Pin_6 };
	const i2c_bus_t* b = bus == 0 ? &bus_lcd : bus == 1 ? &lcd_swapped : bus == 2 ? &bus_rtc : &rtc_swapped;
	uint8_t ok;

	i2c_start(b);
	ok = i2c_tx(b, (uint8_t)(a << 1));
	i2c_stop(b);
	return ok;
}

uint8_t kit_lcd_write(uint8_t page, uint8_t x, const uint8_t* data, uint8_t n) {
	/* page, first column. KIT_LCD_COL_OFFSET = 2 for an SH1106 (132 columns
	 * of RAM, the glass shows 2..129); 0 for an SSD1306 / SSD1309 */
	uint8_t col = (uint8_t)(KIT_LCD_COL_OFFSET + x);
	uint8_t pos[3] = { (uint8_t)(0xB0 | (page & 7)), (uint8_t)(col & 0x0F), (uint8_t)(0x10 | (col >> 4)) };
	uint8_t ok;

	if (!lcd_cmds(pos, sizeof(pos))) {
		return 0;
	}
	i2c_start(&bus_lcd);
	ok = i2c_tx(&bus_lcd, LCD_ADDR << 1);
	ok &= i2c_tx(&bus_lcd, 0x40);		/* control byte: data follows */
	while (n--) {
		ok &= i2c_tx(&bus_lcd, *data++);
	}
	i2c_stop(&bus_lcd);
	return ok;
}

/*----------------------------------------------------------------------------
 * buttons, buzzer
 *--------------------------------------------------------------------------*/
uint8_t kit_buttons(void) {
	uint8_t m = 0;

	if (!(GPIOB->IDR & GPIO_Pin_3)) {
		m |= KIT_BTN_1;
	}
	if (!(GPIOC->IDR & GPIO_Pin_13)) {
		m |= KIT_BTN_2;
	}
	if (!(GPIOB->IDR & GPIO_Pin_4)) {
		m |= KIT_BTN_3;
	}
	return m;
}

/* TIM3 runs from PCLK1 = 32 MHz, prescaled to 1 MHz: ARR = period in us */
void kit_buzzer(uint16_t freq_hz) {
	uint32_t period;

	if (freq_hz < 20) {
		TIM3->CR1 &= (uint16_t)~TIM_CR1_CEN;
		TIM3->CCR3 = 0;
		TIM3->EGR = TIM_EGR_UG;			/* output low, not wherever the wave stopped */
		return;
	}
	period = 1000000UL / freq_hz;
	TIM3->ARR = (uint16_t)(period - 1);
	TIM3->CCR3 = (uint16_t)(period / 2);
	TIM3->EGR = TIM_EGR_UG;
	TIM3->CR1 |= TIM_CR1_CEN;
}

/*----------------------------------------------------------------------------
 * RTC PCF85063: seconds, minutes, hours in BCD from register 0x04
 *--------------------------------------------------------------------------*/
static uint8_t bcd2dec(uint8_t v) {
	return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

static uint8_t dec2bcd(uint8_t v) {
	return (uint8_t)(((v / 10) << 4) | (v % 10));
}

uint8_t kit_rtc_get(uint8_t* hh, uint8_t* mm, uint8_t* ss) {
	uint8_t ok;
	uint8_t s, m, h;

	i2c_start(&bus_rtc);
	ok = i2c_tx(&bus_rtc, RTC_ADDR << 1);
	ok &= i2c_tx(&bus_rtc, 0x04);
	i2c_start(&bus_rtc);
	ok &= i2c_tx(&bus_rtc, (RTC_ADDR << 1) | 1);
	s = i2c_rx(&bus_rtc, 1);
	m = i2c_rx(&bus_rtc, 1);
	h = i2c_rx(&bus_rtc, 0);
	i2c_stop(&bus_rtc);

	if (!ok) {
		return 0;
	}
	*ss = bcd2dec(s & 0x7F);
	*mm = bcd2dec(m & 0x7F);
	*hh = bcd2dec(h & 0x3F);
	return (*ss < 60 && *mm < 60 && *hh < 24);
}

uint8_t kit_rtc_set(uint8_t hh, uint8_t mm, uint8_t ss) {
	uint8_t ok;

	i2c_start(&bus_rtc);
	ok = i2c_tx(&bus_rtc, RTC_ADDR << 1);
	ok &= i2c_tx(&bus_rtc, 0x04);
	ok &= i2c_tx(&bus_rtc, dec2bcd(ss));	/* writing seconds also clears the "clock stopped" flag */
	ok &= i2c_tx(&bus_rtc, dec2bcd(mm));
	ok &= i2c_tx(&bus_rtc, dec2bcd(hh));
	i2c_stop(&bus_rtc);
	return ok;
}

/*----------------------------------------------------------------------------
 * init
 *--------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------
 * SHT45: command 0xFD = measure with high precision (8.3 ms), then read
 * T(2) crc RH(2) crc
 *--------------------------------------------------------------------------*/
static uint8_t sht_crc(const uint8_t* d) {
	uint8_t crc = 0xFF;

	for (uint8_t i = 0; i < 2; i++) {
		crc ^= d[i];
		for (uint8_t b = 0; b < 8; b++) {
			crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
		}
	}
	return crc;
}

uint8_t kit_sht_start(void) {
	uint8_t ok;

	i2c_start(&bus_rtc);
	ok = i2c_tx(&bus_rtc, SHT_ADDR << 1);
	ok &= i2c_tx(&bus_rtc, 0xFD);
	i2c_stop(&bus_rtc);
	return ok;
}

uint8_t kit_sht_read(int16_t* t10, uint16_t* rh10) {
	uint8_t d[6];
	uint8_t ok;
	int32_t rh;

	i2c_start(&bus_rtc);
	ok = i2c_tx(&bus_rtc, (SHT_ADDR << 1) | 1);
	if (!ok) {						/* not there, or still measuring */
		i2c_stop(&bus_rtc);
		return 0;
	}
	for (uint8_t i = 0; i < 6; i++) {
		d[i] = i2c_rx(&bus_rtc, i < 5);
	}
	i2c_stop(&bus_rtc);
	if (sht_crc(&d[0]) != d[2] || sht_crc(&d[3]) != d[5]) {
		return 0;
	}
	/* data sheet: T = -45 + 175 * raw / 65535, RH = -6 + 125 * raw / 65535 */
	*t10 = (int16_t)(-450 + (int32_t)(1750UL * (((uint32_t)d[0] << 8) | d[1]) / 65535UL));
	rh = -60 + (int32_t)(1250UL * (((uint32_t)d[3] << 8) | d[4]) / 65535UL);
	*rh10 = (uint16_t)(rh < 0 ? 0 : rh > 1000 ? 1000 : rh);
	return 1;
}

/*----------------------------------------------------------------------------
 * raw access to the I2C1 header (SCL PB6, SDA PB7), for the "i2c" shell command
 *--------------------------------------------------------------------------*/
uint8_t kit_i2c_probe(uint8_t addr) {
	uint8_t ok;

	i2c_start(&bus_rtc);
	ok = i2c_tx(&bus_rtc, (uint8_t)(addr << 1));
	i2c_stop(&bus_rtc);
	return ok;
}

uint8_t kit_i2c_read(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len) {
	uint8_t ok;

	i2c_start(&bus_rtc);
	ok = i2c_tx(&bus_rtc, (uint8_t)(addr << 1));
	ok &= i2c_tx(&bus_rtc, reg);
	i2c_start(&bus_rtc);
	ok &= i2c_tx(&bus_rtc, (uint8_t)((addr << 1) | 1));
	if (!ok) {
		i2c_stop(&bus_rtc);
		return 0;
	}
	for (uint8_t i = 0; i < len; i++) {
		buf[i] = i2c_rx(&bus_rtc, (uint8_t)(i + 1 < len));
	}
	i2c_stop(&bus_rtc);
	return 1;
}

uint8_t kit_i2c_write(uint8_t addr, uint8_t reg, uint8_t val) {
	uint8_t ok;

	i2c_start(&bus_rtc);
	ok = i2c_tx(&bus_rtc, (uint8_t)(addr << 1));
	ok &= i2c_tx(&bus_rtc, reg);
	ok &= i2c_tx(&bus_rtc, val);
	i2c_stop(&bus_rtc);
	return ok;
}

/* releases both lines and samples them n times: how often each one was high
 * and how many times it changed. A poor man's logic probe. */
void kit_i2c_watch(uint32_t n, uint32_t* scl_high, uint32_t* sda_high, uint32_t* scl_edges, uint32_t* sda_edges) {
	uint32_t last, now;

	sda_hi(&bus_rtc);
	scl_hi(&bus_rtc);
	*scl_high = *sda_high = *scl_edges = *sda_edges = 0;
	last = bus_rtc.port->IDR;
	for (uint32_t i = 0; i < n; i++) {
		now = bus_rtc.port->IDR;
		if (now & bus_rtc.scl) {
			(*scl_high)++;
		}
		if (now & bus_rtc.sda) {
			(*sda_high)++;
		}
		if ((now ^ last) & bus_rtc.scl) {
			(*scl_edges)++;
		}
		if ((now ^ last) & bus_rtc.sda) {
			(*sda_edges)++;
		}
		last = now;
	}
}

#if defined(APP_KIT_SPI_SNIFF)
/*----------------------------------------------------------------------------
 * SPI sniffer: SPI1 as a receive-only slave on the J6 pins (NSS PA4, SCK PA5,
 * MOSI PA7); DMA1 channel 2 moves every byte into sniff_buf. A falling edge of
 * NSS (EXTI4) records the byte offset and a time stamp = start of a frame.
 * MISO stays a plain input: the kit never drives a line of the board under
 * test. The NOR driver gets SPI1 back in kit_spi_sniff_stop().
 *--------------------------------------------------------------------------*/
static uint8_t sniff_buf[KIT_SPI_SNIFF_BUF];
static uint16_t sniff_off[KIT_SPI_SNIFF_FRAMES];
static uint16_t sniff_t[KIT_SPI_SNIFF_FRAMES];	/* DWT cycle count >> 8 */
static volatile uint16_t sniff_nframes;
static uint16_t sniff_bytes;			/* latched at stop */
static uint8_t sniff_on;

/* bytes in the buffer. After a rewind CNDTR is reloaded with what is left of
 * the buffer from the new write position, so this stays true. */
static uint16_t sniff_count(void) {
	return (uint16_t)(KIT_SPI_SNIFF_BUF - DMA1_Channel2->CNDTR);
}

static uint16_t sniff_trig;				/* first byte that starts the capture, > 0xFF = at once */
static uint8_t sniff_armed;				/* 1 = still waiting for sniff_trig */

/* Falling edge of NSS only, and as short as it gets: frames follow each other
 * within 15 us on a busy bus, so nothing here may take long (an earlier
 * version compared frames in this handler and lost the frame boundaries).
 * Until a frame starting with sniff_trig has been seen, each new frame
 * overwrites the previous one. */
void exti4_irq_handler(void) {
	uint16_t n = sniff_nframes;
	uint16_t cnt = sniff_count();

	EXTI->PR = EXTI_PR_PR4;
	if (sniff_armed) {
		if (n && cnt > sniff_off[n - 1] && sniff_buf[sniff_off[n - 1]] == (uint8_t)sniff_trig) {
			sniff_armed = 0;
		}
		else {
			DMA1_Channel2->CCR &= (uint32_t)~DMA_CCR2_EN;
			DMA1_Channel2->CMAR = (uint32_t)sniff_buf;
			DMA1_Channel2->CNDTR = KIT_SPI_SNIFF_BUF;
			DMA1_Channel2->CCR |= DMA_CCR2_EN;
			n = 0;
			cnt = 0;
		}
	}
	if (n < KIT_SPI_SNIFF_FRAMES) {
		sniff_off[n] = cnt;
		sniff_t[n] = (uint16_t)(DWT->CYCCNT >> 8);		/* 8 us units at 32 MHz */
		sniff_nframes = (uint16_t)(n + 1);
	}
}

void kit_spi_sniff_start(uint8_t mode, uint16_t trig) {
	GPIO_InitTypeDef gpio;
	SPI_InitTypeDef spi;

	if (sniff_on) {
		kit_spi_sniff_stop();
	}
	/* DMA, EXTI and SYSCFG are driven by register here: their SPL sources are
	 * not in the build (tools/pio_mcu_base.py, port.cmake) */
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | RCC_AHBPeriph_DMA1, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1 | RCC_APB2Periph_SYSCFG, ENABLE);

	SPI_Cmd(SPI1, DISABLE);
	SPI_I2S_DeInit(SPI1);

	GPIO_PinAFConfig(GPIOA, GPIO_PinSource4, GPIO_AF_SPI1);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF_SPI1);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_SPI1);
	GPIO_StructInit(&gpio);
	gpio.GPIO_Mode = GPIO_Mode_AF;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_Speed = GPIO_Speed_40MHz;
	gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;		/* the 2.8 V board drives these; no pull into its rail */
	gpio.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
	GPIO_Init(GPIOA, &gpio);
	gpio.GPIO_PuPd = GPIO_PuPd_UP;			/* unplugged = deselected */
	gpio.GPIO_Pin = GPIO_Pin_4;
	GPIO_Init(GPIOA, &gpio);
	gpio.GPIO_Mode = GPIO_Mode_IN;
	gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
	gpio.GPIO_Pin = GPIO_Pin_6;
	GPIO_Init(GPIOA, &gpio);

	/* DMA1 channel 2 = SPI1_RX: peripheral -> memory, bytes, memory
	 * increment, one shot, highest priority */
	DMA1_Channel2->CCR = 0;
	DMA1->IFCR = DMA_IFCR_CGIF2;
	DMA1_Channel2->CPAR = (uint32_t)&SPI1->DR;
	DMA1_Channel2->CMAR = (uint32_t)sniff_buf;
	DMA1_Channel2->CNDTR = KIT_SPI_SNIFF_BUF;
	DMA1_Channel2->CCR = DMA_CCR2_MINC | DMA_CCR2_PL | DMA_CCR2_EN;

	SPI_StructInit(&spi);
	spi.SPI_Direction = SPI_Direction_2Lines_RxOnly;
	spi.SPI_Mode = SPI_Mode_Slave;
	spi.SPI_DataSize = SPI_DataSize_8b;
	spi.SPI_CPOL = (mode & 2) ? SPI_CPOL_High : SPI_CPOL_Low;
	spi.SPI_CPHA = (mode & 1) ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;
	spi.SPI_NSS = SPI_NSS_Hard;
	spi.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_Init(SPI1, &spi);
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, ENABLE);

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	sniff_nframes = 0;
	sniff_bytes = 0;
	sniff_trig = trig;
	sniff_armed = (uint8_t)(trig <= 0xFF);

	/* EXTI4 <- PA4, falling edge. Highest priority: a frame mark that comes
	 * late lands on the wrong byte. */
	SYSCFG->EXTICR[1] &= (uint32_t)~SYSCFG_EXTICR2_EXTI4;		/* PA4 */
	EXTI->RTSR &= (uint32_t)~EXTI_RTSR_TR4;
	EXTI->FTSR |= EXTI_FTSR_TR4;
	EXTI->PR = EXTI_PR_PR4;
	EXTI->IMR |= EXTI_IMR_MR4;
	NVIC_SetPriority(EXTI4_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
	NVIC_EnableIRQ(EXTI4_IRQn);

	sniff_on = 1;
	SPI_Cmd(SPI1, ENABLE);
}

void kit_spi_sniff_stop(void) {
	if (!sniff_on) {
		return;
	}
	sniff_bytes = sniff_count();
	NVIC_DisableIRQ(EXTI4_IRQn);
	EXTI->IMR &= (uint32_t)~EXTI_IMR_MR4;
	EXTI->RTSR &= (uint32_t)~EXTI_RTSR_TR4;
	EXTI->FTSR &= (uint32_t)~EXTI_FTSR_TR4;
	SPI_Cmd(SPI1, DISABLE);
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, DISABLE);
	DMA1_Channel2->CCR &= (uint32_t)~DMA_CCR2_EN;
	sniff_on = 0;
	spi_nor_init();						/* SPI1 back to the flash */
}

/* PA4, PA5, PA7 as plain inputs, sampled n times: how often each was high and
 * how many times it changed. Tells which wire is SCK (most edges), CSN (fewest)
 * and DATA before the real capture. Leaves the pins as inputs: call
 * kit_spi_sniff_start() or kit_spi_sniff_stop() afterwards. */
void kit_spi_watch(uint32_t n, uint8_t pull, uint32_t high[3], uint32_t edges[3]) {
	static const uint16_t pin[3] = { GPIO_Pin_4, GPIO_Pin_5, GPIO_Pin_7 };
	GPIO_InitTypeDef gpio;
	uint32_t last, now;

	if (sniff_on) {
		kit_spi_sniff_stop();
	}
	SPI_Cmd(SPI1, DISABLE);					/* the NOR driver is re-armed by kit_spi_sniff_stop() */
	GPIO_StructInit(&gpio);
	gpio.GPIO_Mode = GPIO_Mode_IN;
	/* a weak pull tells a wire that is driven from one that floats */
	gpio.GPIO_PuPd = pull == 1 ? GPIO_PuPd_UP : pull == 2 ? GPIO_PuPd_DOWN : GPIO_PuPd_NOPULL;
	gpio.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_7;
	GPIO_Init(GPIOA, &gpio);
	for (uint8_t k = 0; k < 3; k++) {
		high[k] = edges[k] = 0;
	}
	last = GPIOA->IDR;
	for (uint32_t i = 0; i < n; i++) {
		now = GPIOA->IDR;
		for (uint8_t k = 0; k < 3; k++) {
			if (now & pin[k]) {
				high[k]++;
			}
			if ((now ^ last) & pin[k]) {
				edges[k]++;
			}
		}
		last = now;
	}
	sniff_on = 1;							/* so that stop() restores SPI1 for the flash */
	kit_spi_sniff_stop();
	sniff_bytes = 0;						/* nothing was captured, whatever the DMA counter says */
}

uint8_t kit_spi_sniff_status(uint16_t* bytes, uint16_t* frames) {
	*bytes = sniff_on ? sniff_count() : sniff_bytes;
	*frames = sniff_nframes;
	return sniff_on;
}

uint8_t kit_spi_sniff_frame(uint16_t i, const uint8_t** data, uint16_t* len, uint32_t* dt_us) {
	uint16_t n = sniff_nframes;
	uint16_t end;

	if (i >= n) {
		return 0;
	}
	end = (uint16_t)(i + 1 < n ? sniff_off[i + 1] : (sniff_on ? sniff_count() : sniff_bytes));
	*data = &sniff_buf[sniff_off[i]];
	*len = (uint16_t)(end - sniff_off[i]);
	/* 16 bit of cycles >> 8: wraps after 0.5 s, so a longer pause reads short */
	*dt_us = i ? (uint32_t)(uint16_t)(sniff_t[i] - sniff_t[i - 1]) * 256UL / (SystemCoreClock / 1000000UL) : 0;
	return 1;
}
#endif /* APP_KIT_SPI_SNIFF */

/*----------------------------------------------------------------------------
 * media store: SPI NOR from 0 up to the OTA staging area. The port found the
 * chip at start-up (STAGING has a size only then).
 *--------------------------------------------------------------------------*/
uint32_t kit_store_size(void) {
#if PORT_STAGING_EXTERNAL
	return hal_flash_info(FLASH_PART_STAGING)->size ? PORT_STAGING_ADDR : 0;
#else
	return 0;
#endif
}

static uint8_t store_range_ok(uint32_t off, uint32_t len) {
	uint32_t size = kit_store_size();

	return off <= size && len <= size - off;
}

uint8_t kit_store_read(uint32_t off, void* buf, uint32_t len) {
	return store_range_ok(off, len) && spi_nor_read(off, buf, len) == SPI_NOR_OK;
}

uint8_t kit_store_erase(uint32_t off) {
	return (off % KIT_STORE_SECTOR) == 0 && store_range_ok(off, KIT_STORE_SECTOR)
		   && spi_nor_erase_sector(off) == SPI_NOR_OK;
}

uint8_t kit_store_write(uint32_t off, const void* data, uint32_t len) {
	return store_range_ok(off, len) && spi_nor_write(off, data, len) == SPI_NOR_OK;
}

uint8_t kit_init(void) {
	GPIO_InitTypeDef gpio;

	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | RCC_AHBPeriph_GPIOB | RCC_AHBPeriph_GPIOC, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

	/* buttons */
	GPIO_StructInit(&gpio);
	gpio.GPIO_Mode = GPIO_Mode_IN;
	gpio.GPIO_PuPd = GPIO_PuPd_UP;
	gpio.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4;
	GPIO_Init(GPIOB, &gpio);
	gpio.GPIO_Pin = GPIO_Pin_13;
	GPIO_Init(GPIOC, &gpio);

	/* buzzer: PB0 = TIM3_CH3, PWM mode 1, 1 MHz time base, stopped */
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource0, GPIO_AF_TIM3);
	GPIO_StructInit(&gpio);
	gpio.GPIO_Mode = GPIO_Mode_AF;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_PuPd = GPIO_PuPd_DOWN;
	gpio.GPIO_Speed = GPIO_Speed_2MHz;
	gpio.GPIO_Pin = GPIO_Pin_0;
	GPIO_Init(GPIOB, &gpio);
	TIM3->CR1 = 0;
	TIM3->PSC = (uint16_t)(SystemCoreClock / 1000000UL - 1);
	TIM3->CCMR2 = TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3PE;
	TIM3->CCER = TIM_CCER_CC3E;
	kit_buzzer(0);

	/* display reset: PA15 high - low - high */
	GPIO_StructInit(&gpio);
	gpio.GPIO_Mode = GPIO_Mode_OUT;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
	gpio.GPIO_Speed = GPIO_Speed_2MHz;
	gpio.GPIO_Pin = GPIO_Pin_15;
	GPIO_Init(GPIOA, &gpio);
	GPIOA->BSRRL = GPIO_Pin_15;
	hal_delay_ms(5);
	GPIOA->BSRRH = GPIO_Pin_15;
	hal_delay_ms(5);
	GPIOA->BSRRL = GPIO_Pin_15;
	hal_delay_ms(10);

	i2c_pins(&bus_lcd);
	i2c_pins(&bus_rtc);

	return lcd_cmds(lcd_init_seq, sizeof(lcd_init_seq));
}
