/**
 ******************************************************************************
 * @brief:  Taking a block program from outside (scr_blocks.c). The machine
 *          itself is blk_vm.h.
 ******************************************************************************
**/

#ifndef __BLOCKS_H__
#define __BLOCKS_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

/* A program of len bytes is coming: the one that runs is stopped and dropped.
 * Returns 0 if it cannot fit. */
extern uint8_t blocks_begin(uint16_t len);

/* n bytes of it at offset off. Returns 0 outside the announced length. */
extern uint8_t blocks_data(uint16_t off, const uint8_t* data, uint16_t n);

/* All of it is there; crc = CRC-16 of the whole image. Returns BLK_OK and
 * keeps a copy in the media store, or BLK_ERR_IMAGE. The caller then opens
 * the Blocks screen, which starts the program. */
extern uint8_t blocks_end(uint16_t crc);

#ifdef __cplusplus
}
#endif

#endif /* __BLOCKS_H__ */
