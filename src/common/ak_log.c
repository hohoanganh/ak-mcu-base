#include "ak_log.h"

void ak_log_hdr(char lvl, const char* tag) {
	xprintf("[%8u] %c %s: ", ak_port_millis(), lvl, tag);
}
