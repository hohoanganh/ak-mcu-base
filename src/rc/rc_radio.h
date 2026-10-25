/**
 ******************************************************************************
 * @brief:  Radio of the rc_tx / rc_rx builds: an nRF24L01+ in Enhanced
 *          ShockBurst mode (auto acknowledge, up to 2 retries, dynamic payload,
 *          payload in the ACK). Implemented by the port
 *          (port/stm32l151/nrf24.c); nothing here waits for the air.
 *
 *  Remote (PTX):   rc_radio_send() -> next period rc_radio_tx_done()
 *  Receiver (PRX): rc_radio_ack() preloads the reply, rc_radio_recv() polls
 ******************************************************************************
**/

#ifndef __RC_RADIO_H__
#define __RC_RADIO_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define RC_RADIO_MAX		(32)

#define RC_TX_BUSY			(0)
#define RC_TX_ACKED			(1)
#define RC_TX_LOST			(-1)

/* SPI and pins; 1 if a chip answers (register write read back) */
extern uint8_t rc_radio_init(void);
/* address (5 bytes, LSB first as the chip takes it), channel 0..125,
 * prx = 1: receiver */
extern void rc_radio_config(const uint8_t addr[5], uint8_t ch, uint8_t prx);
extern void rc_radio_channel(uint8_t ch);
extern void rc_radio_off(void);

/* remote */
extern void rc_radio_send(const uint8_t* data, uint8_t len);
/* RC_TX_BUSY, RC_TX_ACKED (ack payload copied to ack, *ack_len may be 0) or
 * RC_TX_LOST (no ACK after the retries) */
extern int8_t rc_radio_tx_done(uint8_t* ack, uint8_t* ack_len);

/* receiver: length of the packet copied to data, 0 if none */
extern uint8_t rc_radio_recv(uint8_t* data);
/* payload of the next ACK (replaces one still waiting) */
extern void rc_radio_ack(const uint8_t* data, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif /* __RC_RADIO_H__ */
