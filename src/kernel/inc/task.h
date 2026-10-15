/**
 ******************************************************************************
 * @author: GaoKong
 * @date:   13/08/2016
 * Mechanism of task scheduler is referenced by doc/Samek0607.pdf
 ******************************************************************************
**/

#ifndef __TASK_H__
#define __TASK_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stdbool.h>

#include "ak.h"
#include "ak_port.h"
#include "message.h"

typedef uint8_t	task_pri_t;
typedef uint8_t	task_id_t;
typedef void (*pf_task)(ak_msg_t*);
typedef void (*pf_task_polling)();

typedef struct {
	task_id_t id;
	task_pri_t pri;
	pf_task task;
} task_t;

typedef struct {
	task_id_t id;
	uint8_t ability;
	pf_task_polling task_polling;
} task_polling_t;

extern void task_create(task_t* task_tbl);
extern void task_post(task_id_t des_task_id, ak_msg_t* msg);
extern void task_post_pure_msg(task_id_t des_task_id, uint8_t sig);
extern void task_post_common_msg(task_id_t des_task_id, uint8_t sig, uint8_t* data, uint8_t len);
/* only with AK_DYNAMIC_MSG_POOL_SIZE > 0 (message.h) */
extern void task_post_dynamic_msg(task_id_t des_task_id, uint8_t sig, uint8_t* data, uint32_t len);
/* Same as task_post_pure_msg() / task_post_common_msg(), but a full pool
 * returns 0 instead of FATAL: for messages made from outside data (UART
 * lines, network frames) that may come faster than they are handled.
 * Returns 1 when posted. */
extern uint8_t task_post_pure_msg_try(task_id_t des_task_id, uint8_t sig);
extern uint8_t task_post_common_msg_try(task_id_t des_task_id, uint8_t sig, uint8_t* data, uint8_t len);
extern uint8_t task_remove_msg(task_id_t task_id, uint8_t sig); /* return numbers message's removed */
extern int task_init();
extern int task_run(); /* endless loop: task_run_once() + ak_port_idle() */
extern uint8_t task_run_once(); /* drain pending messages + one polling pass; returns messages handled */

/* Bit n set = task id n (0..31) handled at least one message since the last
 * call. Reading clears the set. */
extern uint32_t task_alive_take();

extern void task_polling_create(task_polling_t* task_polling_tbl);
extern void task_polling_set_ability(task_id_t task_polling_id, uint8_t ability);
extern void task_polling_run();

extern void task_entry_interrupt(); /* this function MUST-BE called when entry interrupt handler */
extern void task_exit_interrupt(); /* this function MUST-BE called when exit interrupt handler */

extern task_id_t task_self(); /* get current task id, except interrupt task */
extern task_id_t get_current_task_id(); /* get current task id include interrupt task */
extern task_t* get_current_task_info(); /* get current task info */
extern ak_msg_t* get_current_active_object(); /* get current active object info (active message) */

extern void task_irq_io_entry_trigger(); /* this function MUST-BE redefine */
extern void task_irq_io_exit_trigger() ; /* this function MUST-BE redefine */

extern void task_pri_queue_dump(); /* use only for debug */

/* Trace hook (weak, empty by default). Called after each handled message when
 * AK_TASK_TRACE_ENABLE is defined. exe_ms = handler run time. */
extern void ak_task_trace_hook(const ak_msg_t* msg, uint32_t exe_ms);

#ifdef __cplusplus
}
#endif

#endif //__TASK_H__
