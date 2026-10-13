#include <string.h>

#include "boot_ctrl.h"
#include "crc.h"
#include "hal.h"

#define BOOT_CTRL_SLOT_NUM		(2)
/* NVM [0, 64). Fixed: the bootloader in the field and every later app must
 * agree on it. The rest of the NVM belongs to other services (crash log). */
#define BOOT_CTRL_SLOT_SIZE		(32)
#define BOOT_CTRL_NO_SLOT		(0xFF)

typedef char boot_ctrl_size_check[(sizeof(boot_ctrl_t) <= BOOT_CTRL_SLOT_SIZE) ? 1 : -1];

static uint32_t boot_ctrl_crc(const boot_ctrl_t* ctrl) {
	return crc32_update(CRC32_INIT, ctrl, sizeof(boot_ctrl_t) - 4);
}

static uint8_t slot_read(uint8_t slot, boot_ctrl_t* ctrl) {
	return hal_nvm_read((uint32_t)slot * BOOT_CTRL_SLOT_SIZE, ctrl, sizeof(boot_ctrl_t)) == 0 &&
		   ctrl->magic == BOOT_CTRL_MAGIC &&
		   ctrl->crc32 == boot_ctrl_crc(ctrl);
}

/* Newest valid slot (BOOT_CTRL_NO_SLOT if none), its content in *ctrl. */
static uint8_t newest_slot(boot_ctrl_t* ctrl) {
	boot_ctrl_t s[BOOT_CTRL_SLOT_NUM];
	uint8_t ok0 = slot_read(0, &s[0]);
	uint8_t ok1 = slot_read(1, &s[1]);
	uint8_t slot;

	if (ok0 && ok1) {
		/* wrap-safe: the slots differ by one step */
		slot = ((int8_t)(s[1].seq - s[0].seq) > 0) ? 1 : 0;
	}
	else if (ok0) {
		slot = 0;
	}
	else if (ok1) {
		slot = 1;
	}
	else {
		return BOOT_CTRL_NO_SLOT;
	}

	memcpy(ctrl, &s[slot], sizeof(boot_ctrl_t));
	return slot;
}

void boot_ctrl_load(boot_ctrl_t* ctrl) {
	if (newest_slot(ctrl) == BOOT_CTRL_NO_SLOT) {
		memset(ctrl, 0, sizeof(boot_ctrl_t));
		ctrl->magic = BOOT_CTRL_MAGIC;
		ctrl->cmd = BOOT_CMD_NONE;
	}
}

int boot_ctrl_save(boot_ctrl_t* ctrl) {
	boot_ctrl_t cur;
	boot_ctrl_t verify;
	uint8_t slot = newest_slot(&cur);
	uint32_t offset;

	if (slot == BOOT_CTRL_NO_SLOT) {
		slot = 0;
		ctrl->seq = 1;
	}
	else {
		slot ^= 1;		/* never overwrite the newest valid state */
		ctrl->seq = (uint8_t)(cur.seq + 1);
	}
	offset = (uint32_t)slot * BOOT_CTRL_SLOT_SIZE;

	ctrl->magic = BOOT_CTRL_MAGIC;
	ctrl->crc32 = boot_ctrl_crc(ctrl);

	if (hal_nvm_write(offset, ctrl, sizeof(boot_ctrl_t)) != 0) {
		return -1;
	}

	/* read back to confirm the NVM write */
	if (hal_nvm_read(offset, &verify, sizeof(verify)) != 0 ||
			memcmp(&verify, ctrl, sizeof(verify)) != 0) {
		return -2;
	}

	return 0;
}

int boot_ctrl_set_cmd(uint8_t cmd) {
	boot_ctrl_t ctrl;

	boot_ctrl_load(&ctrl);
	if (ctrl.cmd == cmd) {
		return 0;
	}
	ctrl.cmd = cmd;
	return boot_ctrl_save(&ctrl);
}

int boot_ctrl_confirm(void) {
	boot_ctrl_t ctrl;

	boot_ctrl_load(&ctrl);
	if (!ctrl.trial) {
		return 0;
	}
	ctrl.trial = 0;
	ctrl.trial_boots = 0;
	return boot_ctrl_save(&ctrl) == 0 ? 1 : -1;
}
