/**
 ******************************************************************************
 * @brief:  Modbus RTU slave over RS485 (nanoMODBUS server).
 *
 *  The application describes its registers as blocks of uint16_t it owns:
 *
 *    static uint16_t status[3], setpoint[2];
 *    static const mb_reg_block_t holding[] = {
 *        { 0,   3, status,   MB_REG_RO },
 *        { 100, 2, setpoint, MB_REG_RW },
 *    };
 *    static const mb_slave_map_t map = { holding, 2, 0, 0, on_write };
 *    mb_slave_init(1, 9600, &map);
 *    ... call mb_slave_poll() from a polling task
 *
 *  A request must lie inside one block, else exception 2 (illegal data
 *  address); writing a read-only block gives exception 2 as well. Function
 *  codes served: 03, 04, 06, 16. The firmware-update block 0xF000 (mb_ota.h)
 *  is answered here too once mb_slave_enable_ota() was called.
 *
 *  mb_slave_poll() never waits for the bus: it takes a request only after
 *  the bus has been silent for 3.5 characters (5 ms at 9600 baud), when the
 *  whole frame is already in the RX ring, and the answer goes out in the
 *  background (hal_rs485_send). A frame that stops in the middle still waits
 *  up to MB_SLAVE_BYTE_TIMEOUT_MS for its missing bytes.
 ******************************************************************************
**/

#ifndef __MB_SLAVE_H__
#define __MB_SLAVE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define MB_REG_RO		(0)
#define MB_REG_RW		(1)

typedef struct {
	uint16_t  start;		/* first register address */
	uint16_t  count;
	uint16_t* data;			/* count values owned by the application */
	uint8_t   writable;		/* MB_REG_RO / MB_REG_RW (holding registers only) */
} mb_reg_block_t;

typedef struct {
	const mb_reg_block_t* holding;		/* FC 03 / 06 / 16 */
	uint8_t holding_num;
	const mb_reg_block_t* input;		/* FC 04 */
	uint8_t input_num;
	/* optional: a master wrote holding registers [address, address + quantity) */
	void (*on_write)(uint16_t address, uint16_t quantity);
} mb_slave_map_t;

/* unit_id 1..247. Returns 0 on success. */
extern int mb_slave_init(uint8_t unit_id, uint32_t baudrate, const mb_slave_map_t* map);

/* Firmware update over the 0xF000 block. on_commit: a verified image is in
 * STAGING - post the install request, do not reset inside the callback. */
extern void mb_slave_enable_ota(void (*on_commit)(void));

extern void mb_slave_poll(void);

/* Frames answered since init (diagnostics). */
extern uint32_t mb_slave_request_count(void);

#ifdef __cplusplus
}
#endif

#endif /* __MB_SLAVE_H__ */
