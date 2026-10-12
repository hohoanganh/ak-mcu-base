/**
 ******************************************************************************
 * @brief:  Record log on flash: a ring of sectors holding fixed-size records,
 *          append-only, safe against power loss, no RAM index.
 *
 *  Layout of one sector:  [magic 4][seq 4][~seq 4][slot 0][slot 1]...
 *  where a slot is [record rec_size][crc16 2]. The sector with the highest
 *  seq is being written; a new record goes into its first blank slot. When
 *  it is full the next sector (the oldest) is erased and gets seq + 1.
 *
 *  Power loss: a record cut in the middle fails its CRC and reads as "torn"
 *  (flog_read() returns 0), the slot after it is still blank and the log
 *  goes on. A header cut in the middle fails magic / ~seq and the sector is
 *  erased again on the next append. Nothing is cached that is not rebuilt by
 *  flog_mount() from the flash itself.
 *
 *  Backend: three functions with the shape of kit_store_*() (SPI NOR, erased
 *  bytes read 0xFF, byte writes). A backend that programs in words must pick
 *  rec_size so that rec_size + 2 is a multiple of its write unit.
 *
 *  Cost: mounting reads every sector header and the slots of the newest
 *  sector; appending is one write, plus one erase per sector_size bytes.
 ******************************************************************************
**/

#ifndef __FLOG_H__
#define __FLOG_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define FLOG_MAGIC			(0x474F4C46UL)	/* "FLOG" */
#define FLOG_HDR_SIZE		(12)
#define FLOG_REC_MAX		(60)			/* record bytes; slot = rec + 2 (crc) */

typedef struct {
	uint8_t (*read)(uint32_t off, void* buf, uint32_t len);		/* 1 = ok */
	uint8_t (*write)(uint32_t off, const void* data, uint32_t len);
	uint8_t (*erase)(uint32_t off);								/* one sector starting at off */
} flog_ops_t;

typedef struct {
	const flog_ops_t* ops;
	uint32_t base;			/* first sector, offset for ops */
	uint32_t sector_size;
	uint8_t  sectors;		/* 2 or more */
	uint8_t  rec_size;		/* 1..FLOG_REC_MAX */
	uint16_t per_sector;	/* slots in one sector */
	/* rebuilt by flog_mount() */
	uint8_t  mounted;
	uint8_t  used;			/* sectors holding records: head and the ones before it */
	uint8_t  head;			/* sector being written */
	uint16_t head_n;		/* records in it */
	uint32_t head_seq;
} flog_t;

/* Read the flash and rebuild the state. Returns 0 on success, -1 on a bad
 * argument or a read error. An empty or foreign region mounts as empty. */
extern int flog_mount(flog_t* l, const flog_ops_t* ops, uint32_t base, uint32_t sector_size,
					  uint8_t sectors, uint8_t rec_size);

/* Append one record of rec_size bytes. Returns 0 on success. */
extern int flog_append(flog_t* l, const void* rec);

/* Records in the log, torn ones included; the oldest ones are dropped as the
 * ring wraps. */
extern uint32_t flog_count(const flog_t* l);

/* Record i (0 = oldest). Returns 1 and fills rec, 0 for a torn or blank
 * record (skip it), -1 when i is out of range or the read failed. */
extern int flog_read(const flog_t* l, uint32_t i, void* rec);

/* Erase every sector holding records. Returns 0 on success. */
extern int flog_clear(flog_t* l);

#ifdef __cplusplus
}
#endif

#endif /* __FLOG_H__ */
