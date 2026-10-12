/**
 ******************************************************************************
 * @author: GaoKong
 * @date:   05/09/2016
 *
 * ak-mcu-base: timers keep an absolute expiry time instead of a countdown.
 *   - a periodic timer handled late keeps its phase (no drift);
 *   - ticks that piled up before timer_set() do not shorten the new timer;
 *   - the tick ISR posts TIMER_TICK only when the earliest timer is due.
 ******************************************************************************
**/

#include "timer.h"

#include "task_list.h"

/* data shared between the heart beat interrupt and the timer task */
struct ak_timer_payload_irq_t {
	uint32_t now;				/* kernel time (ms), advanced by timer_tick() */
	uint32_t next_expire;		/* earliest expiry in the timer list */
	uint32_t enable_post_msg;
};

static volatile struct ak_timer_payload_irq_t ak_timer_payload_irq = {0, 0, AK_DISABLE};

/* wrap-safe "a is at or after b" */
#define TIMER_REACHED(a, b)		((int32_t)((a) - (b)) >= 0)

/* data to manage memory of timer message */
static ak_timer_t timer_pool[AK_TIMER_POOL_SIZE];
static ak_timer_t* free_list_timer_pool;
static uint32_t free_list_timer_used;
static uint32_t free_list_timer_used_max;
static ak_timer_t* timer_list_head;

/* allocate/free memory of timer message */
static void timer_msg_pool_init();
static ak_timer_t* get_timer_msg();
static void free_timer_msg(ak_timer_t* msg);

static uint8_t timer_remove_msg(task_id_t des_task_id, timer_sig_t sig);
static void timer_update_next_expire();

void timer_msg_pool_init() {
	uint32_t index;

	ENTRY_CRITICAL();

	timer_list_head = TIMER_MSG_NULL;
	free_list_timer_pool = (ak_timer_t*)timer_pool;

	for (index = 0; index < AK_TIMER_POOL_SIZE; index++) {
		if (index == (AK_TIMER_POOL_SIZE - 1)) {
			timer_pool[index].next = TIMER_MSG_NULL;
		}
		else {
			timer_pool[index].next = (ak_timer_t*)&timer_pool[index + 1];
		}
	}

	free_list_timer_used = 0;
	free_list_timer_used_max = 0;

	EXIT_CRITICAL();
}

ak_timer_t* get_timer_msg() {
	ak_timer_t* allocate_timer;

	ENTRY_CRITICAL();

	allocate_timer = free_list_timer_pool;

	if (allocate_timer == TIMER_MSG_NULL) {
		FATAL("MT", 0x30);
	}
	else {
		free_list_timer_pool = allocate_timer->next;

		free_list_timer_used++;
		if (free_list_timer_used >= free_list_timer_used_max) {
			free_list_timer_used_max = free_list_timer_used;
		}
	}

	EXIT_CRITICAL();

	return allocate_timer;
}

void free_timer_msg(ak_timer_t* msg) {

	ENTRY_CRITICAL();

	msg->next = free_list_timer_pool;
	free_list_timer_pool = msg;

	free_list_timer_used--;

	EXIT_CRITICAL();
}

uint32_t get_timer_msg_pool_used() {
	return free_list_timer_used;
}

uint32_t get_timer_msg_pool_used_max() {
	return free_list_timer_used_max;
}

/* MUST-BE called in critical section */
void timer_update_next_expire() {
	ak_timer_t* timer_msg = timer_list_head;
	uint32_t now = ak_timer_payload_irq.now;
	uint32_t remain;
	uint32_t remain_min = 0xFFFFFFFF;

	while (timer_msg != TIMER_MSG_NULL) {
		remain = TIMER_REACHED(now, timer_msg->expire) ? 0 : (timer_msg->expire - now);
		if (remain < remain_min) {
			remain_min = remain;
		}
		timer_msg = timer_msg->next;
	}

	ak_timer_payload_irq.next_expire = now + remain_min;
}

void task_timer_tick(ak_msg_t* msg) {
	ak_msg_t* timer_msg;

	ak_timer_t* timer_list;
	ak_timer_t* timer_next;

	task_id_t des_task_id = 0;
	timer_sig_t sig = 0;

	uint32_t now;
	uint32_t late;
	uint8_t timer_due;
	uint8_t timer_del;

	if (msg->sig != TIMER_TICK) {
		return;
	}

	ENTRY_CRITICAL();

	timer_list = timer_list_head;
	now = ak_timer_payload_irq.now;

	EXIT_CRITICAL();

	while (timer_list != TIMER_MSG_NULL) {
		timer_due = 0;
		timer_del = 0;

		ENTRY_CRITICAL();

		timer_next = timer_list->next;

		if (TIMER_REACHED(now, timer_list->expire)) {
			timer_due = 1;
			des_task_id = timer_list->des_task_id;
			sig = timer_list->sig;

			if (timer_list->period) {
				/* keep the phase; periods missed entirely are skipped */
				late = now - timer_list->expire;
				timer_list->expire += ((late / timer_list->period) + 1) * timer_list->period;
			}
			else {
				timer_del = 1;
			}
		}

		EXIT_CRITICAL();

		if (timer_due) {
			timer_msg = get_pure_msg();
			set_msg_sig(timer_msg, sig);
			task_post(des_task_id, timer_msg);
		}

		if (timer_del) {
			timer_remove_msg(des_task_id, sig);
		}

		timer_list = timer_next;
	}

	ENTRY_CRITICAL();

	timer_update_next_expire();
	ak_timer_payload_irq.enable_post_msg = AK_ENABLE;

	EXIT_CRITICAL();
}

void timer_init() {
	timer_msg_pool_init();

	ENTRY_CRITICAL();

	ak_timer_payload_irq.now = 0;
	ak_timer_payload_irq.next_expire = 0;
	ak_timer_payload_irq.enable_post_msg = AK_ENABLE;

	EXIT_CRITICAL();
}

void timer_tick(uint32_t t) {
	ak_timer_payload_irq.now += t;

	if (timer_list_head != TIMER_MSG_NULL &&
			ak_timer_payload_irq.enable_post_msg == AK_ENABLE &&
			TIMER_REACHED(ak_timer_payload_irq.now, ak_timer_payload_irq.next_expire)) {
		ak_timer_payload_irq.enable_post_msg = AK_DISABLE;

		ak_msg_t* s_msg = get_pure_msg();
		set_msg_sig(s_msg, TIMER_TICK);
		task_post(TASK_TIMER_TICK_ID, s_msg);
	}
}

uint8_t timer_set(task_id_t des_task_id, timer_sig_t sig, uint32_t duty, timer_type_t type) {
	ak_timer_t* timer_msg;

	ENTRY_CRITICAL();

	timer_msg = timer_list_head;

	while (timer_msg != TIMER_MSG_NULL) {
		if (timer_msg->des_task_id == des_task_id &&
				timer_msg->sig == sig) {

			/* re-arm: new duty and type replace the old ones */
			timer_msg->expire = ak_timer_payload_irq.now + duty;
			timer_msg->period = (type == TIMER_PERIODIC) ? duty : 0;
			timer_update_next_expire();

			EXIT_CRITICAL();

			return TIMER_RET_OK;
		}
		else {
			timer_msg = timer_msg->next;
		}
	}

	timer_msg = get_timer_msg();

	timer_msg->des_task_id = des_task_id;
	timer_msg->sig = sig;
	timer_msg->expire = ak_timer_payload_irq.now + duty;

	if (type == TIMER_PERIODIC) {
		timer_msg->period = duty;
	}
	else {
		timer_msg->period = 0;
	}

	timer_msg->next = timer_list_head;
	timer_list_head = timer_msg;

	timer_update_next_expire();

	EXIT_CRITICAL();

	return TIMER_RET_OK;
}

uint8_t timer_remove_msg(task_id_t des_task_id, timer_sig_t sig) {
	ak_timer_t* timer_msg;
	ak_timer_t* timer_msg_prev;

	ENTRY_CRITICAL();

	timer_msg = timer_list_head;
	timer_msg_prev = timer_msg;

	while (timer_msg != TIMER_MSG_NULL) {

		if (timer_msg->des_task_id == des_task_id &&
				timer_msg->sig == sig) {

			if (timer_msg == timer_list_head) {
				timer_list_head = timer_msg->next;
			}
			else {
				timer_msg_prev->next = timer_msg->next;
			}

			free_timer_msg(timer_msg);

			EXIT_CRITICAL();

			return TIMER_RET_OK;
		}
		else {
			timer_msg_prev = timer_msg;
			timer_msg = timer_msg->next;
		}
	}

	EXIT_CRITICAL();

	return TIMER_RET_NG;
}

uint8_t timer_remove_attr(task_id_t des_task_id, timer_sig_t sig) {

	uint8_t ret = timer_remove_msg(des_task_id, sig);

	task_remove_msg(des_task_id, sig);

	return ret;
}
