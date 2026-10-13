/**
 ******************************************************************************
 * @brief:  Boot <-> app shared control block (in NVM, survives reset).
 *
 *  App update: write image to STAGING, verify, set cmd = BOOT_CMD_UPDATE,
 *  reset. Boot sees UPDATE -> copy APP to BACKUP -> verify STAGING -> copy
 *  to APP -> verify -> clear cmd, trial = 1 -> run app. Power loss midway:
 *  cmd is still set and STAGING is intact -> next boot copies again. NVM is
 *  NOT written on every boot (endurance).
 *
 *  Trial and rollback: a freshly installed image runs "on trial". It calls
 *  boot_ctrl_confirm() once it has proven itself (the sample app does so
 *  after APP_FW_CONFIRM_MS of normal operation). The bootloader counts the
 *  boots of an unconfirmed image; at BOOT_MAX_TRIAL_BOOTS it puts the BACKUP
 *  image back (rollback). The app can also ask for that with BOOT_CMD_ROLLBACK.
 *
 *  The block is kept in two NVM slots written alternately. A save that is cut
 *  by a power loss damages only the slot being written: the previous state is
 *  still there, instead of everything falling back to defaults.
 ******************************************************************************
**/

#ifndef __BOOT_CTRL_H__
#define __BOOT_CTRL_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define BOOT_CTRL_MAGIC			(0x4C54434BUL)	/* "KCTL" */

#define BOOT_CMD_NONE			(0)
#define BOOT_CMD_UPDATE			(1)	/* install image from STAGING, then run */
#define BOOT_CMD_LOADER			(2)	/* stay in bootloader, wait for UART download */
#define BOOT_CMD_ROLLBACK		(3)	/* put the BACKUP image (the one before the last install) back */

typedef struct {
	uint32_t magic;
	uint8_t  cmd;
	uint8_t  install_attempts;	/* consecutive unfinished install attempts */
	uint8_t  last_result;		/* fw_err_t of the last install */
	uint8_t  seq;				/* slot sequence, set by boot_ctrl_save() */
	uint32_t install_count;		/* total successful installs */
	uint8_t  trial;				/* 1: APP was installed and has not confirmed itself yet */
	uint8_t  trial_boots;		/* boots of the unconfirmed image so far */
	uint8_t  rolled_back;		/* 1: APP is the image a rollback restored */
	uint8_t  reserved;
	uint32_t crc32;
} boot_ctrl_t;

/* Load the newest valid slot; blank/corrupt NVM yields defaults (cmd NONE). */
extern void boot_ctrl_load(boot_ctrl_t* ctrl);
/* Update seq + crc and write to the slot not holding the newest state.
 * Returns 0 on success. */
extern int boot_ctrl_save(boot_ctrl_t* ctrl);
/* Helper for the app: load, change cmd, save. */
extern int boot_ctrl_set_cmd(uint8_t cmd);
/* The running image is fine: end its trial. Returns 1 if it was on trial,
 * 0 if there was nothing to confirm, < 0 on an NVM error. */
extern int boot_ctrl_confirm(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_CTRL_H__ */
