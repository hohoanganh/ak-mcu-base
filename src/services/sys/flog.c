#include <string.h>

#include "flog.h"
#include "crc.h"

#define NO_SECTOR		(0xFF)

typedef struct {
	uint32_t magic;
	uint32_t seq;
	uint32_t nseq;
} flog_hdr_t;

typedef char flog_hdr_size_check[(sizeof(flog_hdr_t) == FLOG_HDR_SIZE) ? 1 : -1];

static uint32_t sector_off(const flog_t* l, uint8_t s) {
	return l->base + (uint32_t)s * l->sector_size;
}

static uint32_t slot_off(const flog_t* l, uint8_t s, uint16_t k) {
	return sector_off(l, s) + FLOG_HDR_SIZE + (uint32_t)k * (l->rec_size + 2U);
}

/* 1 = the sector carries a complete header, its seq in *seq */
static int hdr_read(const flog_t* l, uint8_t s, uint32_t* seq) {
	flog_hdr_t h;

	if (!l->ops->read(sector_off(l, s), &h, sizeof(h))) {
		return -1;
	}
	if (h.magic != FLOG_MAGIC || h.seq != ~h.nseq) {
		return 0;
	}
	*seq = h.seq;
	return 1;
}

static uint8_t all_blank(const uint8_t* p, uint32_t n) {
	while (n--) {
		if (*p++ != 0xFF) {
			return 0;
		}
	}
	return 1;
}

int flog_mount(flog_t* l, const flog_ops_t* ops, uint32_t base, uint32_t sector_size,
			   uint8_t sectors, uint8_t rec_size) {
	uint8_t buf[FLOG_REC_MAX + 2];
	uint32_t seq, best_seq = 0;
	uint8_t head = NO_SECTOR;
	int r;

	memset(l, 0, sizeof(*l));
	if (ops == 0 || sectors < 2 || rec_size == 0 || rec_size > FLOG_REC_MAX ||
			sector_size < FLOG_HDR_SIZE + rec_size + 2U) {
		return -1;
	}
	l->ops = ops;
	l->base = base;
	l->sector_size = sector_size;
	l->sectors = sectors;
	l->rec_size = rec_size;
	l->per_sector = (uint16_t)((sector_size - FLOG_HDR_SIZE) / (rec_size + 2U));

	/* the newest sector: highest seq, wrap-safe */
	for (uint8_t s = 0; s < sectors; s++) {
		r = hdr_read(l, s, &seq);
		if (r < 0) {
			return -1;
		}
		if (r && (head == NO_SECTOR || (int32_t)(seq - best_seq) > 0)) {
			head = s;
			best_seq = seq;
		}
	}
	l->mounted = 1;
	if (head == NO_SECTOR) {
		return 0;		/* empty */
	}
	l->head = head;
	l->head_seq = best_seq;

	/* the records before it: the run of sectors with consecutive seq ending at head */
	l->used = 1;
	for (uint8_t s = head; l->used < sectors; ) {
		uint8_t p = (uint8_t)((s + sectors - 1) % sectors);

		r = hdr_read(l, p, &seq);
		if (r < 0) {
			return -1;
		}
		if (!r || seq != l->head_seq - l->used) {
			break;
		}
		l->used++;
		s = p;
	}

	/* records in the head sector: up to the first blank slot */
	for (l->head_n = 0; l->head_n < l->per_sector; l->head_n++) {
		if (!ops->read(slot_off(l, head, l->head_n), buf, rec_size + 2U)) {
			return -1;
		}
		if (all_blank(buf, rec_size + 2U)) {
			break;
		}
	}
	return 0;
}

int flog_append(flog_t* l, const void* rec) {
	uint8_t buf[FLOG_REC_MAX + 2];
	uint16_t crc;

	if (!l->mounted) {
		return -1;
	}
	if (l->used == 0 || l->head_n >= l->per_sector) {
		/* next sector: the oldest one when the ring is full. Erase, then the
		 * header; a power loss between the two leaves no header, and the
		 * next append erases it again. */
		uint8_t next = l->used ? (uint8_t)((l->head + 1) % l->sectors) : 0;
		uint32_t seq = l->used ? l->head_seq + 1 : 1;
		flog_hdr_t h = { FLOG_MAGIC, seq, ~seq };

		if (!l->ops->erase(sector_off(l, next)) || !l->ops->write(sector_off(l, next), &h, sizeof(h))) {
			return -1;
		}
		l->head = next;
		l->head_seq = seq;
		l->head_n = 0;
		if (l->used < l->sectors) {
			l->used++;
		}
	}

	memcpy(buf, rec, l->rec_size);
	crc = crc16_update(CRC16_INIT, rec, l->rec_size);
	buf[l->rec_size] = (uint8_t)crc;
	buf[l->rec_size + 1] = (uint8_t)(crc >> 8);
	if (!l->ops->write(slot_off(l, l->head, l->head_n), buf, l->rec_size + 2U)) {
		return -1;
	}
	l->head_n++;
	return 0;
}

uint32_t flog_count(const flog_t* l) {
	return (l->mounted && l->used) ? (uint32_t)(l->used - 1) * l->per_sector + l->head_n : 0;
}

int flog_read(const flog_t* l, uint32_t i, void* rec) {
	uint8_t buf[FLOG_REC_MAX + 2];
	uint8_t oldest, s;
	uint16_t k, crc;

	if (i >= flog_count(l)) {
		return -1;
	}
	oldest = (uint8_t)((l->head + l->sectors - (l->used - 1)) % l->sectors);
	s = (uint8_t)((oldest + i / l->per_sector) % l->sectors);
	k = (uint16_t)(i % l->per_sector);
	if (!l->ops->read(slot_off(l, s, k), buf, l->rec_size + 2U)) {
		return -1;
	}
	crc = (uint16_t)(buf[l->rec_size] | (buf[l->rec_size + 1] << 8));
	if (all_blank(buf, l->rec_size + 2U) || crc != crc16_update(CRC16_INIT, buf, l->rec_size)) {
		return 0;
	}
	memcpy(rec, buf, l->rec_size);
	return 1;
}

int flog_clear(flog_t* l) {
	int ret = 0;

	if (!l->mounted) {
		return -1;
	}
	for (uint8_t n = 0; n < l->used; n++) {
		uint8_t s = (uint8_t)((l->head + l->sectors - n) % l->sectors);

		if (!l->ops->erase(sector_off(l, s))) {
			ret = -1;
		}
	}
	l->used = 0;
	l->head_n = 0;
	return ret;
}
