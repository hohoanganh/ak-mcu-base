#include <string.h>

#include "crash_log.h"
#include "crc.h"
#include "hal.h"

typedef char crash_rec_size_check[(sizeof(crash_rec_t) == 20) ? 1 : -1];
typedef char crash_log_fit_check[(CRASH_LOG_NVM_OFFSET + CRASH_LOG_SLOTS * sizeof(crash_rec_t) <= HAL_NVM_SIZE) ? 1 : -1];

#define NO_SLOT		(0xFF)

static uint16_t rec_crc(const crash_rec_t* r) {
	return crc16_update(CRC16_INIT, r, sizeof(crash_rec_t) - 2);
}

static uint8_t slot_read(uint8_t slot, crash_rec_t* r) {
	return hal_nvm_read(CRASH_LOG_NVM_OFFSET + (uint32_t)slot * sizeof(crash_rec_t), r, sizeof(crash_rec_t)) == 0 &&
		   r->kind != CRASH_KIND_NONE &&
		   r->crc16 == rec_crc(r);
}

/* Slot of the newest valid record (NO_SLOT if the log is empty). seq wraps:
 * the newest is the one no other valid record is "just after". */
static uint8_t newest_slot(uint8_t* seq_out) {
	crash_rec_t r;
	uint8_t best = NO_SLOT;
	uint8_t best_seq = 0;

	for (uint8_t i = 0; i < CRASH_LOG_SLOTS; i++) {
		if (slot_read(i, &r)) {
			if (best == NO_SLOT || (int8_t)(r.seq - best_seq) > 0) {
				best = i;
				best_seq = r.seq;
			}
		}
	}
	if (seq_out) {
		*seq_out = best_seq;
	}
	return best;
}

int crash_log_add(crash_rec_t* rec) {
	crash_rec_t verify;
	uint8_t seq;
	uint8_t slot = newest_slot(&seq);
	uint32_t offset;

	if (slot == NO_SLOT) {
		slot = 0;
		rec->seq = 1;
	}
	else {
		slot = (uint8_t)((slot + 1) % CRASH_LOG_SLOTS);
		rec->seq = (uint8_t)(seq + 1);
	}
	rec->reserved = 0;
	rec->crc16 = rec_crc(rec);

	offset = CRASH_LOG_NVM_OFFSET + (uint32_t)slot * sizeof(crash_rec_t);
	if (hal_nvm_write(offset, rec, sizeof(crash_rec_t)) != 0) {
		return -1;
	}
	if (hal_nvm_read(offset, &verify, sizeof(verify)) != 0 || memcmp(&verify, rec, sizeof(verify)) != 0) {
		return -2;
	}
	return 0;
}

uint8_t crash_log_read(uint8_t n, crash_rec_t* out) {
	uint8_t seq;
	uint8_t slot = newest_slot(&seq);

	if (slot == NO_SLOT || n >= CRASH_LOG_SLOTS) {
		return 0;
	}
	/* older records sit in the slots before the newest; stop at the first
	 * gap in the sequence (empty, torn, or already overwritten) */
	slot = (uint8_t)((slot + CRASH_LOG_SLOTS - n) % CRASH_LOG_SLOTS);
	if (!slot_read(slot, out) || out->seq != (uint8_t)(seq - n)) {
		return 0;
	}
	return 1;
}

uint8_t crash_log_count(void) {
	crash_rec_t r;
	uint8_t seq;
	uint8_t slot;
	uint8_t n = 0;

	/* same walk as crash_log_read(n) for n = 0, 1, ... but the newest slot is
	 * located once, not once per record (8x fewer CRCs with a full log) */
	slot = newest_slot(&seq);
	while (slot != NO_SLOT && n < CRASH_LOG_SLOTS &&
		   slot_read((uint8_t)((slot + CRASH_LOG_SLOTS - n) % CRASH_LOG_SLOTS), &r) &&
		   r.seq == (uint8_t)(seq - n)) {
		n++;
	}
	return n;
}

void crash_log_clear(void) {
	crash_rec_t blank;

	memset(&blank, 0, sizeof(blank));
	for (uint8_t i = 0; i < CRASH_LOG_SLOTS; i++) {
		hal_nvm_write(CRASH_LOG_NVM_OFFSET + (uint32_t)i * sizeof(crash_rec_t), &blank, sizeof(blank));
	}
}

uint8_t crash_log_capture(void) {
	hal_crash_t c;
	crash_rec_t r;

	memset(&r, 0, sizeof(r));
	hal_last_dispatch(&r.task, &r.sig);

	if (hal_crash_take(&c)) {
		r.kind = (c.kind == HAL_CRASH_HARDFAULT) ? CRASH_KIND_HARDFAULT : CRASH_KIND_FATAL;
		r.code = c.code;
		r.pc = c.pc;
		r.lr = c.lr;
		r.info = c.info;
	}
	else if (hal_reset_reason() == HAL_RESET_REASON_WATCHDOG) {
		r.kind = CRASH_KIND_WATCHDOG;
	}
	else {
		return CRASH_KIND_NONE;
	}

	crash_log_add(&r);
	return r.kind;
}

const char* crash_kind_str(uint8_t kind) {
	switch (kind) {
	case CRASH_KIND_HARDFAULT:		return "hardfault";
	case CRASH_KIND_FATAL:			return "fatal";
	case CRASH_KIND_WATCHDOG:		return "watchdog";
	case CRASH_KIND_TASK_STALLED:	return "task stalled";
	default:						return "?";
	}
}
