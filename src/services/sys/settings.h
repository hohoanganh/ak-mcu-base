/**
 ******************************************************************************
 * @brief:  Product settings in NVM: one record the application defines
 *          (Modbus address, baud rate, ...), kept across resets and firmware
 *          updates, set from the shell or over Modbus in the field.
 *
 *  NVM map: [0, 64) boot_ctrl, [64, 224) crash log, [224, 256) this record:
 *    magic(2) version(1) len(1) data[26] crc16(2)
 *  version and len are the application's: a record of another layout (an
 *  older firmware) fails the check and the defaults are used, so a layout
 *  change is a version bump, nothing else. A record cut by a power loss
 *  fails its CRC the same way: the defaults come back, not half of each.
 *  Writes are rare (an operator saving a change), so one slot is enough;
 *  the port writes only the bytes that changed.
 ******************************************************************************
**/

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define SETTINGS_NVM_OFFSET		(224)
#define SETTINGS_NVM_SIZE		(32)
#define SETTINGS_DATA_MAX		(SETTINGS_NVM_SIZE - 6)

/* Fill data (len bytes, at most SETTINGS_DATA_MAX) from the record of this
 * version and length in NVM; from defaults (zeros if NULL) when there is
 * none. Returns 1 when the record was used, 0 for the defaults. */
extern uint8_t settings_load(uint8_t version, void* data, uint8_t len, const void* defaults);

/* Store data as the record. Nothing is written when NVM already holds it.
 * 0 = ok, -1 = bad argument, -2 = NVM write failed, -3 = read back differs. */
extern int settings_save(uint8_t version, const void* data, uint8_t len);

/* Remove the record: the next load gives the defaults. */
extern void settings_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __SETTINGS_H__ */
