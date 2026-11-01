/* slave role: left out of a master-only build (-DNMBS_SERVER_DISABLED) */
#if !defined(NMBS_SERVER_DISABLED)

#include <string.h>

#include "mb_slave.h"
#include "mb_ota.h"
#include "mb_port.h"
#include "hal_rs485.h"

/* AK_RUST_MODBUS=1: requests are split by the Rust code of ak-mcu-rust (crate ak-ffi,
 * libak_ffi.a) instead of nmbs_server_poll(): address, length, CRC, function code and
 * the quantity checks are Rust, the register callbacks below (and OTA) stay C. */
#if defined(AK_RUST_MODBUS)
#include "ak_rs.h"
#endif

/* a frame is dropped when the gap between two bytes exceeds this (only a
 * broken frame waits for it: whole frames are in the ring before the poll) */
#define MB_SLAVE_BYTE_TIMEOUT_MS	(20)

#if !defined(AK_RUST_MODBUS)
static nmbs_t server;
#endif
static const mb_slave_map_t* reg_map;
static uint32_t request_count;
static uint32_t frame_gap_ms;		/* t3.5 rounded up to the next ms */

#if defined(AK_RUST_MODBUS)
static uint8_t  unit;
/* one RTU frame at most. Also holds the answer while it is on the wire: no new
 * request is read before hal_rs485_tx_busy() clears (as nanoMODBUS's msg.buf) */
static uint8_t  rx_buf[256];
static uint16_t rx_len;
static ak_rs_mb_t req;
#endif

/* Block that holds the whole range [address, address + quantity), or NULL. */
static const mb_reg_block_t* find_block(const mb_reg_block_t* blocks, uint8_t num,
										uint16_t address, uint16_t quantity) {
	for (uint8_t i = 0; i < num; i++) {
		if (address >= blocks[i].start &&
				(uint32_t)address + quantity <= (uint32_t)blocks[i].start + blocks[i].count) {
			return &blocks[i];
		}
	}
	return 0;
}

static nmbs_error cb_read_holding(uint16_t address, uint16_t quantity, uint16_t* out,
								  uint8_t unit_id, void* arg) {
	const mb_reg_block_t* b;

	(void)unit_id;
	(void)arg;
	request_count++;

	if (mb_ota_owns(address, quantity)) {
		return mb_ota_read(address, quantity, out);
	}
	b = find_block(reg_map->holding, reg_map->holding_num, address, quantity);
	if (!b) {
		return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
	}
	memcpy(out, &b->data[address - b->start], (size_t)quantity * 2);
	return NMBS_ERROR_NONE;
}

static nmbs_error cb_read_input(uint16_t address, uint16_t quantity, uint16_t* out,
								uint8_t unit_id, void* arg) {
	const mb_reg_block_t* b;

	(void)unit_id;
	(void)arg;
	request_count++;

	b = find_block(reg_map->input, reg_map->input_num, address, quantity);
	if (!b) {
		return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
	}
	memcpy(out, &b->data[address - b->start], (size_t)quantity * 2);
	return NMBS_ERROR_NONE;
}

static nmbs_error cb_write_multi(uint16_t address, uint16_t quantity, const uint16_t* regs,
								 uint8_t unit_id, void* arg) {
	const mb_reg_block_t* b;

	(void)arg;
	request_count++;

	if (mb_ota_owns(address, quantity)) {
		/* RTU broadcast (unit 0) gets no answer: never let it drive an update */
		if (unit_id == 0) {
			return NMBS_EXCEPTION_ILLEGAL_FUNCTION;
		}
		return mb_ota_write(address, quantity, regs);
	}
	b = find_block(reg_map->holding, reg_map->holding_num, address, quantity);
	if (!b || !b->writable) {
		return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
	}
	memcpy(&b->data[address - b->start], regs, (size_t)quantity * 2);
	if (reg_map->on_write) {
		reg_map->on_write(address, quantity);
	}
	return NMBS_ERROR_NONE;
}

static nmbs_error cb_write_single(uint16_t address, uint16_t value, uint8_t unit_id, void* arg) {
	return cb_write_multi(address, 1, &value, unit_id, arg);
}

int mb_slave_init(uint8_t unit_id, uint32_t baudrate, const mb_slave_map_t* map) {
	nmbs_platform_conf conf;
	nmbs_callbacks cb;

	if (!map || unit_id == 0 || unit_id > 247) {
		return -1;
	}
	reg_map = map;
	request_count = 0;
	mb_ota_init(0);

	hal_rs485_init(baudrate);

	/* end of frame: 3.5 characters of 11 bits, above 19200 baud fixed at
	 * 1.75 ms by the spec. Counted in whole ms and rounded up (5 ms at 9600).
	 * Polling a little early only costs a short wait in port_read(). */
	frame_gap_ms = baudrate > 19200U ? 2U : 38500U / baudrate + 1U;

#if defined(AK_RUST_MODBUS)
	(void)conf;
	(void)cb;
	unit = unit_id;
	rx_len = 0;
	return 0;
#else
	mb_port_conf_send(&conf);
	nmbs_callbacks_create(&cb);
	cb.read_holding_registers = cb_read_holding;
	cb.read_input_registers = cb_read_input;
	cb.write_single_register = cb_write_single;
	cb.write_multiple_registers = cb_write_multi;

	if (nmbs_server_create(&server, unit_id, &conf, &cb) != NMBS_ERROR_NONE) {
		return -1;
	}
	nmbs_set_read_timeout(&server, 0);		/* poll must not block on an idle bus */
	nmbs_set_byte_timeout(&server, MB_SLAVE_BYTE_TIMEOUT_MS);
	return 0;
#endif
}

void mb_slave_enable_ota(void (*on_commit)(void)) {
	mb_ota_init(on_commit);
}

/* Only a whole frame goes to nanoMODBUS: once the bus has been silent for
 * t3.5 every byte of the request is in the RX ring and port_read() takes it
 * without waiting. The answer leaves in the background (hal_rs485_send);
 * nanoMODBUS's buffer holds it, so no new poll before the bus is released. */
#if defined(AK_RUST_MODBUS)
/* exception code for a callback error, as nanoMODBUS: 1..4 as is, anything else 4 */
static uint8_t exception_of(nmbs_error e) {
	return (e > 0 && e <= NMBS_EXCEPTION_SERVER_DEVICE_FAILURE) ? (uint8_t)e
															  : NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
}

/* One whole request (the bus is silent): Rust splits it, the callbacks answer it. */
static void rust_poll(void) {
	uint16_t regs[125];
	nmbs_error e = NMBS_ERROR_NONE;
	uint16_t n = 0;
	uint8_t from;
	int c;

	while (rx_len < sizeof(rx_buf) && (c = hal_rs485_getc()) >= 0) {
		rx_buf[rx_len++] = (uint8_t)c;
	}
	if (rx_len == sizeof(rx_buf) && (c = hal_rs485_getc()) >= 0) {
		/* longer than any RTU frame: drop all of it, like nanoMODBUS's flush */
		while (hal_rs485_getc() >= 0) {
		}
		rx_len = 0;
		return;
	}
	if (rx_len == 0) {
		return;
	}
	switch (ak_rs_mb_tach(rx_buf, rx_len, unit, &req)) {
	case AK_RS_MB_THIEU_BYTE:
		/* a broken frame: keep it while the missing bytes may still come */
		if (hal_rs485_rx_idle_ms() >= MB_SLAVE_BYTE_TIMEOUT_MS) {
			rx_len = 0;
		}
		return;

	case AK_RS_MB_NGOAI_LE:
		e = (nmbs_error)req.ngoai_le;
		break;

	case AK_RS_MB_YEU_CAU:
		from = req.quang_ba ? 0 : unit;		/* the callbacks refuse OTA by broadcast */
		switch (req.ma_ham) {
		case 3:
		case 4:
			e = (req.ma_ham == 3 ? cb_read_holding : cb_read_input)(req.dia_chi, req.so_luong, regs, from, 0);
			if (e == NMBS_ERROR_NONE) {
				n = ak_rs_mb_tra_loi_doc(unit, req.ma_ham, regs, req.so_luong, rx_buf, sizeof(rx_buf));
			}
			break;
		case 6:
			e = cb_write_single(req.dia_chi, req.gia_tri[0], from, 0);
			if (e == NMBS_ERROR_NONE) {
				n = ak_rs_mb_tra_loi_ghi(unit, 6, req.dia_chi, req.gia_tri[0], rx_buf, sizeof(rx_buf));
			}
			break;
		default:	/* 16 */
			e = cb_write_multi(req.dia_chi, req.so_luong, req.gia_tri, from, 0);
			if (e == NMBS_ERROR_NONE) {
				n = ak_rs_mb_tra_loi_ghi(unit, 16, req.dia_chi, req.so_luong, rx_buf, sizeof(rx_buf));
			}
			break;
		}
		break;

	default:		/* another unit, bad CRC or length: no answer */
		rx_len = 0;
		return;
	}
	rx_len = 0;
	if (req.quang_ba) {
		return;		/* nobody answers a broadcast */
	}
	if (e != NMBS_ERROR_NONE) {
		n = ak_rs_mb_ngoai_le(unit, req.ma_ham, exception_of(e), rx_buf, sizeof(rx_buf));
	}
	if (n) {
		hal_rs485_send(rx_buf, n);
	}
}
#endif

void mb_slave_poll(void) {
	if (!reg_map || hal_rs485_tx_busy() || hal_rs485_rx_idle_ms() < frame_gap_ms) {
		return;
	}
#if defined(AK_RUST_MODBUS)
	rust_poll();
#else
	nmbs_server_poll(&server);
#endif
}

uint32_t mb_slave_request_count(void) {
	return request_count;
}

#endif /* !NMBS_SERVER_DISABLED */
