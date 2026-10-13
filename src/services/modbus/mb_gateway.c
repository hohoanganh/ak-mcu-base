/* gateway: needs both the server and the client half of nanoMODBUS */
#if !defined(NMBS_SERVER_DISABLED) && !defined(NMBS_CLIENT_DISABLED)

#include "mb_gateway.h"
#include "mb_master.h"
#include "mb_port.h"
#include "hal.h"
#include "hal_tcp.h"

/* a TCP frame is dropped when the gap between two bytes exceeds this */
#define MB_GW_BYTE_TIMEOUT_MS	(50)

static nmbs_t server;
static uint8_t ready;
static uint16_t listen_port;
static uint32_t request_count, error_count;

/*----------------------------------------------------------------------------
 * transport: hal_tcp (non-blocking) with a per-byte timeout like the RTU port
 *--------------------------------------------------------------------------*/
static int32_t tcp_read(uint8_t* buf, uint16_t count, int32_t timeout_ms, void* arg) {
	uint16_t n = 0;
	uint32_t t0 = hal_millis();

	(void)arg;
	while (n < count) {
		int r = hal_tcp_read(buf + n, (uint32_t)(count - n));

		if (r < 0) {
			return -1;					/* client gone: transport error */
		}
		if (r > 0) {
			n = (uint16_t)(n + r);
			t0 = hal_millis();
			continue;
		}
		if (timeout_ms == 0) {
			break;
		}
		if (timeout_ms > 0 && (uint32_t)(hal_millis() - t0) >= (uint32_t)timeout_ms) {
			break;
		}
		mb_port_wait_hook();
	}
	return n;
}

static int32_t tcp_write(const uint8_t* buf, uint16_t count, int32_t timeout_ms, void* arg) {
	(void)timeout_ms;
	(void)arg;
	return hal_tcp_write(buf, count) < 0 ? -1 : count;
}

/*----------------------------------------------------------------------------
 * server callbacks: one RTU transaction each
 *--------------------------------------------------------------------------*/
static nmbs_error forward_result(int err) {
	request_count++;
	if (err == MB_OK) {
		return NMBS_ERROR_NONE;
	}
	error_count++;
	/* the slave's exception passes through; no / bad answer = device failure */
	if (err > 0 && err <= NMBS_EXCEPTION_SERVER_DEVICE_FAILURE) {
		return (nmbs_error)err;
	}
	return NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
}

static nmbs_error cb_read_holding(uint16_t address, uint16_t quantity, uint16_t* out,
								  uint8_t unit_id, void* arg) {
	(void)arg;
	return forward_result(mb_master_read_holding(unit_id, address, quantity, out));
}

static nmbs_error cb_read_input(uint16_t address, uint16_t quantity, uint16_t* out,
								uint8_t unit_id, void* arg) {
	(void)arg;
	return forward_result(mb_master_read_input(unit_id, address, quantity, out));
}

static nmbs_error cb_write_single(uint16_t address, uint16_t value, uint8_t unit_id, void* arg) {
	(void)arg;
	return forward_result(mb_master_write_single(unit_id, address, value));
}

static nmbs_error cb_write_multi(uint16_t address, uint16_t quantity, const uint16_t* regs,
								 uint8_t unit_id, void* arg) {
	(void)arg;
	return forward_result(mb_master_write_multiple(unit_id, address, quantity, regs));
}

/*----------------------------------------------------------------------------
 * api
 *--------------------------------------------------------------------------*/
int mb_gateway_init(uint16_t tcp_port) {
	nmbs_platform_conf conf;
	nmbs_callbacks cb;

	ready = 0;
	request_count = error_count = 0;
	listen_port = tcp_port;

	nmbs_platform_conf_create(&conf);
	conf.transport = NMBS_TRANSPORT_TCP;
	conf.read = tcp_read;
	conf.write = tcp_write;

	nmbs_callbacks_create(&cb);
	cb.read_holding_registers = cb_read_holding;
	cb.read_input_registers = cb_read_input;
	cb.write_single_register = cb_write_single;
	cb.write_multiple_registers = cb_write_multi;

	/* the RTU address is meaningless on TCP: every unit id reaches the callbacks */
	if (nmbs_server_create(&server, 0, &conf, &cb) != NMBS_ERROR_NONE) {
		return -1;
	}
	nmbs_set_read_timeout(&server, 0);		/* poll must not block on an idle socket */
	nmbs_set_byte_timeout(&server, MB_GW_BYTE_TIMEOUT_MS);

	if (hal_tcp_listen(tcp_port) != 0) {
		return -1;
	}
	ready = 1;
	return 0;
}

void mb_gateway_poll(void) {
	if (!ready || !hal_tcp_connected()) {
		return;
	}
	if (nmbs_server_poll(&server) == NMBS_ERROR_TRANSPORT) {
		/* the client went away mid-frame or the chip failed: start over */
		hal_tcp_close();
	}
}

uint32_t mb_gateway_request_count(void) {
	return request_count;
}

uint32_t mb_gateway_error_count(void) {
	return error_count;
}

#endif /* gateway */
