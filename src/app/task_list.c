#include "task_list.h"
#include "timer.h"
#if defined(APP_KIT_DEMO)
#include "ui.h"
#endif
#if defined(APP_REMOTE)
#include "remote.h"
#endif

task_t app_task_table[] = {
	/*************************************************************************/
	/* SYSTEM TASK */
	/*************************************************************************/
	{TASK_TIMER_TICK_ID,	TASK_PRI_LEVEL_7,		task_timer_tick	},

	/*************************************************************************/
	/* APP TASK */
	/*************************************************************************/
	/* above every app task: it must still run when one of them hogs the CPU */
	{TASK_SYSTEM_ID,		TASK_PRI_LEVEL_6,		task_system		},
	{TASK_CONSOLE_ID,		TASK_PRI_LEVEL_3,		task_console	},
	{TASK_FW_ID,			TASK_PRI_LEVEL_4,		task_fw			},
#if defined(APP_KIT_DEMO)
	/* below the console and the update task: a frame may take 20 ms */
	{TASK_UI_ID,			TASK_PRI_LEVEL_2,		task_ui			},
#endif
#if defined(APP_REMOTE)
	/* above the console: a packet every 8 ms must not wait for a shell command */
	{TASK_REMOTE_ID,		TASK_PRI_LEVEL_5,		task_remote		},
#endif

	/*************************************************************************/
	/* END OF TABLE */
	/*************************************************************************/
	{AK_TASK_EOT_ID,		TASK_PRI_LEVEL_0,		(pf_task)0		}
};

task_polling_t app_task_polling_table[] = {
	{TASK_POLL_CONSOLE_ID,	AK_ENABLE,		task_poll_console	},
#if defined(APP_MODBUS_SLAVE) || defined(APP_MODBUS_GATEWAY)
	{TASK_POLL_MODBUS_ID,	AK_ENABLE,		task_poll_modbus	},
#else
	{TASK_POLL_MODBUS_ID,	AK_DISABLE,		task_poll_modbus	},
#endif
#if defined(APP_KIT_DEMO)
	{TASK_POLL_BUTTONS_ID,	AK_ENABLE,		task_poll_buttons	},
#endif
#if defined(APP_REMOTE)
	{TASK_POLL_REMOTE_ID,	AK_ENABLE,		task_poll_remote	},
#endif
	{AK_TASK_POLLING_EOT_ID,AK_DISABLE,		(pf_task_polling)0	},
};
