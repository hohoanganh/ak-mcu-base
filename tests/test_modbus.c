/**
 * Modbus services on host: slave register map, firmware update over the
 * 0xF000 block (with the real fw_update / fw_image on emulated flash), master.
 * The other end of the RS485 link is a nanoMODBUS client/server in the test.
 */
#include <string.h>

#include "hal.h"
#include "hal_rs485.h"
#include "crc.h"
#include "fw_image.h"
#include "fw_update.h"
#include "mb_port.h"
#include "mb_slave.h"
#include "mb_master.h"
#include "mb_gateway.h"
#include "mb_ota.h"
#include "hal_tcp.h"
#include "port_host.h"
#include "tiny_test.h"

/*----------------------------------------------------------------------------
 * the other end of the link
 *--------------------------------------------------------------------------*/
static nmbs_t peer;				/* test client (slave tests) or test server (master tests) */
static uint8_t peer_is_server;
static uint8_t peer_silent;		/* master tests: the slave does not answer */

/* firmware waits for bytes: let virtual time pass, run the fake slave */
void mb_port_wait_hook(void) {
	host_advance_ms(1);
	if (peer_is_server && !peer_silent) {
		nmbs_server_poll(&peer);
	}
}

/* peer reads what the firmware sent; a test client first lets the slave run */
static int32_t peer_read(uint8_t* buf, uint16_t count, int32_t timeout_ms, void* arg) {
	uint32_t n;

	(void)timeout_ms;
	(void)arg;
	if (!peer_is_server) {
		/* a real master hears the whole answer, then the bus is silent
		 * for t3.5 before the slave takes the request (5 ms at 9600) */
		while (hal_rs485_tx_busy()) {
			host_advance_ms(1);
		}
		host_advance_ms(5);
		mb_slave_poll();
	}
	n = host_rs485_take_tx(buf, count);
	if (n == 0) {
		host_advance_ms(1);
	}
	return (int32_t)n;
}

static int32_t peer_write(const uint8_t* buf, uint16_t count, int32_t timeout_ms, void* arg) {
	(void)timeout_ms;
	(void)arg;
	host_rs485_inject(buf, count);
	return count;
}

static void peer_conf(nmbs_platform_conf* conf) {
	nmbs_platform_conf_create(conf);
	conf->transport = NMBS_TRANSPORT_RTU;
	conf->read = peer_read;
	conf->write = peer_write;
}

static void make_client(uint8_t unit) {
	nmbs_platform_conf conf;

	peer_conf(&conf);
	peer_is_server = 0;
	nmbs_client_create(&peer, &conf);
	nmbs_set_destination_rtu_address(&peer, unit);
	nmbs_set_read_timeout(&peer, 0);	/* peer_read already ran the slave */
	nmbs_set_byte_timeout(&peer, 0);
}

/*----------------------------------------------------------------------------
 * slave under test
 *--------------------------------------------------------------------------*/
static uint16_t status_regs[3];
static uint16_t setpoint_regs[2];
static uint16_t input_regs[2];
static int write_calls;
static uint16_t write_addr, write_qty;
static int commit_calls;

static void on_write(uint16_t address, uint16_t quantity) {
	write_calls++;
	write_addr = address;
	write_qty = quantity;
}

static void on_commit(void) {
	commit_calls++;
}

static const mb_reg_block_t holding_blocks[] = {
	{ 0,   3, status_regs,   MB_REG_RO },
	{ 100, 2, setpoint_regs, MB_REG_RW },
};
static const mb_reg_block_t input_blocks[] = {
	{ 0, 2, input_regs, MB_REG_RO },
};
static const mb_slave_map_t map = { holding_blocks, 2, input_blocks, 1, on_write };

static void setup_slave(uint8_t ota) {
	host_reset_state(0xFF);
	host_use_virtual_time(1);
	write_calls = commit_calls = 0;
	status_regs[0] = 0x0102; status_regs[1] = 3; status_regs[2] = 777;
	setpoint_regs[0] = 10; setpoint_regs[1] = 20;
	input_regs[0] = 0xAAAA; input_regs[1] = 0x5555;
	CHECK_EQ(mb_slave_init(1, 9600, &map), 0);
	if (ota) {
		mb_slave_enable_ota(on_commit);
	}
	make_client(1);
}

static void test_slave_read(void) {
	uint16_t r[4];

	setup_slave(0);
	CHECK_EQ(host_rs485_baud(), 9600);
	CHECK_EQ(nmbs_read_holding_registers(&peer, 0, 3, r), NMBS_ERROR_NONE);
	CHECK_EQ(r[0], 0x0102);
	CHECK_EQ(r[2], 777);
	CHECK_EQ(nmbs_read_holding_registers(&peer, 101, 1, r), NMBS_ERROR_NONE);
	CHECK_EQ(r[0], 20);
	CHECK_EQ(nmbs_read_input_registers(&peer, 0, 2, r), NMBS_ERROR_NONE);
	CHECK_EQ(r[1], 0x5555);
	/* outside every block / across the end of a block */
	CHECK_EQ(nmbs_read_holding_registers(&peer, 50, 1, r), NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
	CHECK_EQ(nmbs_read_holding_registers(&peer, 2, 2, r), NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
	CHECK_EQ(nmbs_read_input_registers(&peer, 1, 2, r), NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
	CHECK_EQ(mb_slave_request_count(), 6);
}

static void test_slave_write(void) {
	uint16_t v[2] = { 111, 222 };

	setup_slave(0);
	CHECK_EQ(nmbs_write_single_register(&peer, 100, 55), NMBS_ERROR_NONE);
	CHECK_EQ(setpoint_regs[0], 55);
	CHECK_EQ(write_calls, 1);
	CHECK_EQ(write_addr, 100);
	CHECK_EQ(write_qty, 1);
	CHECK_EQ(nmbs_write_multiple_registers(&peer, 100, 2, v), NMBS_ERROR_NONE);
	CHECK_EQ(setpoint_regs[1], 222);
	CHECK_EQ(write_calls, 2);
	/* read-only block, unknown address */
	CHECK_EQ(nmbs_write_single_register(&peer, 0, 9), NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
	CHECK_EQ(status_regs[0], 0x0102);
	CHECK_EQ(nmbs_write_single_register(&peer, 300, 9), NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
	CHECK_EQ(write_calls, 2);
}

/* CRC of Modbus RTU, low byte first on the wire */
static uint16_t rtu_crc(const uint8_t* p, uint32_t n) {
	uint16_t crc = 0xFFFF;

	while (n--) {
		crc ^= *p++;
		for (int i = 0; i < 8; i++) {
			crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
		}
	}
	return crc;
}

/* FC 03, unit 1, registers 0..2 */
static void read_req(uint8_t* f) {
	uint16_t crc;

	f[0] = 1; f[1] = 3; f[2] = 0; f[3] = 0; f[4] = 0; f[5] = 3;
	crc = rtu_crc(f, 6);
	f[6] = (uint8_t)crc;
	f[7] = (uint8_t)(crc >> 8);
}

/* the slave takes a request only after t3.5 of silence: it never waits for
 * the rest of a frame inside mb_slave_poll() */
static void test_slave_waits_for_frame_end(void) {
	uint8_t f[8], rsp[16];
	uint32_t t;

	setup_slave(0);
	read_req(f);
	host_rs485_inject(f, 4);				/* first half of the request */
	host_advance_ms(2);
	t = hal_millis();
	mb_slave_poll();
	CHECK_EQ(hal_millis(), t);				/* did not wait for the rest */
	CHECK_EQ(host_rs485_take_tx(rsp, sizeof(rsp)), 0);
	host_advance_ms(1);
	host_rs485_inject(f + 4, 4);			/* the rest */
	mb_slave_poll();						/* bus not silent yet */
	CHECK_EQ(host_rs485_take_tx(rsp, sizeof(rsp)), 0);
	host_advance_ms(4);
	mb_slave_poll();						/* 4 ms < t3.5 at 9600 (4.01 ms) */
	CHECK_EQ(host_rs485_take_tx(rsp, sizeof(rsp)), 0);
	CHECK_EQ(mb_slave_request_count(), 0);

	/* silence long enough: the whole frame is in the ring, answered at once */
	host_advance_ms(1);
	t = hal_millis();
	mb_slave_poll();
	CHECK_EQ(hal_millis(), t);
	CHECK_EQ(host_rs485_take_tx(rsp, sizeof(rsp)), 3 + 6 + 2);
	CHECK_EQ(rsp[0], 1);
	CHECK_EQ(rsp[1], 3);
	CHECK_EQ(rsp[2], 6);
	CHECK_EQ(mb_slave_request_count(), 1);
}

/* the answer goes out in the background: mb_slave_poll() returns while it is
 * on the wire, and takes no new request before the bus is released */
static void test_slave_answer_does_not_block(void) {
	uint8_t f[8], rsp[16];
	uint32_t t;

	setup_slave(0);
	read_req(f);
	host_rs485_inject(f, sizeof(f));
	host_advance_ms(5);
	t = hal_millis();
	mb_slave_poll();
	CHECK_EQ(hal_millis(), t);
	CHECK(hal_rs485_tx_busy());				/* 11 bytes = 11.5 ms at 9600 */
	CHECK_EQ(host_rs485_take_tx(rsp, sizeof(rsp)), 11);

	host_rs485_inject(f, sizeof(f));		/* a master that does not wait */
	host_advance_ms(6);
	mb_slave_poll();						/* still sending: not taken */
	CHECK(hal_rs485_tx_busy());
	CHECK_EQ(mb_slave_request_count(), 1);
	host_advance_ms(6);
	CHECK(!hal_rs485_tx_busy());
	mb_slave_poll();
	CHECK_EQ(mb_slave_request_count(), 2);
	CHECK_EQ(host_rs485_take_tx(rsp, sizeof(rsp)), 11);
}

static void test_slave_other_unit(void) {
	uint16_t r[1];

	setup_slave(0);
	make_client(2);		/* request for unit 2: unit 1 stays silent */
	CHECK_EQ(nmbs_read_holding_registers(&peer, 0, 1, r), NMBS_ERROR_TIMEOUT);
	CHECK_EQ(mb_slave_request_count(), 0);
}

/*----------------------------------------------------------------------------
 * firmware update over the 0xF000 block
 *--------------------------------------------------------------------------*/
static uint8_t img_buf[0x4000];

static uint32_t make_image(uint8_t* out, uint32_t img_size, const char* board, uint32_t seed) {
	fw_image_hdr_t* h = (fw_image_hdr_t*)out;
	uint32_t app_addr = hal_flash_info(FLASH_PART_APP)->addr;
	uint8_t* p = out + FW_IMAGE_HDR_SIZE;
	uint32_t x = seed * 2654435761UL + 1;
	uint32_t sp = HOST_RAM_END;
	uint32_t rst = app_addr + FW_IMAGE_HDR_SIZE + 0x101;

	memset(out, 0, FW_IMAGE_HDR_SIZE);
	for (uint32_t i = 0; i < img_size; i++) {
		x = x * 1103515245UL + 12345UL;
		p[i] = (uint8_t)(x >> 16);
	}
	memcpy(p, &sp, 4);
	memcpy(p + 4, &rst, 4);

	h->magic = FW_IMAGE_MAGIC;
	h->hdr_version = FW_IMAGE_HDR_VERSION;
	h->hdr_size = FW_IMAGE_HDR_SIZE;
	h->img_size = img_size;
	h->img_crc32 = crc32_update(CRC32_INIT, p, img_size);
	h->load_addr = app_addr + FW_IMAGE_HDR_SIZE;
	h->version.major = 2;
	strncpy(h->board, board, FW_IMAGE_BOARD_LEN);
	h->hdr_crc32 = crc32_update(CRC32_INIT, h, FW_IMAGE_HDR_SIZE - 4);
	return FW_IMAGE_HDR_SIZE + img_size;
}

static uint16_t ota_reg(uint16_t address) {
	uint16_t v = 0xDEAD;

	nmbs_read_holding_registers(&peer, address, 1, &v);
	return v;
}

static nmbs_error ota_header(uint32_t len, uint32_t psk) {
	uint16_t h[5] = { (uint16_t)(len >> 16), (uint16_t)len, 0, (uint16_t)(psk >> 16), (uint16_t)psk };

	return nmbs_write_multiple_registers(&peer, MB_OTA_REG_LEN_HI, 5, h);
}

static nmbs_error ota_chunk(const uint8_t* img, uint32_t total, uint32_t off) {
	uint16_t regs[2 + MB_OTA_CHUNK_MAX_REGS];
	uint32_t n = (total - off < 128) ? total - off : 128;
	uint16_t q = (uint16_t)((n + 1) / 2);

	regs[0] = (uint16_t)(off >> 16);
	regs[1] = (uint16_t)off;
	for (uint16_t i = 0; i < q; i++) {
		uint8_t lo = (2U * i + 1 < n) ? img[off + 2 * i + 1] : 0xFF;
		regs[2 + i] = (uint16_t)((img[off + 2 * i] << 8) | lo);
	}
	return nmbs_write_multiple_registers(&peer, MB_OTA_REG_CHUNK, (uint16_t)(2 + q), regs);
}

static nmbs_error ota_send_all(const uint8_t* img, uint32_t total) {
	nmbs_error e = NMBS_ERROR_NONE;

	for (uint32_t off = 0; e == NMBS_ERROR_NONE && off < total; off += 128) {
		e = ota_chunk(img, total, off);
	}
	return e;
}

static void test_ota_full_flow(void) {
	fw_image_hdr_t hdr;
	uint32_t total;

	/* both staging geometries; odd image size ends inside a register */
	for (int layout = 1; layout >= 0; layout--) {
		host_set_layout((uint8_t)layout);
		setup_slave(1);
		total = make_image(img_buf, layout ? 3001 : 3000, hal_board_name(), 7);

		CHECK_EQ(ota_reg(MB_OTA_REG_STATUS), MB_OTA_ST_IDLE);
		CHECK_EQ(ota_header(total, MB_OTA_PSK_IMG), NMBS_ERROR_NONE);
		CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_BEGIN), NMBS_ERROR_NONE);
		CHECK_EQ(ota_reg(MB_OTA_REG_STATUS), MB_OTA_ST_RECEIVING);

		CHECK_EQ(ota_chunk(img_buf, total, 0), NMBS_ERROR_NONE);
		/* the same chunk again (answer lost): accepted, not written twice */
		CHECK_EQ(ota_chunk(img_buf, total, 0), NMBS_ERROR_NONE);
		CHECK_EQ(ota_reg(MB_OTA_REG_RECV_LO), 128);
		for (uint32_t off = 128; off < total; off += 128) {
			CHECK_EQ(ota_chunk(img_buf, total, off), NMBS_ERROR_NONE);
		}
		CHECK_EQ(ota_reg(MB_OTA_REG_RECV_LO), (uint16_t)total);

		CHECK_EQ(commit_calls, 0);
		CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_COMMIT), NMBS_ERROR_NONE);
		CHECK_EQ(ota_reg(MB_OTA_REG_STATUS), MB_OTA_ST_COMMITTED);
		CHECK_EQ(commit_calls, 1);
		/* COMMIT again: still ok, not committed twice; no new session */
		CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_COMMIT), NMBS_ERROR_NONE);
		CHECK_EQ(commit_calls, 1);
		CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_BEGIN), NMBS_EXCEPTION_ILLEGAL_DATA_VALUE);

		/* what the bootloader will find in STAGING is the image, bit for bit */
		CHECK_EQ(fw_update_state(), FW_UPDATE_READY);
		CHECK_EQ(fw_image_verify(FLASH_PART_STAGING, &hdr), FW_OK);
		CHECK_EQ(hdr.version.major, 2);
		CHECK(memcmp(host_part_mem(FLASH_PART_STAGING), img_buf, total) == 0);
	}
	host_set_layout(1);
}

static void test_ota_rejects(void) {
	uint32_t total;

	/* OTA not enabled: the block answers "device failure", nothing is written */
	setup_slave(0);
	total = make_image(img_buf, 2000, hal_board_name(), 1);
	CHECK_EQ(ota_header(total, MB_OTA_PSK_IMG), NMBS_ERROR_NONE);
	CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_BEGIN), NMBS_EXCEPTION_SERVER_DEVICE_FAILURE);

	/* image type of the older base */
	setup_slave(1);
	CHECK_EQ(ota_header(total, 0x1A2B3C4DUL), NMBS_ERROR_NONE);
	CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_BEGIN), NMBS_EXCEPTION_ILLEGAL_DATA_VALUE);
	CHECK_EQ(ota_reg(MB_OTA_REG_STATUS), MB_OTA_ST_ERR_HEADER);

	/* larger than STAGING */
	setup_slave(1);
	CHECK_EQ(ota_header(hal_flash_info(FLASH_PART_STAGING)->size + 4, MB_OTA_PSK_IMG), NMBS_ERROR_NONE);
	CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_BEGIN), NMBS_EXCEPTION_ILLEGAL_DATA_VALUE);
	CHECK_EQ(ota_reg(MB_OTA_REG_STATUS), MB_OTA_ST_ERR_SIZE);

	/* a chunk out of order */
	setup_slave(1);
	CHECK_EQ(ota_header(total, MB_OTA_PSK_IMG), NMBS_ERROR_NONE);
	CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_BEGIN), NMBS_ERROR_NONE);
	CHECK_EQ(ota_chunk(img_buf, total, 0), NMBS_ERROR_NONE);
	CHECK_EQ(ota_chunk(img_buf, total, 256), NMBS_EXCEPTION_SERVER_DEVICE_FAILURE);
	CHECK_EQ(ota_reg(MB_OTA_REG_STATUS), MB_OTA_ST_ERR_OFFSET);
	/* header may be rewritten after an error, a new session starts clean */
	CHECK_EQ(ota_header(total, MB_OTA_PSK_IMG), NMBS_ERROR_NONE);
	CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_BEGIN), NMBS_ERROR_NONE);
	CHECK_EQ(ota_reg(MB_OTA_REG_RECV_LO), 0);

	/* COMMIT before everything arrived */
	CHECK_EQ(ota_chunk(img_buf, total, 0), NMBS_ERROR_NONE);
	CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_COMMIT), NMBS_EXCEPTION_ILLEGAL_DATA_VALUE);
	CHECK_EQ(commit_calls, 0);
}

static void test_ota_bad_image(void) {
	uint32_t total;

	/* one flipped bit in the body: image CRC */
	setup_slave(1);
	total = make_image(img_buf, 2000, hal_board_name(), 3);
	img_buf[FW_IMAGE_HDR_SIZE + 900] ^= 0x10;
	CHECK_EQ(ota_header(total, MB_OTA_PSK_IMG), NMBS_ERROR_NONE);
	CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_BEGIN), NMBS_ERROR_NONE);
	CHECK_EQ(ota_send_all(img_buf, total), NMBS_ERROR_NONE);
	CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_COMMIT), NMBS_EXCEPTION_ILLEGAL_DATA_VALUE);
	CHECK_EQ(ota_reg(MB_OTA_REG_STATUS), MB_OTA_ST_ERR_CHECKSUM);
	CHECK_EQ(ota_reg(MB_OTA_REG_DETAIL), FW_ERR_IMG_CRC);
	CHECK_EQ(commit_calls, 0);

	/* image built for another board */
	setup_slave(1);
	total = make_image(img_buf, 2000, "other-board", 3);
	CHECK_EQ(ota_header(total, MB_OTA_PSK_IMG), NMBS_ERROR_NONE);
	CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_BEGIN), NMBS_ERROR_NONE);
	CHECK_EQ(ota_send_all(img_buf, total), NMBS_ERROR_NONE);
	CHECK_EQ(nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_COMMIT), NMBS_EXCEPTION_ILLEGAL_DATA_VALUE);
	CHECK_EQ(ota_reg(MB_OTA_REG_STATUS), MB_OTA_ST_ERR_HEADER);
	CHECK_EQ(ota_reg(MB_OTA_REG_DETAIL), FW_ERR_BOARD);
	CHECK_EQ(commit_calls, 0);
}

static void test_ota_broadcast_blocked(void) {
	uint32_t total;

	setup_slave(1);
	total = make_image(img_buf, 2000, hal_board_name(), 4);
	make_client(0);		/* RTU broadcast: no answer expected */
	ota_header(total, MB_OTA_PSK_IMG);
	host_advance_ms(5);
	mb_slave_poll();	/* a broadcast gets no answer: nobody else runs the slave */
	nmbs_write_single_register(&peer, MB_OTA_REG_CMD, MB_OTA_CMD_BEGIN);
	host_advance_ms(5);
	mb_slave_poll();
	make_client(1);
	CHECK_EQ(ota_reg(MB_OTA_REG_STATUS), MB_OTA_ST_IDLE);
	CHECK_EQ(ota_reg(MB_OTA_REG_LEN_LO), 0);
}

/*----------------------------------------------------------------------------
 * master under test, against a nanoMODBUS server in the test
 *--------------------------------------------------------------------------*/
static uint16_t far_regs[4] = { 11, 22, 33, 44 };

static nmbs_error far_read(uint16_t address, uint16_t quantity, uint16_t* out, uint8_t unit_id, void* arg) {
	(void)unit_id;
	(void)arg;
	if ((uint32_t)address + quantity > 4) {
		return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
	}
	memcpy(out, &far_regs[address], (size_t)quantity * 2);
	return NMBS_ERROR_NONE;
}

static nmbs_error far_write(uint16_t address, uint16_t value, uint8_t unit_id, void* arg) {
	(void)unit_id;
	(void)arg;
	if (address >= 4) {
		return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
	}
	far_regs[address] = value;
	return NMBS_ERROR_NONE;
}

static void setup_master(void) {
	nmbs_platform_conf conf;
	nmbs_callbacks cb;

	host_reset_state(0xFF);
	host_use_virtual_time(1);
	far_regs[0] = 11; far_regs[1] = 22; far_regs[2] = 33; far_regs[3] = 44;
	peer_conf(&conf);
	nmbs_callbacks_create(&cb);
	cb.read_holding_registers = far_read;
	cb.write_single_register = far_write;
	nmbs_server_create(&peer, 5, &conf, &cb);
	nmbs_set_read_timeout(&peer, 0);
	nmbs_set_byte_timeout(&peer, 0);
	peer_is_server = 1;
	peer_silent = 0;
	CHECK_EQ(mb_master_init(19200, 50), MB_OK);
}

static void test_master(void) {
	uint16_t r[4];
	uint32_t t0;

	setup_master();
	CHECK_EQ(host_rs485_baud(), 19200);
	CHECK_EQ(mb_master_read_holding(5, 0, 4, r), MB_OK);
	CHECK_EQ(r[0], 11);
	CHECK_EQ(r[3], 44);
	CHECK_EQ(mb_master_write_single(5, 2, 99), MB_OK);
	CHECK_EQ(far_regs[2], 99);
	/* the slave's exception code comes back as a positive value */
	CHECK_EQ(mb_master_read_holding(5, 3, 2, r), 2);
	CHECK(strcmp(mb_err_str(2), "exception 2: illegal data address") == 0);

	/* nobody answers: gives up after the response timeout, not later */
	peer_silent = 1;
	t0 = hal_millis();
	CHECK_EQ(mb_master_read_holding(5, 0, 1, r), MB_ERR_TIMEOUT);
	CHECK(hal_millis() - t0 >= 50 && hal_millis() - t0 < 80);

	/* a late answer to that request must not be taken for the next one */
	peer_silent = 0;
	nmbs_server_poll(&peer);
	CHECK_EQ(mb_master_read_holding(5, 1, 1, r), MB_OK);
	CHECK_EQ(r[0], 22);
	peer_is_server = 0;
}

/*----------------------------------------------------------------------------
 * gateway under test: a Modbus TCP client in the test, the RTU slave of the
 * master tests (unit 5) on the other side
 *--------------------------------------------------------------------------*/
static uint16_t far_in[2] = { 0x1234, 0x5678 };

static nmbs_error far_read_input(uint16_t address, uint16_t quantity, uint16_t* out, uint8_t unit_id, void* arg) {
	(void)unit_id;
	(void)arg;
	if ((uint32_t)address + quantity > 2) {
		return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
	}
	memcpy(out, &far_in[address], (size_t)quantity * 2);
	return NMBS_ERROR_NONE;
}

static nmbs_error far_write_multi(uint16_t address, uint16_t quantity, const uint16_t* regs, uint8_t unit_id, void* arg) {
	(void)unit_id;
	(void)arg;
	if ((uint32_t)address + quantity > 4) {
		return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
	}
	memcpy(&far_regs[address], regs, (size_t)quantity * 2);
	return NMBS_ERROR_NONE;
}

static void setup_gateway(void) {
	nmbs_platform_conf conf;
	nmbs_callbacks cb;
	hal_net_cfg_t net = { { 2, 0, 0, 0, 0, 1 }, { 192, 168, 1, 80 }, { 255, 255, 255, 0 }, { 192, 168, 1, 1 } };

	host_reset_state(0xFF);
	host_use_virtual_time(1);
	far_regs[0] = 11; far_regs[1] = 22; far_regs[2] = 33; far_regs[3] = 44;
	peer_conf(&conf);
	nmbs_callbacks_create(&cb);
	cb.read_holding_registers = far_read;
	cb.read_input_registers = far_read_input;
	cb.write_single_register = far_write;
	cb.write_multiple_registers = far_write_multi;
	nmbs_server_create(&peer, 5, &conf, &cb);
	nmbs_set_read_timeout(&peer, 0);
	nmbs_set_byte_timeout(&peer, 0);
	peer_is_server = 1;
	peer_silent = 0;
	host_tcp_set_present(1);
	CHECK_EQ(hal_tcp_init(&net), 0);
	CHECK_EQ(mb_master_init(19200, 50), MB_OK);
	CHECK_EQ(mb_gateway_init(502), 0);
	CHECK_EQ(host_tcp_port(), 502);
}

/* one Modbus TCP request through the gateway; returns the response length */
static uint32_t tcp_request(uint16_t tid, uint8_t unit, const uint8_t* pdu, uint16_t pdu_len, uint8_t* resp, uint32_t max) {
	uint8_t frame[7 + 256];

	frame[0] = (uint8_t)(tid >> 8);
	frame[1] = (uint8_t)tid;
	frame[2] = frame[3] = 0;							/* protocol id */
	frame[4] = (uint8_t)((pdu_len + 1) >> 8);
	frame[5] = (uint8_t)(pdu_len + 1);
	frame[6] = unit;
	memcpy(&frame[7], pdu, pdu_len);
	host_tcp_inject(frame, 7U + pdu_len);
	mb_gateway_poll();
	return host_tcp_take_tx(resp, max);
}

static void test_gateway(void) {
	uint8_t resp[64];
	uint32_t n;

	setup_gateway();

	/* no client yet: poll does nothing, nothing goes out */
	mb_gateway_poll();
	CHECK_EQ(host_tcp_take_tx(resp, sizeof(resp)), 0);
	CHECK_EQ(host_tcp_connect(), 0);

	/* FC 3: four holding registers of unit 5 */
	{
		const uint8_t pdu[] = { 0x03, 0x00, 0x00, 0x00, 0x04 };
		n = tcp_request(0x1234, 5, pdu, sizeof(pdu), resp, sizeof(resp));
		CHECK_EQ(n, 7 + 2 + 8);
		CHECK_EQ(resp[0], 0x12);
		CHECK_EQ(resp[1], 0x34);						/* transaction id echoed */
		CHECK_EQ(resp[5], 11);							/* MBAP length */
		CHECK_EQ(resp[6], 5);							/* unit id echoed */
		CHECK_EQ(resp[7], 0x03);
		CHECK_EQ(resp[8], 8);
		CHECK_EQ((resp[9] << 8) | resp[10], 11);
		CHECK_EQ((resp[15] << 8) | resp[16], 44);
	}
	/* FC 4: input registers */
	{
		const uint8_t pdu[] = { 0x04, 0x00, 0x01, 0x00, 0x01 };
		n = tcp_request(2, 5, pdu, sizeof(pdu), resp, sizeof(resp));
		CHECK_EQ(n, 7 + 2 + 2);
		CHECK_EQ((resp[9] << 8) | resp[10], 0x5678);
	}
	/* FC 6: write single, echoed, lands in the slave */
	{
		const uint8_t pdu[] = { 0x06, 0x00, 0x02, 0x00, 0x63 };
		n = tcp_request(3, 5, pdu, sizeof(pdu), resp, sizeof(resp));
		CHECK_EQ(n, 7 + 5);
		CHECK_EQ(resp[7], 0x06);
		CHECK_EQ(resp[11], 0x63);
		CHECK_EQ(far_regs[2], 99);
	}
	/* FC 16: write multiple */
	{
		const uint8_t pdu[] = { 0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x07, 0x00, 0x08 };
		n = tcp_request(4, 5, pdu, sizeof(pdu), resp, sizeof(resp));
		CHECK_EQ(n, 7 + 5);
		CHECK_EQ(resp[7], 0x10);
		CHECK_EQ(far_regs[0], 7);
		CHECK_EQ(far_regs[1], 8);
	}
	CHECK_EQ(mb_gateway_request_count(), 4);
	CHECK_EQ(mb_gateway_error_count(), 0);

	/* the slave's exception comes back as a Modbus TCP exception */
	{
		const uint8_t pdu[] = { 0x03, 0x00, 0x03, 0x00, 0x02 };
		n = tcp_request(5, 5, pdu, sizeof(pdu), resp, sizeof(resp));
		CHECK_EQ(n, 7 + 2);
		CHECK_EQ(resp[7], 0x83);
		CHECK_EQ(resp[8], 2);
	}
	/* a function the gateway does not forward: exception 1 */
	{
		const uint8_t pdu[] = { 0x01, 0x00, 0x00, 0x00, 0x08 };
		n = tcp_request(6, 5, pdu, sizeof(pdu), resp, sizeof(resp));
		CHECK_EQ(n, 7 + 2);
		CHECK_EQ(resp[7], 0x81);
		CHECK_EQ(resp[8], 1);
	}
	/* nobody answers on RS485: exception 4, after the master's timeout */
	{
		const uint8_t pdu[] = { 0x03, 0x00, 0x00, 0x00, 0x01 };
		uint32_t t0 = hal_millis();
		peer_silent = 1;
		n = tcp_request(7, 5, pdu, sizeof(pdu), resp, sizeof(resp));
		peer_silent = 0;
		CHECK_EQ(n, 7 + 2);
		CHECK_EQ(resp[7], 0x83);
		CHECK_EQ(resp[8], 4);
		CHECK(hal_millis() - t0 >= 50 && hal_millis() - t0 < 100);
		/* the slave wakes up and answers the stale request: the gateway's
		 * master must not take that for the answer to its next one */
		nmbs_server_poll(&peer);
	}
	/* a request for another unit goes to that unit: unit 6 does not exist here */
	{
		const uint8_t pdu[] = { 0x03, 0x00, 0x00, 0x00, 0x01 };
		n = tcp_request(8, 6, pdu, sizeof(pdu), resp, sizeof(resp));
		CHECK_EQ(n, 7 + 2);
		CHECK_EQ(resp[6], 6);
		CHECK_EQ(resp[8], 4);
	}
	CHECK_EQ(mb_gateway_error_count(), 3);

	/* the client goes away mid-frame: the gateway drops the socket and takes
	 * the next client */
	{
		const uint8_t half[] = { 0x00, 0x09, 0x00, 0x00 };
		const uint8_t pdu[] = { 0x03, 0x00, 0x00, 0x00, 0x01 };
		host_tcp_inject(half, sizeof(half));
		host_tcp_disconnect();
		mb_gateway_poll();
		CHECK_EQ(hal_tcp_connected(), 0);
		CHECK_EQ(host_tcp_connect(), 0);
		n = tcp_request(9, 5, pdu, sizeof(pdu), resp, sizeof(resp));
		CHECK_EQ(n, 7 + 2 + 2);
		CHECK_EQ((resp[9] << 8) | resp[10], 7);
	}
	/* a torn frame (gap longer than the byte timeout) is dropped, the next
	 * complete one is answered */
	{
		const uint8_t head[] = { 0x00, 0x0A, 0x00, 0x00, 0x00, 0x06 };
		const uint8_t pdu[] = { 0x04, 0x00, 0x00, 0x00, 0x01 };
		host_tcp_inject(head, sizeof(head));
		mb_gateway_poll();
		CHECK_EQ(host_tcp_take_tx(resp, sizeof(resp)), 0);
		n = tcp_request(11, 5, pdu, sizeof(pdu), resp, sizeof(resp));
		CHECK_EQ(n, 7 + 2 + 2);
		CHECK_EQ((resp[9] << 8) | resp[10], 0x1234);
	}
	peer_is_server = 0;
}

/* No Ethernet chip: hal_tcp_init() fails, the gateway stays off, the master
 * still works. */
static void test_gateway_no_chip(void) {
	hal_net_cfg_t net = { { 2, 0, 0, 0, 0, 1 }, { 192, 168, 1, 80 }, { 255, 255, 255, 0 }, { 192, 168, 1, 1 } };
	uint16_t r[1];

	setup_master();
	host_tcp_set_present(0);
	CHECK_EQ(hal_tcp_init(&net), -1);
	CHECK_EQ(mb_gateway_init(502), -1);
	mb_gateway_poll();
	CHECK_EQ(host_tcp_port(), 0);
	CHECK_EQ(host_tcp_connect(), -1);
	CHECK_EQ(mb_master_read_holding(5, 1, 1, r), MB_OK);
	CHECK_EQ(r[0], 22);
	host_tcp_set_present(1);
	peer_is_server = 0;
}

TT_MAIN_BEGIN("test_modbus")
	RUN_TEST(test_slave_read);
	RUN_TEST(test_slave_write);
	RUN_TEST(test_slave_waits_for_frame_end);
	RUN_TEST(test_slave_answer_does_not_block);
	RUN_TEST(test_slave_other_unit);
	RUN_TEST(test_ota_full_flow);
	RUN_TEST(test_ota_rejects);
	RUN_TEST(test_ota_bad_image);
	RUN_TEST(test_ota_broadcast_blocked);
	RUN_TEST(test_master);
	RUN_TEST(test_gateway);
	RUN_TEST(test_gateway_no_chip);
TT_MAIN_END()
