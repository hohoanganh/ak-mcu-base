#include <string.h>

#include "fw_proto.h"
#include "fw_update.h"
#include "boot_ctrl.h"
#include "crc.h"
#include "hal.h"

typedef enum {
	ST_SOF = 0,
	ST_CMD,
	ST_SEQ,
	ST_LEN_L,
	ST_LEN_H,
	ST_DATA,
	ST_CRC_L,
	ST_CRC_H,
} rx_state_t;

static const fw_proto_cfg_t* cfg;
static rx_state_t rx_state = ST_SOF;
static uint8_t  rx_cmd;
static uint8_t  rx_seq;
static uint16_t rx_len;
static uint16_t rx_idx;
static uint16_t rx_crc;
static uint32_t rx_last_ms;
static uint8_t  rx_buf[FW_PROTO_MAX_PAYLOAD];
static fw_proto_action_t pending_action = FW_PROTO_ACT_NONE;

static void put_u32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_ver(uint8_t* p, const fw_version_t* v) {
	p[0] = v->major;
	p[1] = v->minor;
	p[2] = v->patch;
	p[3] = 0;
	put_u32(p + 4, v->build);
}

uint32_t fw_proto_pack(uint8_t* out, uint8_t cmd, uint8_t seq, const uint8_t* payload, uint16_t len) {
	uint16_t crc;

	out[0] = FW_PROTO_SOF;
	out[1] = cmd;
	out[2] = seq;
	out[3] = (uint8_t)len;
	out[4] = (uint8_t)(len >> 8);
	if (len) {
		memcpy(out + 5, payload, len);
	}
	crc = crc16_update(CRC16_INIT, out + 1, 4U + len);
	out[5 + len] = (uint8_t)crc;
	out[6 + len] = (uint8_t)(crc >> 8);
	return 7U + len;
}

static void send_resp(const uint8_t* payload, uint16_t len) {
	uint8_t frame[48];

	if (cfg && cfg->tx && len + 7U <= sizeof(frame)) {
		cfg->tx(frame, fw_proto_pack(frame, (uint8_t)(rx_cmd | FW_PROTO_RESP), rx_seq, payload, len));
	}
}

static void handle_frame(void) {
	uint8_t r[40];
	uint16_t rl = 1;
	fw_err_t st = FW_OK;
	fw_image_hdr_t hdr;

	memset(r, 0, sizeof(r));

	switch (rx_cmd) {
	case FW_PROTO_CMD_INFO: {
		const char* board = hal_board_name();
		r[1] = FW_PROTO_VERSION;
		r[2] = cfg->role;
		put_ver(&r[3], cfg->version);
		strncpy((char*)&r[11], board, 16);
		put_u32(&r[27], hal_flash_info(FLASH_PART_STAGING)->size);
		r[31] = (uint8_t)FW_PROTO_MAX_CHUNK;
		r[32] = (uint8_t)(FW_PROTO_MAX_CHUNK >> 8);
		r[33] = (uint8_t)fw_update_state();
		put_u32(&r[34], fw_update_written());
		rl = 38;
	}
		break;

	case FW_PROTO_CMD_BEGIN:
		st = (rx_len == 4) ? fw_update_begin(get_u32(rx_buf)) : FW_ERR_ARG;
		break;

	case FW_PROTO_CMD_DATA:
		if (rx_len <= 4) {
			st = FW_ERR_ARG;
		}
		else {
			st = fw_update_write(get_u32(rx_buf), &rx_buf[4], rx_len - 4U);
		}
		put_u32(&r[1], fw_update_written());
		rl = 5;
		break;

	case FW_PROTO_CMD_END:
		st = fw_update_finish(&hdr);
		if (st == FW_OK) {
			put_ver(&r[1], &hdr.version);
			put_u32(&r[9], hdr.img_size);
			rl = 13;
		}
		break;

	case FW_PROTO_CMD_INSTALL:
		if (fw_update_state() != FW_UPDATE_READY) {
			st = FW_ERR_STATE;
		}
		else {
			pending_action = FW_PROTO_ACT_INSTALL;
		}
		break;

	case FW_PROTO_CMD_LOADER:
		pending_action = FW_PROTO_ACT_LOADER;
		break;

	case FW_PROTO_CMD_RESET:
		pending_action = FW_PROTO_ACT_RESET;
		break;

	case FW_PROTO_CMD_RUN:
		pending_action = FW_PROTO_ACT_RUN;
		break;

	case FW_PROTO_CMD_CONFIRM: {
		/* r[1]: 1 = was on trial, 0 = nothing to confirm */
		int c = boot_ctrl_confirm();
		r[1] = (uint8_t)(c > 0);
		rl = 2;
		st = (c < 0) ? FW_ERR_FLASH : FW_OK;
	}
		break;

	case FW_PROTO_CMD_ROLLBACK:
		if (hal_flash_info(FLASH_PART_BACKUP)->size == 0) {
			st = FW_ERR_STATE;
		}
		else {
			pending_action = FW_PROTO_ACT_ROLLBACK;
		}
		break;

	default:
		if (rx_cmd >= FW_PROTO_CMD_EXT && cfg && cfg->ext) {
			uint16_t n = 0;

			st = (fw_err_t)cfg->ext(rx_cmd, rx_buf, rx_len, &r[1], &n);
			rl = (uint16_t)(1 + (n > FW_PROTO_EXT_RESP_MAX ? FW_PROTO_EXT_RESP_MAX : n));
		}
		else {
			st = FW_ERR_CMD;
		}
		break;
	}

	r[0] = (uint8_t)st;
	send_resp(r, rl);
}

void fw_proto_init(const fw_proto_cfg_t* c) {
	cfg = c;
	rx_state = ST_SOF;
	pending_action = FW_PROTO_ACT_NONE;
}

uint8_t fw_proto_busy(void) {
	return rx_state != ST_SOF;
}

fw_proto_action_t fw_proto_take_action(void) {
	fw_proto_action_t a = pending_action;
	pending_action = FW_PROTO_ACT_NONE;
	return a;
}

uint8_t fw_proto_feed(uint8_t b, uint32_t now_ms) {
	if (rx_state != ST_SOF && (now_ms - rx_last_ms) > FW_PROTO_BYTE_TIMEOUT) {
		rx_state = ST_SOF;	/* frame interrupted */
	}
	rx_last_ms = now_ms;

	switch (rx_state) {
	case ST_SOF:
		if (b != FW_PROTO_SOF) {
			return 0;
		}
		rx_state = ST_CMD;
		break;

	case ST_CMD:
		rx_cmd = b;
		rx_crc = crc16_update(CRC16_INIT, &b, 1);
		rx_state = ST_SEQ;
		break;

	case ST_SEQ:
		rx_seq = b;
		rx_crc = crc16_update(rx_crc, &b, 1);
		rx_state = ST_LEN_L;
		break;

	case ST_LEN_L:
		rx_len = b;
		rx_crc = crc16_update(rx_crc, &b, 1);
		rx_state = ST_LEN_H;
		break;

	case ST_LEN_H:
		rx_len |= (uint16_t)b << 8;
		rx_crc = crc16_update(rx_crc, &b, 1);
		rx_idx = 0;
		if (rx_len > sizeof(rx_buf)) {
			rx_state = ST_SOF;	/* garbage / oversized frame: drop */
		}
		else {
			rx_state = rx_len ? ST_DATA : ST_CRC_L;
		}
		break;

	case ST_DATA:
		rx_buf[rx_idx++] = b;
		if (rx_idx >= rx_len) {
			rx_crc = crc16_update(rx_crc, rx_buf, rx_len);
			rx_state = ST_CRC_L;
		}
		break;

	case ST_CRC_L:
		rx_idx = b;		/* hold low byte */
		rx_state = ST_CRC_H;
		break;

	case ST_CRC_H:
		rx_state = ST_SOF;
		if ((uint16_t)(rx_idx | ((uint16_t)b << 8)) == rx_crc && cfg) {
			handle_frame();
		}
		/* bad crc: stay silent, the sender retries on timeout */
		break;
	}

	return 1;
}
