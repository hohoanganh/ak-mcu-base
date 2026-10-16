/**
 ******************************************************************************
 * @brief:  hal_rs485.h for STM32L151: USART2 (PA2 TX, PA3 RX) + direction pin
 *          (port_cfg.h). RX: interrupt -> ring, time of the last byte. TX:
 *          TXE interrupt from the caller's buffer, then the TC interrupt
 *          releases the direction pin once the last stop bit is out, not when
 *          the last byte entered the shift register.
 ******************************************************************************
**/

#include "stm32l1xx.h"
#include "stm32l1xx_conf.h"

#include "hal.h"
#include "hal_rs485.h"
#include "port_cfg.h"

/* one writer (ISR: head), one reader (main loop: tail): no lock needed */
static volatile uint8_t rx_buf[PORT_RS485_RX_BUF];
static volatile uint16_t rx_head, rx_tail;
static volatile uint32_t rx_bytes;
static volatile uint32_t rx_last_ms;

/* hal_rs485_send() in flight: the ISR owns these until tx_busy drops */
static const uint8_t* volatile tx_ptr;
static volatile uint32_t tx_left;
static volatile uint8_t tx_busy;

void usart2_irq_handler(void) {
	uint32_t sr = USART2->SR;

	if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
		uint8_t c = (uint8_t)USART2->DR;	/* reading DR clears RXNE and ORE */
		uint16_t next = (uint16_t)((rx_head + 1) & (PORT_RS485_RX_BUF - 1));

		rx_bytes++;
		rx_last_ms = hal_millis();
		/* full: drop the byte - the frame fails its CRC and the master repeats */
		if (next != rx_tail) {
			rx_buf[rx_head] = c;
			rx_head = next;
		}
	}

	if ((USART2->CR1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE)) {
		USART2->DR = *tx_ptr;				/* SR read above + DR write: clears TC */
		tx_ptr = tx_ptr + 1;
		if (--tx_left == 0) {
			/* last byte in the shift register: wait for its stop bit */
			USART2->CR1 = (USART2->CR1 & ~USART_CR1_TXEIE) | USART_CR1_TCIE;
		}
	}
	else if ((USART2->CR1 & USART_CR1_TCIE) && (sr & USART_SR_TC)) {
		USART2->CR1 &= ~USART_CR1_TCIE;
		PORT_RS485_DIR_PORT->BSRRH = PORT_RS485_DIR_PIN;
		tx_busy = 0;
	}
}

void hal_rs485_init(uint32_t baudrate) {
	GPIO_InitTypeDef gpio;
	USART_InitTypeDef uart;
	NVIC_InitTypeDef nvic;

	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | PORT_RS485_DIR_CLK, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

	/* direction: output, low = receive */
	GPIO_StructInit(&gpio);
	gpio.GPIO_Mode = GPIO_Mode_OUT;
	gpio.GPIO_OType = GPIO_OType_PP;
	gpio.GPIO_Speed = GPIO_Speed_2MHz;
	gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
	gpio.GPIO_Pin = PORT_RS485_DIR_PIN;
	GPIO_Init(PORT_RS485_DIR_PORT, &gpio);
	PORT_RS485_DIR_PORT->BSRRH = PORT_RS485_DIR_PIN;

	GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);
	gpio.GPIO_Mode = GPIO_Mode_AF;
	gpio.GPIO_PuPd = GPIO_PuPd_UP;		/* RX idles high while the transceiver output is off */
	gpio.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
	GPIO_Init(GPIOA, &gpio);

	USART_Cmd(USART2, DISABLE);
	USART_StructInit(&uart);
	uart.USART_BaudRate = baudrate;
	uart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART2, &uart);

	rx_head = rx_tail = 0;
	rx_bytes = 0;
	rx_last_ms = hal_millis();
	tx_left = 0;
	tx_busy = 0;

	nvic.NVIC_IRQChannel = USART2_IRQn;
	nvic.NVIC_IRQChannelPreemptionPriority = 2;
	nvic.NVIC_IRQChannelSubPriority = 0;
	nvic.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvic);

	USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
	USART_Cmd(USART2, ENABLE);
}

int hal_rs485_getc(void) {
	int c;

	if (rx_tail == rx_head) {
		return -1;
	}
	c = rx_buf[rx_tail];
	rx_tail = (uint16_t)((rx_tail + 1) & (PORT_RS485_RX_BUF - 1));
	return c;
}

uint32_t hal_rs485_rx_bytes(void) {
	return rx_bytes;
}

uint8_t hal_rs485_tx_busy(void) {
	return tx_busy;
}

uint32_t hal_rs485_rx_idle_ms(void) {
	return hal_millis() - rx_last_ms;
}

/* with interrupts masked or from an ISR nothing would ever drain a send */
static uint8_t irq_usable(void) {
	return __get_PRIMASK() == 0 && (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) == 0;
}

void hal_rs485_send(const uint8_t* data, uint32_t len) {
	if (len == 0) {
		return;
	}
	while (tx_busy) {
	}
	tx_ptr = data;
	tx_left = len;
	tx_busy = 1;
	PORT_RS485_DIR_PORT->BSRRL = PORT_RS485_DIR_PIN;
	USART2->CR1 |= USART_CR1_TXEIE;		/* TX bits of CR1: only the ISR touches them while busy */
}

void hal_rs485_write(const uint8_t* data, uint32_t len) {
	if (len == 0) {
		return;
	}
	if (irq_usable()) {
		hal_rs485_send(data, len);
		while (tx_busy) {
		}
		return;
	}
	PORT_RS485_DIR_PORT->BSRRL = PORT_RS485_DIR_PIN;
	while (len--) {
		while (!(USART2->SR & USART_SR_TXE)) {
		}
		USART2->DR = *data++;
	}
	/* TC, not TXE: the stop bit of the last byte must be on the wire */
	while (!(USART2->SR & USART_SR_TC)) {
	}
	PORT_RS485_DIR_PORT->BSRRH = PORT_RS485_DIR_PIN;
}
