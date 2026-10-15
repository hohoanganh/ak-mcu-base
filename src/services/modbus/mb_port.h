/**
 ******************************************************************************
 * @brief:  nanoMODBUS platform functions on top of hal_rs485 (RTU).
 ******************************************************************************
**/

#ifndef __MB_PORT_H__
#define __MB_PORT_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#include "nanomodbus.h"

/* Fills conf with the RTU transport over hal_rs485. Writes wait until the
 * frame is on the wire (the master reads the answer right after). */
extern void mb_port_conf(nmbs_platform_conf* conf);

/* Same, but writes only start the transmission (hal_rs485_send): for the
 * slave, which must not poll again while hal_rs485_tx_busy(). */
extern void mb_port_conf_send(nmbs_platform_conf* conf);

/* Called while a read waits for bytes. Weak, empty by default; host tests
 * use it to advance virtual time and to run the other end of the link. */
extern void mb_port_wait_hook(void);

#ifdef __cplusplus
}
#endif

#endif /* __MB_PORT_H__ */
