/**
 ******************************************************************************
 * @brief:  System task: heartbeat, hardware watchdog and task liveness.
 *
 *  Every heartbeat it pings all tasks (AK_SIG_PING) and looks at who handled
 *  a message since the last one:
 *   - a handler that never returns keeps this task from running: the hardware
 *     watchdog resets the chip, and the crash log names the task that was
 *     running;
 *   - a task that gets no CPU time (starved by a higher priority one, queue
 *     blocked) misses its pings: after APP_TASK_STALL_ROUNDS it is logged as
 *     stalled and the chip is reset.
 *  This task therefore has the highest priority of the application.
 ******************************************************************************
**/

#include "ak.h"
#include "task.h"
#include "timer.h"

#include "hal.h"
#include "ak_log.h"
#include "crash_log.h"

#include "app.h"
#include "task_list.h"

#define TAG "SYS"

static uint8_t missed[AK_TASK_EOT_ID];

uint8_t app_led_held;

static void ping_tasks(void) {
	for (uint8_t id = 0; id < AK_TASK_EOT_ID; id++) {
		if (id != TASK_SYSTEM_ID) {
			task_post_pure_msg(id, AK_SIG_PING);
		}
	}
}

static void task_stalled(uint8_t id) {
	crash_rec_t r = { 0 };

	r.kind = CRASH_KIND_TASK_STALLED;
	r.code = id;
	r.task = AK_TASK_IDLE_ID;
	crash_log_add(&r);

	LOG_E(TAG, "task %d stalled, reset\n", id);
	hal_reset();
}

void task_system(ak_msg_t* msg) {
	uint32_t alive;

	switch (msg->sig) {
	case SYSTEM_SIG_INIT:
		hal_wdt_start(APP_WDT_TIMEOUT_MS);
		timer_set(TASK_SYSTEM_ID, SYSTEM_SIG_HEARTBEAT, APP_HEARTBEAT_MS, TIMER_PERIODIC);
		LOG_I(TAG, "watchdog %d ms, heartbeat %d ms\n", APP_WDT_TIMEOUT_MS, APP_HEARTBEAT_MS);
		task_alive_take();
		ping_tasks();
		break;

	case SYSTEM_SIG_HEARTBEAT:
		alive = task_alive_take();
		for (uint8_t id = 0; id < AK_TASK_EOT_ID; id++) {
			if (id == TASK_SYSTEM_ID || (alive & ((uint32_t)1 << id))) {
				missed[id] = 0;
			}
			else if (++missed[id] >= APP_TASK_STALL_ROUNDS) {
				task_stalled(id);
			}
		}

		/* Kick from a task, not an ISR: a task hogging the CPU delays the
		 * heartbeat and the watchdog resets the chip. */
		hal_wdt_kick();
		if (!app_led_held) {
			hal_led_toggle();
		}
		ping_tasks();
		break;

	default:
		break;
	}
}
