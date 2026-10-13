/**
 ******************************************************************************
 * @brief:  Modbus TCP <-> RTU gateway: a Modbus TCP server (nanoMODBUS over
 *          hal_tcp) that forwards each request to the RS485 bus with the
 *          RTU master (mb_master) and returns the slave's answer.
 *
 *  - The MBAP unit identifier picks the RTU slave; 0 is an RTU broadcast
 *    (writes only, no answer expected).
 *  - Forwarded: FC 03 read holding, 04 read input, 06 write single,
 *    16 write multiple. Other functions get exception 1.
 *  - A slave that does not answer (or answers garbage) gets exception 4.
 *    The slave's own exceptions pass through unchanged.
 *  - One TCP client at a time; a second one waits until the first closes.
 *
 *  mb_gateway_poll() reads one TCP request and answers it; while it waits
 *  for the RTU slave it blocks for at most the master's response timeout
 *  (APP_MB_RESPONSE_TIMEOUT_MS), so call it from the kernel polling loop.
 ******************************************************************************
**/

#ifndef __MB_GATEWAY_H__
#define __MB_GATEWAY_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

/* Start the TCP server on tcp_port; hal_tcp_init() and mb_master_init() must
 * have succeeded before. 0 = ok. */
extern int mb_gateway_init(uint16_t tcp_port);

extern void mb_gateway_poll(void);

/* requests forwarded, and those that ended in an exception / no answer */
extern uint32_t mb_gateway_request_count(void);
extern uint32_t mb_gateway_error_count(void);

#ifdef __cplusplus
}
#endif

#endif /* __MB_GATEWAY_H__ */
