#include "mb_port.h"
#include "hal.h"
#include "hal_rs485.h"

__attribute__((weak)) void mb_port_wait_hook(void) {
}

/* timeout_ms: per byte. 0 = take what is there, < 0 = wait forever. */
static int32_t port_read(uint8_t* buf, uint16_t count, int32_t timeout_ms, void* arg) {
	uint16_t n = 0;
	uint32_t t0 = hal_millis();
	int c;

	(void)arg;
	while (n < count) {
		c = hal_rs485_getc();
		if (c >= 0) {
			buf[n++] = (uint8_t)c;
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

static int32_t port_write(const uint8_t* buf, uint16_t count, int32_t timeout_ms, void* arg) {
	(void)timeout_ms;
	(void)arg;
	hal_rs485_write(buf, count);
	return count;
}

/* nanoMODBUS keeps the frame in its own buffer until the next poll, which the
 * slave does not start while hal_rs485_tx_busy(): the buffer stays valid */
static int32_t port_send(const uint8_t* buf, uint16_t count, int32_t timeout_ms, void* arg) {
	(void)timeout_ms;
	(void)arg;
	hal_rs485_send(buf, count);
	return count;
}

void mb_port_conf(nmbs_platform_conf* conf) {
	nmbs_platform_conf_create(conf);
	conf->transport = NMBS_TRANSPORT_RTU;
	conf->read = port_read;
	conf->write = port_write;
}

void mb_port_conf_send(nmbs_platform_conf* conf) {
	mb_port_conf(conf);
	conf->write = port_send;
}
