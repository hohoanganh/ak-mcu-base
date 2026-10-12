/**
 ******************************************************************************
 * @brief:  Firmware download protocol over a byte stream (UART, RS485...).
 *          Shared by the bootloader (loader mode) and the app (OTA).
 *
 * Frame:    A5 | cmd | seq | len(LE16) | payload[len] | crc16(LE)
 *           crc16-CCITT (init 0xFFFF) over cmd..payload.
 * Response: cmd | 0x80, same seq, payload[0] = fw_err_t.
 *
 * 0xA5 is not ASCII, so the protocol shares one UART with the text shell:
 * fw_proto_feed() returns 0 for bytes outside a frame -> pass them to the shell.
 *
 *  cmd  name     request payload       response payload (after status)
 *  01   INFO     -                     proto, role, ver(8), board(16),
 *                                      staging_size(4), max_chunk(2),
 *                                      state(1), written(4)
 *  02   BEGIN    total(4)              -
 *  03   DATA     offset(4) data[<=128] written(4)
 *  04   END      -                     ver(8), img_size(4)
 *  05   INSTALL  -                     -   (then: install image + run)
 *  06   LOADER   -                     -   (app: reset into bootloader)
 *  07   RESET    -                     -
 *  08   RUN      -                     -   (boot: leave loader, run app)
 *  09   CONFIRM  -                     was_on_trial[1]  (end the trial of the running image)
 *  0A   ROLLBACK -                     -   (then: install the image before the last update)
 *  40.. application commands, handled by cfg->ext if set (else "unknown cmd")
 ******************************************************************************
**/

#ifndef __FW_PROTO_H__
#define __FW_PROTO_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#include "fw_types.h"

#define FW_PROTO_VERSION		(1)
#define FW_PROTO_SOF			(0xA5)
#define FW_PROTO_MAX_CHUNK		(128)
#define FW_PROTO_MAX_PAYLOAD	(4 + FW_PROTO_MAX_CHUNK)
#define FW_PROTO_BYTE_TIMEOUT	(500)	/* max ms between bytes of one frame */

#define FW_PROTO_CMD_INFO		(0x01)
#define FW_PROTO_CMD_BEGIN		(0x02)
#define FW_PROTO_CMD_DATA		(0x03)
#define FW_PROTO_CMD_END		(0x04)
#define FW_PROTO_CMD_INSTALL	(0x05)
#define FW_PROTO_CMD_LOADER		(0x06)
#define FW_PROTO_CMD_RESET		(0x07)
#define FW_PROTO_CMD_RUN		(0x08)
#define FW_PROTO_CMD_CONFIRM	(0x09)	/* end the trial of the running image (boot_ctrl_confirm) */
#define FW_PROTO_CMD_ROLLBACK	(0x0A)	/* put the image before the last install back */
#define FW_PROTO_CMD_EXT		(0x40)	/* first command passed to cfg->ext */
#define FW_PROTO_EXT_RESP_MAX	(32)	/* response payload after the status byte */
#define FW_PROTO_RESP			(0x80)

#define FW_ROLE_BOOT			(0)
#define FW_ROLE_APP				(1)

typedef enum {
	FW_PROTO_ACT_NONE = 0,
	FW_PROTO_ACT_INSTALL,	/* verified image in STAGING, install it */
	FW_PROTO_ACT_LOADER,
	FW_PROTO_ACT_RESET,
	FW_PROTO_ACT_RUN,
	FW_PROTO_ACT_ROLLBACK,	/* app: set BOOT_CMD_ROLLBACK and reset; boot: roll back now */
} fw_proto_action_t;

typedef struct {
	uint8_t role;
	const fw_version_t* version;	/* running firmware version */
	void (*tx)(const uint8_t* data, uint32_t len);
	/* Optional: commands >= FW_PROTO_CMD_EXT. Returns the status byte
	 * (fw_err_t), may put up to FW_PROTO_EXT_RESP_MAX bytes into resp and
	 * their number into *resp_len (0 on entry). */
	uint8_t (*ext)(uint8_t cmd, const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t* resp_len);
} fw_proto_cfg_t;

extern void fw_proto_init(const fw_proto_cfg_t* cfg);

/* Feed one byte. Returns 1 if consumed by the protocol, 0 otherwise (shell). */
extern uint8_t fw_proto_feed(uint8_t byte, uint32_t now_ms);

/* Inside a frame? (shell should yield) */
extern uint8_t fw_proto_busy(void);

/* Fetch (and clear) the action to perform after the response was sent. */
extern fw_proto_action_t fw_proto_take_action(void);

/* Helper: build a frame into out (>= len + 7 bytes), returns its length. */
extern uint32_t fw_proto_pack(uint8_t* out, uint8_t cmd, uint8_t seq, const uint8_t* payload, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __FW_PROTO_H__ */
