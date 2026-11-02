/**
 ******************************************************************************
 * @brief:  Modbus TCP server forwarding every request to the RS485 slaves,
 *          non-blocking. The MBAP header (transaction, protocol, length,
 *          unit) is stripped, the PDU goes out as RTU through mb_master, and
 *          the answer comes back under the same header. Any function code
 *          passes through: the slave decides what it supports.
 *
 *  A slave that does not answer: exception 0x0B (gateway target device
 *  failed to respond); a bad answer: exception 0x04; the slave's own
 *  exception passes through. One request at a time; bytes of the next one
 *  wait in the socket.
 ******************************************************************************
**/

#include <string.h>

#include "mb_gateway.h"
#include "mb_master.h"
#include "hal.h"
#include "hal_tcp.h"

#define MBAP_LEN				(7)
#define EX_DEVICE_FAILURE		(0x04)
#define EX_TARGET_NO_RESPONSE	(0x0B)

static uint8_t frame[MBAP_LEN + MB_PDU_MAX];
static uint16_t frame_len;
static uint32_t last_byte_ms;
static uint8_t forwarding;			/* mb_master has our request */
static uint8_t discard;				/* the client left while forwarding: no answer */
static uint16_t tid;				/* of the request in flight */
static uint8_t req_unit, req_fc;
static uint8_t ready;
static uint32_t request_count, error_count;

static uint16_t get16(const uint8_t* p) {
	return (uint16_t)((p[0] << 8) | p[1]);
}

static void reply(const uint8_t* pdu, uint8_t n) {
	uint8_t out[MBAP_LEN + MB_PDU_MAX];

	out[0] = (uint8_t)(tid >> 8);
	out[1] = (uint8_t)tid;
	out[2] = out[3] = 0;						/* protocol id: Modbus */
	out[4] = (uint8_t)((n + 1) >> 8);
	out[5] = (uint8_t)(n + 1);
	out[6] = req_unit;
	memcpy(&out[MBAP_LEN], pdu, n);
	hal_tcp_write(out, (uint32_t)MBAP_LEN + n);
}

static void reply_exception(uint8_t code) {
	uint8_t ex[2];

	ex[0] = (uint8_t)(req_fc | 0x80);
	ex[1] = code;
	error_count++;
	reply(ex, 2);
}

/* mb_master: the answer (or none) is in */
static void on_done(void) {
	const mb_result_t* r = mb_master_result();

	forwarding = 0;
	if (discard) {
		return;
	}
	if (r->err == MB_OK) {
		reply(r->pdu, r->len);
	}
	else if (r->err > 0) {
		reply_exception((uint8_t)r->err);			/* the slave's exception, as is */
	}
	else {
		reply_exception(r->err == MB_ERR_TIMEOUT ? EX_TARGET_NO_RESPONSE : EX_DEVICE_FAILURE);
	}
}

/* bytes a whole frame needs: the header first, then what its length field says */
static uint16_t need(void) {
	return frame_len < 6 ? 6 : (uint16_t)(6 + get16(&frame[4]));
}

int mb_gateway_init(uint16_t tcp_port) {
	ready = 0;
	request_count = error_count = 0;
	frame_len = 0;
	forwarding = discard = 0;
	if (hal_tcp_listen(tcp_port) != 0) {
		return -1;
	}
	ready = 1;
	return 0;
}

void mb_gateway_poll(void) {
	int r;

	if (!ready) {
		return;
	}
	if (!hal_tcp_connected()) {
		frame_len = 0;
		if (forwarding) {
			discard = 1;		/* the answer, when it comes, has nobody to go to */
		}
		return;
	}
	if (forwarding) {
		return;					/* the socket keeps the next request */
	}

	while (frame_len < need()) {
		if (frame_len >= 6) {
			uint16_t len = get16(&frame[4]);

			/* not Modbus, or longer than an RTU frame can carry: the stream
			 * cannot be resynchronised, start over with a new connection */
			if (get16(&frame[2]) != 0 || len < 2 || len > MB_PDU_MAX + 1) {
				error_count++;
				hal_tcp_close();
				frame_len = 0;
				return;
			}
		}
		r = hal_tcp_read(&frame[frame_len], (uint32_t)(need() - frame_len));
		if (r < 0) {
			/* the client went away mid-frame or the chip failed: start over */
			hal_tcp_close();
			frame_len = 0;
			return;
		}
		if (r == 0) {
			if (frame_len && hal_millis() - last_byte_ms >= MB_GW_BYTE_TIMEOUT_MS) {
				frame_len = 0;	/* torn frame */
			}
			return;
		}
		frame_len = (uint16_t)(frame_len + r);
		last_byte_ms = hal_millis();
	}

	/* a whole request: forward it */
	tid = get16(&frame[0]);
	req_unit = frame[6];
	req_fc = (uint8_t)(frame[MBAP_LEN] & 0x7F);
	request_count++;
	discard = 0;
	if (mb_master_send(req_unit, &frame[MBAP_LEN], (uint8_t)(frame_len - MBAP_LEN), on_done) == MB_OK) {
		forwarding = 1;
	}
	else {
		reply_exception(EX_DEVICE_FAILURE);
	}
	frame_len = 0;
}

uint32_t mb_gateway_request_count(void) {
	return request_count;
}

uint32_t mb_gateway_error_count(void) {
	return error_count;
}
