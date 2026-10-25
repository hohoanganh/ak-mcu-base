/**
 ******************************************************************************
 * @brief:  Application task list. The kernel includes this file for
 *          TASK_TIMER_TICK_ID, AK_TASK_EOT_ID, AK_TASK_POLLING_EOT_ID.
 *
 *  Adding a task: add the ID to the enum and a row to app_task_table
 *  (task_list.c) at the SAME position. task_create() FATALs if id != index.
 ******************************************************************************
**/

#ifndef __TASK_LIST_H__
#define __TASK_LIST_H__

#include "ak.h"
#include "task.h"

enum {
	/* KERNEL DEFINE TASK */
	TASK_TIMER_TICK_ID,

	/* APP DEFINE TASK */
	TASK_SYSTEM_ID,
	TASK_CONSOLE_ID,
	TASK_FW_ID,
#if defined(APP_KIT_DEMO)
	TASK_UI_ID,				/* demo/: display, buttons, games */
#endif
#if defined(APP_REMOTE)
	TASK_REMOTE_ID,			/* remote/: bench remote for a toy drone */
#endif
#if defined(APP_RC_TX) || defined(APP_RC_RX)
	TASK_RC_ID,				/* rc/: radio link of the remote / receiver */
	TASK_RC_UI_ID,			/* rc/: its display */
#endif

	/* EOT task ID */
	AK_TASK_EOT_ID,
};

enum {
	TASK_POLL_CONSOLE_ID,
	TASK_POLL_MODBUS_ID,
#if defined(APP_KIT_DEMO)
	TASK_POLL_BUTTONS_ID,
#endif
#if defined(APP_REMOTE)
	TASK_POLL_REMOTE_ID,
#endif
#if defined(APP_RC_TX) || defined(APP_RC_RX)
	TASK_POLL_RC_ID,
#endif

	/* EOT polling task ID */
	AK_TASK_POLLING_EOT_ID,
};

extern task_t app_task_table[];
extern task_polling_t app_task_polling_table[];

/* task handler */
extern void task_system(ak_msg_t* msg);
extern void task_console(ak_msg_t* msg);
extern void task_fw(ak_msg_t* msg);

/* polling handler */
extern void task_poll_console(void);
extern void task_poll_modbus(void);

#endif /* __TASK_LIST_H__ */
