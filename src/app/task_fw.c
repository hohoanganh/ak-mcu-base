/**
 ******************************************************************************
 * @brief:  App-side OTA. Images arrive via fw_proto (task_console) into
 *          STAGING; this task only does the last step: tell the bootloader
 *          to install, then reset. Other transports (Modbus, RF...) just call
 *          fw_update_*() and post FW_SIG_INSTALL here.
 ******************************************************************************
**/

#include "ak.h"
#include "task.h"
#include "timer.h"

#include "hal.h"
#include "ak_log.h"
#include "boot_ctrl.h"
#include "fw_update.h"
#include "hal_flash.h"

#include "app.h"
#include "task_list.h"

#define TAG "FW"

/* let the console flush logs + response before reset */
#define FW_RESET_DELAY_MS		(100)

static void schedule_reset(void) {
	timer_set(TASK_FW_ID, FW_SIG_DO_RESET, FW_RESET_DELAY_MS, TIMER_ONE_SHOT);
}

void task_fw(ak_msg_t* msg) {
	switch (msg->sig) {
	case FW_SIG_INSTALL:
		if (fw_update_state() != FW_UPDATE_READY) {
			LOG_W(TAG, "install ignored: no verified image\n");
			break;
		}
		if (boot_ctrl_set_cmd(BOOT_CMD_UPDATE) != 0) {
			LOG_E(TAG, "boot_ctrl write failed\n");
			break;
		}
		LOG_I(TAG, "new image ready, rebooting to install\n");
		schedule_reset();
		break;

	case FW_SIG_LOADER:
		if (boot_ctrl_set_cmd(BOOT_CMD_LOADER) != 0) {
			LOG_E(TAG, "boot_ctrl write failed\n");
			break;
		}
		LOG_I(TAG, "rebooting into bootloader loader\n");
		schedule_reset();
		break;

	case FW_SIG_ROLLBACK:
		if (hal_flash_info(FLASH_PART_BACKUP)->size == 0) {
			LOG_W(TAG, "rollback ignored: this board keeps no backup image\n");
			break;
		}
		if (boot_ctrl_set_cmd(BOOT_CMD_ROLLBACK) != 0) {
			LOG_E(TAG, "boot_ctrl write failed\n");
			break;
		}
		LOG_I(TAG, "rebooting to roll back to the previous image\n");
		schedule_reset();
		break;

	case FW_SIG_RESET:
		LOG_I(TAG, "reset\n");
		schedule_reset();
		break;

	case FW_SIG_DO_RESET:
		hal_console_flush();
		hal_reset();
		break;

#if APP_CRASH_TEST
	case FW_SIG_TEST_STARVE:
		/* always ready again: tasks below this priority get no CPU time */
		task_post_pure_msg(TASK_FW_ID, FW_SIG_TEST_STARVE);
		break;
#endif

	default:
		break;
	}
}
