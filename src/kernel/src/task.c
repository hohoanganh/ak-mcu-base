/**
 ******************************************************************************
 * @author: GaoKong
 * @date:   13/08/2016
 * Mechanism of task scheduler is referenced by doc/Samek0607.pdf
 *
 * Portable version (ak-mcu-base): no sys_dbg/sys_ctrl/log_queue dependency,
 * all chip access goes through ak_port.h. Changes vs. original:
 *   - get_current_task_id() returns des_task_id while a task runs (original
 *     returned if_des_task_id -> wrong src_task_id on messages sent by tasks).
 *   - LOG2LKUP(0) guarded (UB on host).
 *   - task_run_once() added for host tests / simulation.
 *   - outside a task the current id is AK_TASK_IDLE_ID (original kept the
 *     last task after the first interrupt -> wrong src_task_id from polling).
 *   - task_run() checks the ready set and idles with interrupts masked, so a
 *     message posted by an ISR in between is not left waiting for the next IRQ.
 ******************************************************************************
**/

#include "ak.h"

#include "task.h"
#include "timer.h"
#include "message.h"

#include "task_list.h"

typedef struct {
	task_pri_t  pri;
	uint8_t     mask;
	ak_msg_t*   qhead;
	ak_msg_t*   qtail;
} tcb_t;

static task_id_t current_task_id = AK_TASK_IDLE_ID;
static task_t current_task_info;
static ak_msg_t current_active_object;

static tcb_t	task_pri_queue[TASK_PRI_MAX_SIZE];
static task_t*	task_table = (task_t*)0;
static uint8_t	task_table_size = 0;
static uint8_t	task_current = 0;
static uint8_t	task_ready = 0;
static uint32_t	task_alive = 0;

static task_polling_t* task_polling_table = (task_polling_t*)0;
static uint8_t	task_polling_table_size = 0;

static uint8_t task_sheduler();

/* function MUST-BE redefine */
__AK_WEAK void task_irq_io_entry_trigger() {
}

/* function MUST-BE redefine */
__AK_WEAK void task_irq_io_exit_trigger() {
}

__AK_WEAK void ak_task_trace_hook(const ak_msg_t* msg, uint32_t exe_ms) {
	(void)msg;
	(void)exe_ms;
}

void task_create(task_t* task_tbl) {
	uint8_t idx = 0;
	if (task_tbl) {
		task_table = task_tbl;
		while (task_tbl[idx].id != AK_TASK_EOT_ID) {
			/* task_post()/task_sheduler() index task_table[] by task id,
			 * so row idx must have id == idx. */
			if (task_tbl[idx].id != idx) {
				FATAL("TK", 0x08);
			}
			/* valid pri is 1..TASK_PRI_MAX_SIZE (pri 0 -> index -1) */
			if (task_tbl[idx].pri < 1 || task_tbl[idx].pri > TASK_PRI_MAX_SIZE) {
				FATAL("TK", 0x09);
			}
			idx++;
		}
		task_table_size = idx;
	}
	else {
		FATAL("TK", 0x01);
	}
}

void task_polling_create(task_polling_t* task_polling_tbl) {
	uint8_t idx = 0;
	if (task_polling_tbl) {
		task_polling_table = task_polling_tbl;
		while (task_polling_tbl[idx].id != AK_TASK_POLLING_EOT_ID) {
			idx++;
		}
		task_polling_table_size = idx;
	}
	else {
		FATAL("TK", 0x06);
	}
}

void task_post(task_id_t des_task_id, ak_msg_t* msg) {
	tcb_t* t_tcb;

	if (des_task_id >= task_table_size) {
		FATAL("TK", 0x02);
	}

	t_tcb = &task_pri_queue[task_table[des_task_id].pri - 1];

	ENTRY_CRITICAL();

	msg->next = AK_MSG_NULL;
	msg->des_task_id = des_task_id;

	if (t_tcb->qtail == AK_MSG_NULL) {
		/* put message to queue */
		t_tcb->qtail = msg;
		t_tcb->qhead = msg;

		/* change status task to ready*/
		task_ready |= t_tcb->mask;
	}
	else {
		/* put message to queue */
		t_tcb->qtail->next = msg;
		t_tcb->qtail = msg;
	}

	EXIT_CRITICAL();
}

uint8_t task_remove_msg(task_id_t task_id, uint8_t sig) {
	tcb_t* t_tcb;
	uint8_t total_rm_msg = 0;

	ak_msg_t* del_msg = AK_MSG_NULL; /* MUST-BE initialized AK_MSG_NULL */
	ak_msg_t* trace_msg = AK_MSG_NULL;
	ak_msg_t* traverse_msg;

	if (task_id >= task_table_size) {
		FATAL("TK", 0x05);
	}

	ENTRY_CRITICAL();

	/* get task table control */
	t_tcb = &task_pri_queue[task_table[task_id].pri - 1];

	/* check task queue available */
	if (task_ready & t_tcb->mask) {

		/* get first message of queue */
		traverse_msg = t_tcb->qhead;

		while (traverse_msg != AK_MSG_NULL) {

			/* check message task id and signal */
			if (traverse_msg->des_task_id == task_id && traverse_msg->sig == sig) {

				/* assign remove message */
				del_msg = traverse_msg;

				if (del_msg == t_tcb->qhead) {
					t_tcb->qhead = traverse_msg->next;
				}
				else {
					trace_msg->next = traverse_msg->next;
				}

				/* last message of queue */
				if (del_msg->next == AK_MSG_NULL) {
					t_tcb->qtail = trace_msg;

					/* Check if no message exist after remove current message */
					if (t_tcb->qhead == AK_MSG_NULL) {

						/* change status of task to inactive */
						task_ready &= ~t_tcb->mask;
					}
				}
			}
			else {
				trace_msg = traverse_msg;
			}

			/* consider the next message */
			traverse_msg = traverse_msg->next;

			/* free the message if it's found */
			if (del_msg != AK_MSG_NULL) {
				msg_force_free(del_msg);
				del_msg = AK_MSG_NULL;
				total_rm_msg++;
			}
		}
	}

	EXIT_CRITICAL();
	return total_rm_msg;
}

void task_post_pure_msg(task_id_t des_task_id, uint8_t sig) {
	ak_msg_t* s_msg = get_pure_msg();
	set_msg_sig(s_msg, sig);
	task_post(des_task_id, s_msg);
}

void task_post_common_msg(task_id_t des_task_id, uint8_t sig, uint8_t* data, uint8_t len) {
	ak_msg_t* s_msg = get_common_msg();
	set_msg_sig(s_msg, sig);
	set_data_common_msg(s_msg, data, len);
	task_post(des_task_id, s_msg);
}

#if AK_DYNAMIC_MSG_POOL_SIZE > 0
void task_post_dynamic_msg(task_id_t des_task_id, uint8_t sig, uint8_t* data, uint32_t len) {
	ak_msg_t* s_msg = get_dynamic_msg();
	set_msg_sig(s_msg, sig);
	set_data_dynamic_msg(s_msg, data, len);
	task_post(des_task_id, s_msg);
}
#endif

uint8_t task_post_pure_msg_try(task_id_t des_task_id, uint8_t sig) {
	ak_msg_t* s_msg = get_pure_msg_try();

	if (s_msg == AK_MSG_NULL) {
		return 0;
	}
	set_msg_sig(s_msg, sig);
	task_post(des_task_id, s_msg);
	return 1;
}

uint8_t task_post_common_msg_try(task_id_t des_task_id, uint8_t sig, uint8_t* data, uint8_t len) {
	ak_msg_t* s_msg;

	/* checked before taking a message: an oversized post is a bug, not a
	 * full pool, and must not leak the message it took */
	if (len > AK_COMMON_MSG_DATA_SIZE) {
		FATAL("MF", 0x24);
	}
	s_msg = get_common_msg_try();
	if (s_msg == AK_MSG_NULL) {
		return 0;
	}
	set_msg_sig(s_msg, sig);
	set_data_common_msg(s_msg, data, len);
	task_post(des_task_id, s_msg);
	return 1;
}

void task_entry_interrupt() {
	ENTRY_CRITICAL();
	task_irq_io_entry_trigger();
	current_task_id = AK_TASK_INTERRUPT_ID;
	EXIT_CRITICAL();
}

void task_exit_interrupt() {
	ENTRY_CRITICAL();
	current_task_id = current_task_info.id;
	task_irq_io_exit_trigger();
	EXIT_CRITICAL();
}

int task_init() {
	uint8_t pri;
	tcb_t* t_tcb;

	/* init task manager variable */
	task_current = 0;
	task_ready = 0;
	task_alive = 0;
	current_task_id = AK_TASK_IDLE_ID;
	memset(&current_task_info, 0, sizeof(current_task_info));
	current_task_info.id = AK_TASK_IDLE_ID;

	/* init kernel queue */
	for (pri = 1; pri <= TASK_PRI_MAX_SIZE; pri++) {
		t_tcb = &task_pri_queue[pri - 1];
		t_tcb->pri      = pri;
		t_tcb->mask     = (uint8_t)(1 << (pri - 1));
		t_tcb->qhead    = AK_MSG_NULL;
		t_tcb->qtail    = AK_MSG_NULL;
	}

	/* message manager must be initial fist */
	msg_init();

	/* init timer manager */
	timer_init();

	return 0;
}

uint8_t task_run_once() {
	uint8_t n = task_sheduler();
	task_polling_run();
	return n;
}

int task_run() {
	AK_PRINTF("[AK] kernel %s, active objects ready\n", AK_VERSION);

	for (;;) {
		if (task_run_once() == 0) {
			/* interrupts stay masked from the check until the port sleeps */
			ENTRY_CRITICAL();
			if (task_ready == 0) {
				ak_port_idle();
			}
			EXIT_CRITICAL();
		}
	}
}

uint32_t task_alive_take() {
	uint32_t alive;

	ENTRY_CRITICAL();
	alive = task_alive;
	task_alive = 0;
	EXIT_CRITICAL();

	return alive;
}

void task_polling_set_ability(task_id_t task_polling_id, uint8_t ability) {
	task_polling_t* __task_polling_table = task_polling_table;

	while (__task_polling_table->id < AK_TASK_POLLING_EOT_ID) {

		if (__task_polling_table->id == task_polling_id) {

			ENTRY_CRITICAL();

			__task_polling_table->ability = ability;

			EXIT_CRITICAL();

			break;
		}

		__task_polling_table++;
	}

	if (__task_polling_table->id == AK_TASK_POLLING_EOT_ID) {
		FATAL("TK", 0x07);
	}
}

void task_polling_run() {
	task_polling_t* __task_polling_table = task_polling_table;

	if (__task_polling_table == (task_polling_t*)0) {
		return;
	}

	while (__task_polling_table->id < AK_TASK_POLLING_EOT_ID) {

		ENTRY_CRITICAL();
		if (__task_polling_table->ability == AK_ENABLE) {

			EXIT_CRITICAL();
			__task_polling_table->task_polling();
		}
		else {
			EXIT_CRITICAL();
		}
		__task_polling_table++;
	}
}

static uint8_t task_sheduler() {
	uint8_t t_task_new;
	uint8_t n_msg = 0;

	ENTRY_CRITICAL();

	uint8_t t_task_current = task_current;

	while ((t_task_new = LOG2LKUP(task_ready)) > t_task_current) {
		/* get task */
		tcb_t* t_tcb = &task_pri_queue[t_task_new - 1];

		/* get message */
		ak_msg_t* t_msg = t_tcb->qhead;
		t_tcb->qhead = t_msg->next;

		/* last message of queue */
		if (t_msg->next == AK_MSG_NULL) {
			t_tcb->qtail = AK_MSG_NULL;
			/* change status of task to inactive */
			task_ready &= ~t_tcb->mask;
		}

		/* update current task */
		task_current = t_task_new;

		/* update current ak object */
		current_task_info = task_table[t_msg->des_task_id];
		current_active_object = *t_msg;

		/* NOTE: switches to AK_TASK_INTERRUPT_ID while inside an ISR */
		current_task_id = t_msg->des_task_id;

		task_alive |= (uint32_t)1 << (t_msg->des_task_id & 31);
		ak_port_note_dispatch(t_msg->des_task_id, t_msg->sig);

		/* execute task */
		EXIT_CRITICAL();

#if defined(AK_TASK_TRACE_ENABLE)
		uint32_t start_exe = ak_port_millis();
		task_table[t_msg->des_task_id].task(t_msg);
		ak_task_trace_hook(t_msg, ak_port_millis() - start_exe);
#else
		task_table[t_msg->des_task_id].task(t_msg);
#endif

		ENTRY_CRITICAL();

		n_msg++;

		/* check and free message */
		msg_free(t_msg);
	}

	task_current = t_task_current;

	current_task_info.id = AK_TASK_IDLE_ID;
	current_task_id = AK_TASK_IDLE_ID;
	ak_port_note_dispatch(AK_TASK_IDLE_ID, 0);

	EXIT_CRITICAL();

	return n_msg;
}

void task_pri_queue_dump() {
	uint8_t t_task_ready;
	tcb_t* t_tcb;
	ak_msg_t* t_msg;

	ENTRY_CRITICAL();

	t_task_ready = task_ready;

	for (uint8_t pri = 1; pri <= TASK_PRI_MAX_SIZE; pri++) {
		t_tcb = &task_pri_queue[pri - 1];

		/* check task queue available */
		if (t_task_ready & t_tcb->mask) {

			/* get first message of queue */
			t_msg = t_tcb->qhead;

			while (t_msg != AK_MSG_NULL) {

				/* dump message queue */
				AK_PRINTF("srcTaskID:%d\tdesTaskID:%d\tmsgType:0x%x\trefCnt:%d\tsig:%d\n"\
						  , t_msg->src_task_id \
						  , t_msg->des_task_id \
						  , (t_msg->ref_count & AK_MSG_TYPE_MASK) \
						  , (t_msg->ref_count & AK_MSG_REF_COUNT_MASK) \
						  , t_msg->sig);

				/* consider the next message */
				t_msg = t_msg->next;
			}
		}
	}

	EXIT_CRITICAL();
}

task_id_t task_self() {
	return current_task_info.id;
}

task_id_t get_current_task_id() {
	return current_task_id;
}

task_t* get_current_task_info() {
	return (task_t*)&current_task_info;
}

ak_msg_t* get_current_active_object() {
	return (ak_msg_t*)&current_active_object;
}
