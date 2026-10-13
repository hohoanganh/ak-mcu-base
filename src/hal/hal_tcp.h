/**
 ******************************************************************************
 * @brief:  One TCP server connection for the port that has an Ethernet chip
 *          (STM32L151 kit: W5500 on the SPI header J6). Enough for a Modbus
 *          TCP server / gateway: one listening port, one client at a time,
 *          non-blocking reads. A port without Ethernet returns < 0 from
 *          hal_tcp_init() and the services built on it stay off.
 *
 *  All calls are made from tasks (never from an ISR) and return quickly:
 *  reads hand over what the chip holds, writes return once the chip has
 *  taken the bytes.
 ******************************************************************************
**/

#ifndef __HAL_TCP_H__
#define __HAL_TCP_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

typedef struct {
	uint8_t mac[6];
	uint8_t ip[4];
	uint8_t mask[4];
	uint8_t gw[4];
} hal_net_cfg_t;

/* Bring the chip up with a static address. 0 = ok, < 0 = no chip found. */
extern int hal_tcp_init(const hal_net_cfg_t* cfg);

/* PHY link: 1 = cable connected (0 when the chip cannot tell). */
extern uint8_t hal_tcp_link_up(void);

/* Listen on a port (one server socket). After hal_tcp_close() the socket
 * listens again on the same port. 0 = ok. */
extern int hal_tcp_listen(uint16_t port);

/* 1 while a client is connected. A client that went away shows up as 0
 * here and as < 0 from read / write. */
extern uint8_t hal_tcp_connected(void);

/* Bytes the client sent, up to max, copied into buf. 0 = nothing right now,
 * < 0 = the connection is gone. Never waits. */
extern int hal_tcp_read(uint8_t* buf, uint32_t max);

/* Send to the client. Returns len once the chip holds the bytes, < 0 = the
 * connection is gone or the chip did not take them in time. */
extern int hal_tcp_write(const uint8_t* data, uint32_t len);

/* Drop the client (if any) and listen again. */
extern void hal_tcp_close(void);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_TCP_H__ */
