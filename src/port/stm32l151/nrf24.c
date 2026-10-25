/**
 ******************************************************************************
 * @brief:  rc_radio.h for STM32L151: nRF24L01+ on SPI1 (shared with the SPI
 *          NOR, same mode 0 and clock), Enhanced ShockBurst. Built only with
 *          -DAPP_RC_TX or -DAPP_RC_RX.
 *
 *  Pins: CE PA8, CSN PA4 (J6 of the AK Base Kit 3.0). The kit 2.1 has the
 *  module on the board with CSN on PB9: build with -DRC_NRF_CSN_PB9.
 *  The IRQ pin is not used: the tasks poll STATUS.
 ******************************************************************************
**/

#if defined(APP_RC_TX) || defined(APP_RC_RX)

#include "stm32l1xx.h"
#include "stm32l1xx_conf.h"

#include "hal.h"
#include "rc_radio.h"

#define CE_PORT				GPIOA
#define CE_PIN				GPIO_Pin_8
#if defined(RC_NRF_CSN_PB9)
#define CSN_PORT			GPIOB
#define CSN_PIN				GPIO_Pin_9
#else
#define CSN_PORT			GPIOA
#define CSN_PIN				GPIO_Pin_4
#endif

/* registers */
#define R_CONFIG			(0x00)
#define R_EN_AA				(0x01)
#define R_EN_RXADDR			(0x02)
#define R_SETUP_AW			(0x03)
#define R_SETUP_RETR		(0x04)
#define R_RF_CH				(0x05)
#define R_RF_SETUP			(0x06)
#define R_STATUS			(0x07)
#define R_RX_ADDR_P0		(0x0A)
#define R_TX_ADDR			(0x10)
#define R_FIFO_STATUS		(0x17)
#define R_DYNPD				(0x1C)
#define R_FEATURE			(0x1D)

/* commands */
#define C_R_REG				(0x00)
#define C_W_REG				(0x20)
#define C_R_RX_PAYLOAD		(0x61)
#define C_W_TX_PAYLOAD		(0xA0)
#define C_W_ACK_PAYLOAD		(0xA8)		/* | pipe */
#define C_R_RX_PL_WID		(0x60)
#define C_FLUSH_TX			(0xE1)
#define C_FLUSH_RX			(0xE2)
#define C_ACTIVATE			(0x50)		/* nRF24L01 (non +) and some clones: unlock FEATURE */
#define C_NOP				(0xFF)

#define ST_RX_DR			(0x40)
#define ST_TX_DS			(0x20)
#define ST_MAX_RT			(0x10)
#define FIFO_RX_EMPTY		(0x01)
#define FIFO_TX_FULL		(0x20)

#define CFG_EN_CRC			(0x08)
#define CFG_CRCO			(0x04)		/* 2-byte CRC */
#define CFG_PWR_UP			(0x02)
#define CFG_PRIM_RX			(0x01)

static uint8_t is_prx;

static uint8_t xfer(uint8_t b) {
	uint32_t spin = 20000;

	while (!(SPI1->SR & SPI_SR_TXE) && --spin) {
	}
	SPI1->DR = b;
	while (!(SPI1->SR & SPI_SR_RXNE) && --spin) {
	}
	return (uint8_t)SPI1->DR;
}

static inline void csn(uint8_t high) {
	if (high) {
		CSN_PORT->BSRRL = CSN_PIN;
	}
	else {
		CSN_PORT->BSRRH = CSN_PIN;
	}
}

static inline void ce(uint8_t high) {
	if (high) {
		CE_PORT->BSRRL = CE_PIN;
	}
	else {
		CE_PORT->BSRRH = CE_PIN;
	}
}

/* one command; returns STATUS */
static uint8_t cmd(uint8_t c, const uint8_t* tx, uint8_t* rx, uint8_t len) {
	uint8_t st;

	csn(0);
	st = xfer(c);
	while (len--) {
		uint8_t v = xfer(tx ? *tx++ : C_NOP);

		if (rx) {
			*rx++ = v;
		}
	}
	csn(1);
	return st;
}

static void wreg(uint8_t r, uint8_t v) {
	cmd((uint8_t)(C_W_REG | r), &v, 0, 1);
}

static uint8_t rreg(uint8_t r) {
	uint8_t v = 0;

	cmd((uint8_t)(C_R_REG | r), 0, &v, 1);
	return v;
}

static void wait_us(uint32_t us) {
	/* ~4 cycles per turn at 32 MHz: close enough for the 10..150 us the chip asks */
	for (volatile uint32_t i = 0; i < us * 8U; i++) {
	}
}

uint8_t rc_radio_init(void) {
	GPIO_InitTypeDef gpio;

	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | RCC_AHBPeriph_GPIOB, ENABLE);
	csn(1);
	ce(0);
	GPIO_StructInit(&gpio);
	gpio.GPIO_Mode = GPIO_Mode_OUT;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_Speed = GPIO_Speed_10MHz;
	gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
	gpio.GPIO_Pin = CE_PIN;
	GPIO_Init(CE_PORT, &gpio);
	gpio.GPIO_Pin = CSN_PIN;
	GPIO_Init(CSN_PORT, &gpio);
	hal_delay_ms(5);							/* power on reset of the chip: 100 ms from cold, the kit is long up */

	/* a register that holds what we write: SETUP_AW 1 (3 bytes) then 3 (5 bytes) */
	wreg(R_SETUP_AW, 0x01);
	if (rreg(R_SETUP_AW) != 0x01) {
		return 0;
	}
	wreg(R_SETUP_AW, 0x03);
	return rreg(R_SETUP_AW) == 0x03;
}

void rc_radio_config(const uint8_t addr[5], uint8_t ch, uint8_t prx) {
	ce(0);
	wreg(R_CONFIG, CFG_EN_CRC | CFG_CRCO);		/* powered down while configuring */
	wreg(R_EN_AA, 0x01);						/* auto ACK on pipe 0 */
	wreg(R_EN_RXADDR, 0x01);
	wreg(R_SETUP_AW, 0x03);
	/* ACK payloads of 6..32 bytes need 500 us at 1 Mbps; 2 retries: a packet
	 * is decided within ~1.8 ms, inside the 4 ms period */
	wreg(R_SETUP_RETR, (1 << 4) | 2);
	wreg(R_RF_CH, ch & 0x7F);
	wreg(R_RF_SETUP, 0x06);						/* 1 Mbps, 0 dBm */
	cmd(C_W_REG | R_RX_ADDR_P0, addr, 0, 5);
	cmd(C_W_REG | R_TX_ADDR, addr, 0, 5);
	wreg(R_FEATURE, 0x06);						/* EN_DPL | EN_ACK_PAY */
	if (rreg(R_FEATURE) != 0x06) {
		uint8_t key = 0x73;

		cmd(C_ACTIVATE, &key, 0, 1);
		wreg(R_FEATURE, 0x06);
	}
	wreg(R_DYNPD, 0x01);
	cmd(C_FLUSH_TX, 0, 0, 0);
	cmd(C_FLUSH_RX, 0, 0, 0);
	wreg(R_STATUS, ST_RX_DR | ST_TX_DS | ST_MAX_RT);
	is_prx = prx;
	wreg(R_CONFIG, (uint8_t)(CFG_EN_CRC | CFG_CRCO | CFG_PWR_UP | (prx ? CFG_PRIM_RX : 0)));
	hal_delay_ms(2);							/* power up: 1.5 ms */
	if (prx) {
		ce(1);									/* listening from now on */
	}
}

void rc_radio_channel(uint8_t ch) {
	ce(0);
	wreg(R_RF_CH, ch & 0x7F);
	if (is_prx) {
		ce(1);									/* RX settles in 130 us */
	}
}

void rc_radio_off(void) {
	ce(0);
	wreg(R_CONFIG, CFG_EN_CRC | CFG_CRCO);
}

void rc_radio_send(const uint8_t* data, uint8_t len) {
	ce(0);
	wreg(R_STATUS, ST_RX_DR | ST_TX_DS | ST_MAX_RT);
	cmd(C_FLUSH_TX, 0, 0, 0);
	cmd(C_W_TX_PAYLOAD, data, 0, len);
	/* CE stays high until rc_radio_tx_done() saw the end: retransmissions
	 * and the ACK window happen without us */
	ce(1);
	wait_us(15);
}

int8_t rc_radio_tx_done(uint8_t* ack, uint8_t* ack_len) {
	uint8_t st = cmd(C_NOP, 0, 0, 0);

	*ack_len = 0;
	if (st & ST_TX_DS) {
		ce(0);
		if (st & ST_RX_DR) {
			uint8_t n = 0;

			cmd(C_R_RX_PL_WID, 0, &n, 1);
			if (n > 0 && n <= RC_RADIO_MAX) {
				cmd(C_R_RX_PAYLOAD, 0, ack, n);
				*ack_len = n;
			}
			else {
				cmd(C_FLUSH_RX, 0, 0, 0);
			}
		}
		wreg(R_STATUS, ST_RX_DR | ST_TX_DS | ST_MAX_RT);
		return RC_TX_ACKED;
	}
	if (st & ST_MAX_RT) {
		ce(0);
		cmd(C_FLUSH_TX, 0, 0, 0);
		wreg(R_STATUS, ST_MAX_RT);
		return RC_TX_LOST;
	}
	return RC_TX_BUSY;
}

uint8_t rc_radio_recv(uint8_t* data) {
	uint8_t n = 0;

	if (rreg(R_FIFO_STATUS) & FIFO_RX_EMPTY) {
		return 0;
	}
	cmd(C_R_RX_PL_WID, 0, &n, 1);
	if (n == 0 || n > RC_RADIO_MAX) {
		cmd(C_FLUSH_RX, 0, 0, 0);
		wreg(R_STATUS, ST_RX_DR);
		return 0;
	}
	cmd(C_R_RX_PAYLOAD, 0, data, n);
	wreg(R_STATUS, ST_RX_DR);
	return n;
}

void rc_radio_ack(const uint8_t* data, uint8_t len) {
	/* one reply waiting at a time: the newest telemetry, not a queue */
	cmd(C_FLUSH_TX, 0, 0, 0);
	cmd(C_W_ACK_PAYLOAD | 0, data, 0, len);
}

#endif /* APP_RC_TX || APP_RC_RX */
