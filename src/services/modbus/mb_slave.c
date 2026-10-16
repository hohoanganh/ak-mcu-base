/* slave role: left out of a master-only build (-DNMBS_SERVER_DISABLED) */
#if !defined(NMBS_SERVER_DISABLED)

#include <string.h>

#include "mb_slave.h"
#include "mb_ota.h"
#include "mb_port.h"
#include "hal_rs485.h"

/* a frame is dropped when the gap between two bytes exceeds this (only a
 * broken frame waits for it: whole frames are in the ring before the poll) */
#define MB_SLAVE_BYTE_TIMEOUT_MS	(20)

static nmbs_t server;
static const mb_slave_map_t* reg_map;
static uint32_t request_count;
static uint32_t frame_gap_ms;		/* t3.5 rounded up to the next ms */

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
	mb_port_conf_send(&conf);

	/* end of frame: 3.5 characters of 11 bits, above 19200 baud fixed at
	 * 1.75 ms by the spec. Counted in whole ms and rounded up (5 ms at 9600).
	 * Polling a little early only costs a short wait in port_read(). */
	frame_gap_ms = baudrate > 19200U ? 2U : 38500U / baudrate + 1U;

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
}

void mb_slave_enable_ota(void (*on_commit)(void)) {
	mb_ota_init(on_commit);
}

/* Only a whole frame goes to nanoMODBUS: once the bus has been silent for
 * t3.5 every byte of the request is in the RX ring and port_read() takes it
 * without waiting. The answer leaves in the background (hal_rs485_send);
 * nanoMODBUS's buffer holds it, so no new poll before the bus is released. */
void mb_slave_poll(void) {
	if (!reg_map || hal_rs485_tx_busy() || hal_rs485_rx_idle_ms() < frame_gap_ms) {
		return;
	}
	nmbs_server_poll(&server);
}

uint32_t mb_slave_request_count(void) {
	return request_count;
}

#endif /* !NMBS_SERVER_DISABLED */
