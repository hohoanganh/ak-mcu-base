/**
 ******************************************************************************
 * @brief:  Modbus RTU master over RS485, non-blocking.
 *
 *  One request at a time, in three steps that never wait:
 *    mb_master_send()   builds unit + PDU + CRC and starts the transmission
 *    mb_master_poll()   (kernel polling loop) collects the answer: a frame is
 *                       complete once the bus has been silent for t3.5, and
 *                       a slave that stays silent is given up on after the
 *                       response timeout
 *    done()             the callback given to send(), run from poll() when
 *                       the result is ready; read it with mb_master_result()
 *  A task posts itself a message from done() and reads the result in the
 *  handler of that message: nothing holds the scheduler for the frame time
 *  or the timeout. Only one request may be in flight (half-duplex bus).
 *
 *  The PDU is function code + data, as on the wire: the helpers below build
 *  the usual ones and decode a read answer. Any other function code can be
 *  sent as raw bytes (that is how the gateway forwards Modbus TCP).
 *
 *  Result: 0 = ok, > 0 = Modbus exception code sent by the slave,
 *  < 0 = no valid answer (MB_ERR_TIMEOUT, MB_ERR_CRC, ...).
 ******************************************************************************
**/

#ifndef __MB_MASTER_H__
#define __MB_MASTER_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define MB_OK					(0)
#define MB_ERR_ARG				(-1)
#define MB_ERR_BAD_RESPONSE		(-2)
#define MB_ERR_TIMEOUT			(-3)
#define MB_ERR_TRANSPORT		(-4)
#define MB_ERR_CRC				(-5)
#define MB_ERR_BUSY				(-6)	/* a request is still in flight */
#define MB_ERR_UNIT_ID			(-7)

#define MB_PDU_MAX				(253)	/* RTU frame 256 B - unit - CRC */
#define MB_READ_MAX				(125)	/* registers per FC 03 / 04 */
#define MB_WRITE_MAX			(123)	/* registers per FC 16 */

typedef struct {
	int err;				/* MB_OK, exception code > 0, or MB_ERR_* */
	uint8_t unit;			/* unit the request went to */
	uint8_t len;			/* bytes of pdu[] (0 unless err == MB_OK) */
	const uint8_t* pdu;		/* function code + data of the answer; valid until the next send */
} mb_result_t;

typedef void (*mb_master_done_t)(void);

/* response_timeout_ms: how long to wait for the first byte of an answer
 * after the request is out. Returns MB_OK. */
extern int mb_master_init(uint32_t baudrate, uint16_t response_timeout_ms);

/* Start one request. unit_id 0 = broadcast (no answer: done() comes right
 * after the frame is out, err MB_OK, len 0). Returns MB_OK, MB_ERR_ARG or
 * MB_ERR_BUSY. done may be NULL (then poll mb_master_busy()). */
extern int mb_master_send(uint8_t unit_id, const uint8_t* pdu, uint8_t pdu_len, mb_master_done_t done);

/* 1 from send() until the result is ready. */
extern uint8_t mb_master_busy(void);

/* Call from the kernel polling loop. Runs done() when a result is ready. */
extern void mb_master_poll(void);

/* The result of the last request (valid once busy() dropped / done() ran). */
extern const mb_result_t* mb_master_result(void);

/* PDU builders: fill pdu (MB_PDU_MAX bytes is always enough), return the
 * length, 0 on a bad argument. fc: 3 holding, 4 input registers. */
extern uint8_t mb_pdu_read(uint8_t* pdu, uint8_t fc, uint16_t address, uint16_t quantity);
extern uint8_t mb_pdu_write_single(uint8_t* pdu, uint16_t address, uint16_t value);
extern uint8_t mb_pdu_write_multiple(uint8_t* pdu, uint16_t address, uint16_t quantity, const uint16_t* regs);

/* Registers of a read answer (FC 03 / 04) into out, at most max. Returns the
 * count, or MB_ERR_BAD_RESPONSE when the PDU is not a well-formed read answer. */
extern int mb_result_regs(const mb_result_t* r, uint16_t* out, uint16_t max);

extern const char* mb_err_str(int err);

#ifdef __cplusplus
}
#endif

#endif /* __MB_MASTER_H__ */
