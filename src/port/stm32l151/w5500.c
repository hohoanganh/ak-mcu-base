/**
 ******************************************************************************
 * @brief:  hal_tcp.h for the AK Base Kit with a W5500 module on the SPI
 *          header J6: SPI1 shared with the NOR flash (PA5 SCK, PA6 MISO,
 *          PA7 MOSI), chip select PA4 (J6 pin 4, see port_cfg.h).
 *
 *  The W5500 runs TCP/IP itself; this driver only moves bytes through its
 *  socket 0 with the variable-length SPI frame: [addr 16][control 8][data].
 *  Control = block select << 3 | write << 2. Blocks: common 0x00, socket 0
 *  registers 0x01, its TX buffer 0x02, its RX buffer 0x03 (W5500 datasheet
 *  chapter 3). Every wait on the chip is bounded.
 *
 *  Not yet run on a kit with a real module.
 ******************************************************************************
**/

#include <string.h>

#include "stm32l1xx.h"
#include "stm32l1xx_conf.h"

#include "hal.h"
#include "hal_tcp.h"
#include "port_cfg.h"

/* common registers */
#define W_MR			(0x0000)
#define W_GAR			(0x0001)
#define W_SUBR			(0x0005)
#define W_SHAR			(0x0009)
#define W_SIPR			(0x000F)
#define W_PHYCFGR		(0x002E)
#define W_VERSIONR		(0x0039)
#define W_VERSION		(0x04)

/* socket registers (socket 0 block) */
#define S_MR			(0x0000)
#define S_CR			(0x0001)
#define S_IR			(0x0002)
#define S_SR			(0x0003)
#define S_PORT			(0x0004)
#define S_TX_FSR		(0x0020)
#define S_TX_WR			(0x0024)
#define S_RX_RSR		(0x0026)
#define S_RX_RD			(0x0028)

#define S_MR_TCP		(0x01)
#define S_CR_OPEN		(0x01)
#define S_CR_LISTEN		(0x02)
#define S_CR_DISCON		(0x08)
#define S_CR_CLOSE		(0x10)
#define S_CR_SEND		(0x20)
#define S_CR_RECV		(0x40)
#define S_IR_SEND_OK	(0x10)
#define S_IR_TIMEOUT	(0x08)
#define S_SR_CLOSED		(0x00)
#define S_SR_INIT		(0x13)
#define S_SR_LISTEN		(0x14)
#define S_SR_ESTABLISHED (0x17)
#define S_SR_CLOSE_WAIT	(0x1C)

#define BLK_COMMON		(0x00)
#define BLK_S0_REG		(0x01)
#define BLK_S0_TX		(0x02)
#define BLK_S0_RX		(0x03)

#define XFER_SPIN_MAX	(20000U)
#define CMD_WAIT_MS		(100U)
#define SEND_WAIT_MS	(500U)

static uint8_t present;
static uint8_t listening;
static uint16_t listen_port;

/*----------------------------------------------------------------------------
 * SPI frame
 *--------------------------------------------------------------------------*/
static inline void cs_low(void) {
	PORT_W5500_CS_PORT->BSRRH = PORT_W5500_CS_PIN;
}

static inline void cs_high(void) {
	PORT_W5500_CS_PORT->BSRRL = PORT_W5500_CS_PIN;
}

static uint8_t xfer(uint8_t b) {
	uint32_t spin = XFER_SPIN_MAX;

	while (!(SPI1->SR & SPI_SR_TXE)) {
		if (--spin == 0) {
			return 0xFF;
		}
	}
	SPI1->DR = b;
	while (!(SPI1->SR & SPI_SR_RXNE)) {
		if (--spin == 0) {
			return 0xFF;
		}
	}
	return (uint8_t)SPI1->DR;
}

static void frame_begin(uint16_t addr, uint8_t block, uint8_t write) {
	cs_low();
	xfer((uint8_t)(addr >> 8));
	xfer((uint8_t)addr);
	xfer((uint8_t)((block << 3) | (write ? 0x04 : 0x00)));
}

static void w_read(uint16_t addr, uint8_t block, uint8_t* buf, uint32_t len) {
	frame_begin(addr, block, 0);
	while (len--) {
		*buf++ = xfer(0x00);
	}
	cs_high();
}

static void w_write(uint16_t addr, uint8_t block, const uint8_t* data, uint32_t len) {
	frame_begin(addr, block, 1);
	while (len--) {
		xfer(*data++);
	}
	cs_high();
}

static uint8_t reg8(uint16_t addr, uint8_t block) {
	uint8_t v;

	w_read(addr, block, &v, 1);
	return v;
}

static void set8(uint16_t addr, uint8_t block, uint8_t v) {
	w_write(addr, block, &v, 1);
}

static uint16_t reg16(uint16_t addr, uint8_t block) {
	uint8_t v[2];

	w_read(addr, block, v, 2);
	return (uint16_t)((v[0] << 8) | v[1]);
}

/* the free / received size counters move while the chip works: read until
 * two reads agree (datasheet) */
static uint16_t reg16_stable(uint16_t addr) {
	uint16_t a = reg16(addr, BLK_S0_REG);
	uint16_t b;

	for (int i = 0; i < 8; i++) {
		b = reg16(addr, BLK_S0_REG);
		if (a == b) {
			break;
		}
		a = b;
	}
	return a;
}

static void set16(uint16_t addr, uint8_t block, uint16_t v) {
	uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };

	w_write(addr, block, b, 2);
}

/* socket command: the chip clears Sn_CR when it has taken it */
static int command(uint8_t cmd) {
	uint32_t t0 = hal_millis();

	set8(S_CR, BLK_S0_REG, cmd);
	while (reg8(S_CR, BLK_S0_REG) != 0) {
		if (hal_millis() - t0 > CMD_WAIT_MS) {
			return -1;
		}
	}
	return 0;
}

/*----------------------------------------------------------------------------
 * SPI1 + chip select: the NOR driver may already own the bus
 *--------------------------------------------------------------------------*/
static void spi_init(void) {
	GPIO_InitTypeDef gpio;
	SPI_InitTypeDef spi;

	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | PORT_W5500_CS_CLK, ENABLE);
	GPIO_StructInit(&gpio);
	gpio.GPIO_Mode = GPIO_Mode_OUT;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_Speed = GPIO_Speed_10MHz;
	gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
	gpio.GPIO_Pin = PORT_W5500_CS_PIN;
	cs_high();
	GPIO_Init(PORT_W5500_CS_PORT, &gpio);

	if (SPI1->CR1 & SPI_CR1_SPE) {
		return;		/* spi_nor_init() set the bus up (same mode 0, 4 MHz) */
	}
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF_SPI1);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_SPI1);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_SPI1);
	gpio.GPIO_Mode = GPIO_Mode_AF;
	gpio.GPIO_Speed = GPIO_Speed_40MHz;
	gpio.GPIO_PuPd = GPIO_PuPd_UP;
	gpio.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_Init(GPIOA, &gpio);

	SPI_StructInit(&spi);
	spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	spi.SPI_Mode = SPI_Mode_Master;
	spi.SPI_DataSize = SPI_DataSize_8b;
	spi.SPI_CPOL = SPI_CPOL_Low;
	spi.SPI_CPHA = SPI_CPHA_1Edge;
	spi.SPI_NSS = SPI_NSS_Soft;
	spi.SPI_BaudRatePrescaler = PORT_NOR_SPI_PRESCALER;
	spi.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_Init(SPI1, &spi);
	SPI_Cmd(SPI1, ENABLE);
}

/*----------------------------------------------------------------------------
 * hal_tcp
 *--------------------------------------------------------------------------*/
int hal_tcp_init(const hal_net_cfg_t* cfg) {
	uint32_t t0;

	present = 0;
	listening = 0;
	spi_init();

	/* software reset, then the chip must say it is a W5500 */
	set8(W_MR, BLK_COMMON, 0x80);
	t0 = hal_millis();
	while (reg8(W_MR, BLK_COMMON) & 0x80) {
		if (hal_millis() - t0 > CMD_WAIT_MS) {
			break;
		}
	}
	if (reg8(W_VERSIONR, BLK_COMMON) != W_VERSION) {
		return -1;
	}

	w_write(W_SHAR, BLK_COMMON, cfg->mac, 6);
	w_write(W_GAR, BLK_COMMON, cfg->gw, 4);
	w_write(W_SUBR, BLK_COMMON, cfg->mask, 4);
	w_write(W_SIPR, BLK_COMMON, cfg->ip, 4);
	present = 1;
	return 0;
}

uint8_t hal_tcp_link_up(void) {
	return present && (reg8(W_PHYCFGR, BLK_COMMON) & 0x01);
}

static int open_and_listen(void) {
	set8(S_MR, BLK_S0_REG, S_MR_TCP);
	set16(S_PORT, BLK_S0_REG, listen_port);
	set8(S_IR, BLK_S0_REG, 0xFF);
	if (command(S_CR_OPEN) != 0 || reg8(S_SR, BLK_S0_REG) != S_SR_INIT) {
		return -1;
	}
	if (command(S_CR_LISTEN) != 0 || reg8(S_SR, BLK_S0_REG) != S_SR_LISTEN) {
		return -1;
	}
	return 0;
}

int hal_tcp_listen(uint16_t port) {
	if (!present) {
		return -1;
	}
	listen_port = port;
	listening = (open_and_listen() == 0);
	return listening ? 0 : -1;
}

uint8_t hal_tcp_connected(void) {
	uint8_t sr;

	if (!listening) {
		return 0;
	}
	sr = reg8(S_SR, BLK_S0_REG);
	if (sr == S_SR_ESTABLISHED) {
		return 1;
	}
	if (sr == S_SR_CLOSE_WAIT || sr == S_SR_CLOSED) {
		hal_tcp_close();	/* the client left: listen again right away */
	}
	return 0;
}

int hal_tcp_read(uint8_t* buf, uint32_t max) {
	uint16_t avail, rd;

	if (reg8(S_SR, BLK_S0_REG) != S_SR_ESTABLISHED) {
		return -1;
	}
	avail = reg16_stable(S_RX_RSR);
	if (avail == 0) {
		return 0;
	}
	if (avail > max) {
		avail = (uint16_t)max;
	}
	rd = reg16(S_RX_RD, BLK_S0_REG);
	w_read(rd, BLK_S0_RX, buf, avail);		/* the chip wraps inside its buffer */
	set16(S_RX_RD, BLK_S0_REG, (uint16_t)(rd + avail));
	if (command(S_CR_RECV) != 0) {
		return -1;
	}
	return (int)avail;
}

int hal_tcp_write(const uint8_t* data, uint32_t len) {
	uint32_t t0 = hal_millis();
	uint16_t wr;
	uint8_t ir;

	if (reg8(S_SR, BLK_S0_REG) != S_SR_ESTABLISHED) {
		return -1;
	}
	while (reg16_stable(S_TX_FSR) < len) {
		if (hal_millis() - t0 > SEND_WAIT_MS) {
			return -1;
		}
	}
	wr = reg16(S_TX_WR, BLK_S0_REG);
	w_write(wr, BLK_S0_TX, data, len);
	set16(S_TX_WR, BLK_S0_REG, (uint16_t)(wr + len));
	set8(S_IR, BLK_S0_REG, S_IR_SEND_OK | S_IR_TIMEOUT);
	if (command(S_CR_SEND) != 0) {
		return -1;
	}
	t0 = hal_millis();
	for (;;) {
		ir = reg8(S_IR, BLK_S0_REG);
		if (ir & S_IR_SEND_OK) {
			set8(S_IR, BLK_S0_REG, S_IR_SEND_OK);
			return (int)len;
		}
		if ((ir & S_IR_TIMEOUT) || hal_millis() - t0 > SEND_WAIT_MS) {
			set8(S_IR, BLK_S0_REG, S_IR_TIMEOUT);
			return -1;
		}
		if (reg8(S_SR, BLK_S0_REG) != S_SR_ESTABLISHED) {
			return -1;
		}
	}
}

void hal_tcp_close(void) {
	if (!present) {
		return;
	}
	if (reg8(S_SR, BLK_S0_REG) == S_SR_ESTABLISHED) {
		command(S_CR_DISCON);
	}
	command(S_CR_CLOSE);
	set8(S_IR, BLK_S0_REG, 0xFF);
	listening = (listen_port != 0) && (open_and_listen() == 0);
}
