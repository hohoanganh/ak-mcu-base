#include <string.h>

#include "mb_master.h"
#include "hal.h"
#include "hal_rs485.h"

typedef enum {
	ST_IDLE = 0,		/* nothing in flight; result holds the last outcome */
	ST_SENDING,			/* request on the wire */
	ST_WAITING,			/* request out, collecting the answer */
} state_t;

/* the request while it is sent, then the answer: one frame at a time */
static uint8_t frame[256];
static uint16_t frame_len;
static state_t state;
static uint8_t req_unit, req_fc;
static uint32_t sent_ms;			/* when the request was out */
static uint32_t response_timeout;
static uint32_t frame_gap_ms;		/* t3.5 rounded up to the next ms */
static mb_master_done_t done_cb;
static mb_result_t result;
static uint8_t ready;

/* CRC-16/MODBUS, low byte first on the wire */
static uint16_t rtu_crc(const uint8_t* p, uint32_t n) {
	uint16_t crc = 0xFFFF;

	while (n--) {
		crc ^= *p++;
		for (uint8_t i = 0; i < 8; i++) {
			crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
		}
	}
	return crc;
}

int mb_master_init(uint32_t baudrate, uint16_t response_timeout_ms) {
	hal_rs485_init(baudrate);
	response_timeout = response_timeout_ms;
	/* end of frame: 3.5 characters of 11 bits, above 19200 baud fixed at
	 * 1.75 ms by the spec; whole ms, rounded up (5 ms at 9600) */
	frame_gap_ms = baudrate > 19200U ? 2U : 38500U / baudrate + 1U;
	state = ST_IDLE;
	memset(&result, 0, sizeof(result));
	ready = 1;
	return MB_OK;
}

static void finish(int err) {
	state = ST_IDLE;
	result.err = err;
	result.unit = req_unit;
	if (err != MB_OK) {
		result.len = 0;
		result.pdu = 0;
	}
	if (done_cb) {
		done_cb();
	}
}

int mb_master_send(uint8_t unit_id, const uint8_t* pdu, uint8_t pdu_len, mb_master_done_t done) {
	uint16_t crc;

	if (!ready || !pdu || pdu_len == 0 || pdu_len > MB_PDU_MAX || unit_id > 247) {
		return MB_ERR_ARG;
	}
	if (state != ST_IDLE || hal_rs485_tx_busy()) {
		return MB_ERR_BUSY;
	}
	/* bytes of an earlier, late answer must not be taken for this one */
	while (hal_rs485_getc() >= 0) {
	}
	frame[0] = unit_id;
	memcpy(&frame[1], pdu, pdu_len);
	crc = rtu_crc(frame, (uint32_t)pdu_len + 1);
	frame[pdu_len + 1] = (uint8_t)crc;
	frame[pdu_len + 2] = (uint8_t)(crc >> 8);
	frame_len = (uint16_t)(pdu_len + 3);

	req_unit = unit_id;
	req_fc = (uint8_t)(pdu[0] & 0x7F);
	done_cb = done;
	result.err = MB_ERR_BUSY;
	result.len = 0;
	result.pdu = 0;
	state = ST_SENDING;
	hal_rs485_send(frame, frame_len);
	return MB_OK;
}

uint8_t mb_master_busy(void) {
	return state != ST_IDLE;
}

const mb_result_t* mb_master_result(void) {
	return &result;
}

/* a whole frame is in frame[]: the answer to the request, or not */
static void parse_answer(void) {
	uint16_t crc;

	if (frame_len < 4) {
		finish(MB_ERR_BAD_RESPONSE);
		return;
	}
	crc = rtu_crc(frame, frame_len - 2U);
	if (frame[frame_len - 2] != (uint8_t)crc || frame[frame_len - 1] != (uint8_t)(crc >> 8)) {
		finish(MB_ERR_CRC);
		return;
	}
	if (frame[0] != req_unit) {
		finish(MB_ERR_UNIT_ID);
		return;
	}
	if ((frame[1] & 0x7F) != req_fc) {
		finish(MB_ERR_BAD_RESPONSE);
		return;
	}
	if (frame[1] & 0x80) {
		/* exception: fc | 0x80, code */
		finish(frame_len == 5 && frame[2] != 0 ? (int)frame[2] : MB_ERR_BAD_RESPONSE);
		return;
	}
	result.pdu = &frame[1];
	result.len = (uint8_t)(frame_len - 3);
	finish(MB_OK);
}

void mb_master_poll(void) {
	int c;

	switch (state) {
	case ST_IDLE:
		return;

	case ST_SENDING:
		if (hal_rs485_tx_busy()) {
			return;
		}
		if (req_unit == 0) {
			finish(MB_OK);		/* broadcast: nobody answers */
			return;
		}
		sent_ms = hal_millis();
		frame_len = 0;
		state = ST_WAITING;
		/* fall through: the answer may already be in the ring */

	case ST_WAITING:
		while ((c = hal_rs485_getc()) >= 0) {
			if (frame_len >= sizeof(frame)) {
				finish(MB_ERR_BAD_RESPONSE);	/* longer than any RTU frame */
				return;
			}
			frame[frame_len++] = (uint8_t)c;
		}
		if (frame_len == 0) {
			if (hal_millis() - sent_ms >= response_timeout) {
				finish(MB_ERR_TIMEOUT);
			}
			return;
		}
		if (hal_rs485_rx_idle_ms() < frame_gap_ms) {
			return;				/* still arriving */
		}
		parse_answer();
		return;
	}
}

/*----------------------------------------------------------------------------
 * PDU helpers
 *--------------------------------------------------------------------------*/
static void put16(uint8_t* p, uint16_t v) {
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

uint8_t mb_pdu_read(uint8_t* pdu, uint8_t fc, uint16_t address, uint16_t quantity) {
	if ((fc != 3 && fc != 4) || quantity < 1 || quantity > MB_READ_MAX ||
			(uint32_t)address + quantity > 0x10000UL) {
		return 0;
	}
	pdu[0] = fc;
	put16(pdu + 1, address);
	put16(pdu + 3, quantity);
	return 5;
}

uint8_t mb_pdu_write_single(uint8_t* pdu, uint16_t address, uint16_t value) {
	pdu[0] = 6;
	put16(pdu + 1, address);
	put16(pdu + 3, value);
	return 5;
}

uint8_t mb_pdu_write_multiple(uint8_t* pdu, uint16_t address, uint16_t quantity, const uint16_t* regs) {
	if (!regs || quantity < 1 || quantity > MB_WRITE_MAX || (uint32_t)address + quantity > 0x10000UL) {
		return 0;
	}
	pdu[0] = 16;
	put16(pdu + 1, address);
	put16(pdu + 3, quantity);
	pdu[5] = (uint8_t)(quantity * 2);
	for (uint16_t i = 0; i < quantity; i++) {
		put16(pdu + 6 + 2 * i, regs[i]);
	}
	return (uint8_t)(6 + 2 * quantity);
}

int mb_result_regs(const mb_result_t* r, uint16_t* out, uint16_t max) {
	uint16_t n;

	if (!r || r->err != MB_OK || !r->pdu || r->len < 2 || (r->pdu[0] != 3 && r->pdu[0] != 4)) {
		return MB_ERR_BAD_RESPONSE;
	}
	n = r->pdu[1];
	if ((n & 1) || n + 2U != r->len) {
		return MB_ERR_BAD_RESPONSE;
	}
	n /= 2;
	if (n > max) {
		return MB_ERR_BAD_RESPONSE;
	}
	for (uint16_t i = 0; i < n; i++) {
		out[i] = (uint16_t)((r->pdu[2 + 2 * i] << 8) | r->pdu[3 + 2 * i]);
	}
	return (int)n;
}

const char* mb_err_str(int err) {
	switch (err) {
	case MB_OK:					return "ok";
	case 1:						return "exception 1: illegal function";
	case 2:						return "exception 2: illegal data address";
	case 3:						return "exception 3: illegal data value";
	case 4:						return "exception 4: device failure";
	case 10:					return "exception 10: gateway path unavailable";
	case 11:					return "exception 11: gateway target did not respond";
	case MB_ERR_ARG:			return "bad argument";
	case MB_ERR_BAD_RESPONSE:	return "bad response";
	case MB_ERR_TIMEOUT:		return "timeout";
	case MB_ERR_TRANSPORT:		return "transport error";
	case MB_ERR_CRC:			return "crc error";
	case MB_ERR_BUSY:			return "busy";
	case MB_ERR_UNIT_ID:		return "answer from another unit";
	default:					return "error";
	}
}
