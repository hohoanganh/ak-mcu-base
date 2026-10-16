/**
 * Portable AK kernel tests on host: priority order, FIFO, message pools,
 * timers, remove, src_task_id, FATAL.
 *
 * Built twice: test_kernel (defaults) and test_kernel_nodyn with
 * AK_DYNAMIC_MSG_POOL_SIZE=0 (no dynamic messages, no malloc/free).
 */
#include <string.h>
#include <setjmp.h>

#include "ak.h"
#include "task.h"
#include "timer.h"
#include "message.h"
#include "task_list.h"
#include "port_host.h"
#include "tiny_test.h"

#define SIG_A	(AK_USER_DEFINE_SIG + 0)
#define SIG_B	(AK_USER_DEFINE_SIG + 1)
#define SIG_C	(AK_USER_DEFINE_SIG + 2)
#define SIG_FWD	(AK_USER_DEFINE_SIG + 3)

typedef struct {
	uint8_t task;
	uint8_t sig;
	uint8_t src;
	uint8_t data0;
	uint8_t len;
} rec_t;

static rec_t log_buf[64];
static int log_n;
static int poll_count;

static void rec(uint8_t task, ak_msg_t* m) {
	if (log_n < 64) {
		log_buf[log_n].task = task;
		log_buf[log_n].sig = m->sig;
		log_buf[log_n].src = m->src_task_id;
		log_buf[log_n].data0 = 0;
		log_buf[log_n].len = 0;
		if (get_msg_type(m) == COMMON_MSG_TYPE) {
			log_buf[log_n].len = get_data_len_common_msg(m);
			log_buf[log_n].data0 = get_data_common_msg(m)[0];
		}
#if AK_DYNAMIC_MSG_POOL_SIZE > 0
		else if (get_msg_type(m) == DYNAMIC_MSG_TYPE) {
			log_buf[log_n].len = (uint8_t)get_data_len_dynamic_msg(m);
			log_buf[log_n].data0 = get_data_dynamic_msg(m)[0];
		}
#endif
		log_n++;
	}
}

static void task_low(ak_msg_t* m) {
	rec(TASK_LOW_ID, m);
}

static void task_mid(ak_msg_t* m) {
	rec(TASK_MID_ID, m);
	/* message created inside a task: src_task_id must be TASK_MID_ID */
	if (m->sig == SIG_FWD) {
		task_post_pure_msg(TASK_LOW_ID, SIG_C);
	}
}

static void task_high(ak_msg_t* m) {
	rec(TASK_HIGH_ID, m);
}

static void poll_a(void) {
	poll_count++;
}

static task_t tbl[] = {
	{TASK_TIMER_TICK_ID,	TASK_PRI_LEVEL_7,	task_timer_tick	},
	{TASK_LOW_ID,			TASK_PRI_LEVEL_1,	task_low		},
	{TASK_MID_ID,			TASK_PRI_LEVEL_3,	task_mid		},
	{TASK_HIGH_ID,			TASK_PRI_LEVEL_6,	task_high		},
	{AK_TASK_EOT_ID,		TASK_PRI_LEVEL_0,	(pf_task)0		},
};

static task_polling_t poll_tbl[] = {
	{TASK_POLL_A_ID,			AK_ENABLE,	poll_a				},
	{AK_TASK_POLLING_EOT_ID,	AK_DISABLE,	(pf_task_polling)0	},
};

static void setup(void) {
	host_reset_state(0xFF);
	host_use_virtual_time(1);
	host_fatal_jmp = 0;
	log_n = 0;
	poll_count = 0;
	task_init();
	task_create(tbl);
	task_polling_create(poll_tbl);
}

static void run_all(void) {
	while (task_run_once()) {
	}
}

static void test_log2lkup(void) {
	CHECK_EQ(LOG2LKUP(0), 0);
	CHECK_EQ(LOG2LKUP(1), 1);
	CHECK_EQ(LOG2LKUP(0x80), 8);
	CHECK_EQ(LOG2LKUP(0x81), 8);
	CHECK_EQ(LOG2LKUP(0x80000000UL), 32);
}

static void test_priority_order(void) {
	setup();
	task_post_pure_msg(TASK_LOW_ID, SIG_A);
	task_post_pure_msg(TASK_MID_ID, SIG_A);
	task_post_pure_msg(TASK_HIGH_ID, SIG_A);
	run_all();
	CHECK_EQ(log_n, 3);
	CHECK_EQ(log_buf[0].task, TASK_HIGH_ID);
	CHECK_EQ(log_buf[1].task, TASK_MID_ID);
	CHECK_EQ(log_buf[2].task, TASK_LOW_ID);
	CHECK(poll_count > 0);
}

static void test_fifo_same_task(void) {
	setup();
	task_post_pure_msg(TASK_MID_ID, SIG_A);
	task_post_pure_msg(TASK_MID_ID, SIG_B);
	task_post_pure_msg(TASK_MID_ID, SIG_C);
	run_all();
	CHECK_EQ(log_n, 3);
	CHECK_EQ(log_buf[0].sig, SIG_A);
	CHECK_EQ(log_buf[1].sig, SIG_B);
	CHECK_EQ(log_buf[2].sig, SIG_C);
}

static void test_msg_types_and_pools(void) {
	uint8_t d[5] = { 0x42, 1, 2, 3, 4 };

	setup();
	task_post_common_msg(TASK_LOW_ID, SIG_A, d, sizeof(d));
	CHECK_EQ(get_common_msg_pool_used(), 1);
#if AK_DYNAMIC_MSG_POOL_SIZE > 0
	uint8_t big[200];

	memset(big, 0x77, sizeof(big));
	task_post_dynamic_msg(TASK_LOW_ID, SIG_B, big, sizeof(big));
	CHECK_EQ(get_dynamic_msg_pool_used(), 1);
#endif
	run_all();
	CHECK_EQ(log_buf[0].len, 5);
	CHECK_EQ(log_buf[0].data0, 0x42);
#if AK_DYNAMIC_MSG_POOL_SIZE > 0
	CHECK_EQ(log_n, 2);
	CHECK_EQ(log_buf[1].len, 200);
	CHECK_EQ(log_buf[1].data0, 0x77);
	CHECK_EQ(get_dynamic_msg_pool_used(), 0);
#else
	CHECK_EQ(log_n, 1);
#endif
	/* no pool leak */
	CHECK_EQ(get_common_msg_pool_used(), 0);
	CHECK_EQ(get_pure_msg_pool_used(), 0);
}

/* _try posts: a full pool is an answer (0), not a FATAL; the caller drops or
 * retries later. Everything posted still arrives, in order. */
static void test_post_try(void) {
	jmp_buf j;
	uint8_t d[3] = { 7, 8, 9 };
	int n;

	setup();
	host_fatal_jmp = &j;
	if (setjmp(j) != 0) {
		CHECK(0);			/* no FATAL on this path */
		host_fatal_jmp = 0;
		return;
	}
	for (n = 0; task_post_pure_msg_try(TASK_LOW_ID, SIG_A); n++) {
	}
	CHECK_EQ(n, AK_PURE_MSG_POOL_SIZE);
	CHECK_EQ(get_pure_msg_pool_used(), AK_PURE_MSG_POOL_SIZE);
	CHECK_EQ(task_post_pure_msg_try(TASK_LOW_ID, SIG_A), 0);

	for (n = 0; task_post_common_msg_try(TASK_MID_ID, SIG_B, d, sizeof(d)); n++) {
	}
	CHECK_EQ(n, AK_COMMON_MSG_POOL_SIZE);
	CHECK_EQ(task_post_common_msg_try(TASK_MID_ID, SIG_B, d, sizeof(d)), 0);

	run_all();
	CHECK_EQ(log_n, AK_PURE_MSG_POOL_SIZE + AK_COMMON_MSG_POOL_SIZE);
	CHECK_EQ(log_buf[0].task, TASK_MID_ID);		/* higher priority first */
	CHECK_EQ(log_buf[0].len, 3);
	CHECK_EQ(log_buf[0].data0, 7);
	CHECK_EQ(log_buf[log_n - 1].task, TASK_LOW_ID);
	CHECK_EQ(log_buf[log_n - 1].src, AK_TASK_IDLE_ID);
	CHECK_EQ(get_pure_msg_pool_used(), 0);
	CHECK_EQ(get_common_msg_pool_used(), 0);

	/* room again: posts go through */
	CHECK_EQ(task_post_pure_msg_try(TASK_HIGH_ID, SIG_C), 1);
	run_all();
	CHECK_EQ(log_buf[log_n - 1].task, TASK_HIGH_ID);
	host_fatal_jmp = 0;
}

/* data longer than a common message is a programming error: FATAL even on
 * the _try path */
static void test_post_try_oversize_is_fatal(void) {
	jmp_buf j;
	uint8_t big[AK_COMMON_MSG_DATA_SIZE + 1];

	setup();
	memset(big, 0, sizeof(big));
	host_fatal_jmp = &j;
	if (setjmp(j) == 0) {
		task_post_common_msg_try(TASK_LOW_ID, SIG_A, big, sizeof(big));
		CHECK(0);
	}
	else {
		CHECK_EQ(host_fatal_code, 0x24);
	}
	host_fatal_jmp = 0;
	CHECK_EQ(get_common_msg_pool_used(), 0);	/* nothing taken from the pool */
}

static void test_src_task_id(void) {
	setup();
	task_post_pure_msg(TASK_MID_ID, SIG_FWD);
	run_all();
	CHECK_EQ(log_n, 2);
	CHECK_EQ(log_buf[1].task, TASK_LOW_ID);
	CHECK_EQ(log_buf[1].sig, SIG_C);
	/* original returned if_des_task_id (0) - fixed */
	CHECK_EQ(log_buf[1].src, TASK_MID_ID);
}

static void test_timer_one_shot(void) {
	setup();
	timer_set(TASK_LOW_ID, SIG_A, 100, TIMER_ONE_SHOT);
	CHECK_EQ(get_timer_msg_pool_used(), 1);
	for (int i = 0; i < 99; i++) {
		timer_tick(1);
		run_all();
	}
	CHECK_EQ(log_n, 0);
	timer_tick(1);
	run_all();
	CHECK_EQ(log_n, 1);
	CHECK_EQ(log_buf[0].sig, SIG_A);
	CHECK_EQ(get_timer_msg_pool_used(), 0);
	for (int i = 0; i < 300; i++) {
		timer_tick(1);
		run_all();
	}
	CHECK_EQ(log_n, 1);
}

static void test_timer_periodic_and_remove(void) {
	setup();
	timer_set(TASK_MID_ID, SIG_B, 10, TIMER_PERIODIC);
	for (int i = 0; i < 55; i++) {
		timer_tick(1);
		run_all();
	}
	CHECK_EQ(log_n, 5);
	/* batched tick (late ISR): 30 ms at once -> fires once, not 3 times */
	timer_tick(30);
	run_all();
	CHECK_EQ(log_n, 6);
	timer_remove_attr(TASK_MID_ID, SIG_B);
	CHECK_EQ(get_timer_msg_pool_used(), 0);
	for (int i = 0; i < 50; i++) {
		timer_tick(1);
		run_all();
	}
	CHECK_EQ(log_n, 6);
}

static void test_timer_reset_existing(void) {
	setup();
	timer_set(TASK_LOW_ID, SIG_A, 50, TIMER_ONE_SHOT);
	for (int i = 0; i < 40; i++) {
		timer_tick(1);
		run_all();
	}
	/* re-arm same (task, sig): restarts the counter, no new timer */
	timer_set(TASK_LOW_ID, SIG_A, 50, TIMER_ONE_SHOT);
	CHECK_EQ(get_timer_msg_pool_used(), 1);
	for (int i = 0; i < 40; i++) {
		timer_tick(1);
		run_all();
	}
	CHECK_EQ(log_n, 0);
	for (int i = 0; i < 10; i++) {
		timer_tick(1);
		run_all();
	}
	CHECK_EQ(log_n, 1);
}

static int count_sig(uint8_t sig) {
	int n = 0;
	for (int i = 0; i < log_n; i++) {
		if (log_buf[i].sig == sig) {
			n++;
		}
	}
	return n;
}

/* timer_set() on a running timer takes the new duty and type, not only the
 * new expiry: periodic -> one-shot stops repeating, one-shot -> periodic
 * starts repeating. */
static void test_timer_rearm_changes_type(void) {
	setup();
	timer_set(TASK_MID_ID, SIG_B, 10, TIMER_PERIODIC);
	for (int i = 0; i < 25; i++) {
		timer_tick(1);
		run_all();
	}
	CHECK_EQ(log_n, 2);
	timer_set(TASK_MID_ID, SIG_B, 10, TIMER_ONE_SHOT);
	CHECK_EQ(get_timer_msg_pool_used(), 1);
	for (int i = 0; i < 35; i++) {
		timer_tick(1);
		run_all();
	}
	CHECK_EQ(log_n, 3);
	CHECK_EQ(get_timer_msg_pool_used(), 0);

	timer_set(TASK_MID_ID, SIG_B, 100, TIMER_ONE_SHOT);
	timer_set(TASK_MID_ID, SIG_B, 10, TIMER_PERIODIC);
	for (int i = 0; i < 35; i++) {
		timer_tick(1);
		run_all();
	}
	CHECK_EQ(log_n, 6);
	timer_remove_attr(TASK_MID_ID, SIG_B);
}

/* xprintf: sign before the zero padding, width counts the sign. */
static char xp_buf[64];
static int xp_n;
static void xp_out(uint8_t c) {
	if (xp_n < (int)sizeof(xp_buf) - 1) {
		xp_buf[xp_n++] = (char)c;
	}
}
static const char* xp(const char* fmt, int32_t v) {
	void (*saved)(uint8_t) = xfunc_out;
	xfunc_out = xp_out;
	xp_n = 0;
	xprintf(fmt, v);
	xp_buf[xp_n] = 0;
	xfunc_out = saved;
	return xp_buf;
}
static void test_xprintf_numbers(void) {
	CHECK(strcmp(xp("%05d", -5), "-0005") == 0);
	CHECK(strcmp(xp("%5d", -5), "   -5") == 0);
	CHECK(strcmp(xp("%03d", 7), "007") == 0);
	CHECK(strcmp(xp("%d", -42), "-42") == 0);
	CHECK(strcmp(xp("%02d", -123), "-123") == 0);
	CHECK(strcmp(xp("%08X", 0xBEEF), "0000BEEF") == 0);
	CHECK(strcmp(xp("%u", -1), "4294967295") == 0);
}

/* A late tick (handler hogged the CPU) must not shift the following periods. */
static void test_timer_periodic_no_drift(void) {
	setup();
	timer_set(TASK_MID_ID, SIG_B, 10, TIMER_PERIODIC);
	for (int i = 0; i < 9; i++) {
		timer_tick(1);
		run_all();
	}
	CHECK_EQ(log_n, 0);
	timer_tick(5);		/* t = 14: first period handled 4 ms late */
	run_all();
	CHECK_EQ(log_n, 1);
	for (int i = 0; i < 5; i++) {
		timer_tick(1);
		run_all();
	}
	CHECK_EQ(log_n, 1);	/* t = 19 */
	timer_tick(1);		/* t = 20: second period on schedule */
	run_all();
	CHECK_EQ(log_n, 2);
}

/* Ticks that piled up before timer_set() must not shorten the new timer. */
static void test_timer_set_after_blocking(void) {
	setup();
	timer_set(TASK_MID_ID, SIG_B, 1000, TIMER_PERIODIC);
	timer_tick(50);		/* 50 ms pass while a handler blocks the scheduler */
	timer_set(TASK_LOW_ID, SIG_A, 100, TIMER_ONE_SHOT);
	run_all();
	for (int i = 0; i < 99; i++) {
		timer_tick(1);
		run_all();
	}
	CHECK_EQ(count_sig(SIG_A), 0);
	timer_tick(1);
	run_all();
	CHECK_EQ(count_sig(SIG_A), 1);
}

/* The tick ISR only wakes the timer task when a timer is due. */
static void test_timer_tick_only_when_due(void) {
	int handled = 0;

	setup();
	timer_set(TASK_LOW_ID, SIG_A, 100, TIMER_ONE_SHOT);
	for (int i = 0; i < 99; i++) {
		timer_tick(1);
		handled += task_run_once();
	}
	CHECK_EQ(handled, 0);
	CHECK_EQ(get_pure_msg_pool_used(), 0);
	timer_tick(1);
	run_all();
	CHECK_EQ(log_n, 1);
}

/* Outside any task the current id is IDLE, inside an ISR it is INTERRUPT. */
static void test_src_task_id_idle_and_isr(void) {
	setup();
	CHECK_EQ(get_current_task_id(), AK_TASK_IDLE_ID);
	task_post_pure_msg(TASK_MID_ID, SIG_A);
	run_all();
	CHECK_EQ(get_current_task_id(), AK_TASK_IDLE_ID);
	CHECK_EQ(task_self(), AK_TASK_IDLE_ID);

	task_entry_interrupt();
	task_post_pure_msg(TASK_LOW_ID, SIG_B);
	task_exit_interrupt();
	/* back in idle context (polling task): must not inherit the last task */
	CHECK_EQ(get_current_task_id(), AK_TASK_IDLE_ID);
	task_post_pure_msg(TASK_LOW_ID, SIG_C);
	run_all();
	CHECK_EQ(log_n, 3);
	CHECK_EQ(log_buf[1].src, AK_TASK_INTERRUPT_ID);
	CHECK_EQ(log_buf[2].src, AK_TASK_IDLE_ID);
}

/* The scheduler reports what it runs (watchdog post-mortem) and which tasks
 * got CPU time (liveness check with AK_SIG_PING). */
static uint8_t seen_task, seen_sig;

static void task_probe(ak_msg_t* m) {
	rec(TASK_HIGH_ID, m);
	seen_task = host_cur_task();
	seen_sig = host_cur_sig();
}

static void test_dispatch_note_and_alive(void) {
	static task_t probe_tbl[] = {
		{TASK_TIMER_TICK_ID,	TASK_PRI_LEVEL_7,	task_timer_tick	},
		{TASK_LOW_ID,			TASK_PRI_LEVEL_1,	task_low		},
		{TASK_MID_ID,			TASK_PRI_LEVEL_3,	task_mid		},
		{TASK_HIGH_ID,			TASK_PRI_LEVEL_6,	task_probe		},
		{AK_TASK_EOT_ID,		TASK_PRI_LEVEL_0,	(pf_task)0		},
	};

	setup();
	task_create(probe_tbl);
	CHECK_EQ(task_alive_take(), 0);
	CHECK_EQ(host_cur_task(), AK_TASK_IDLE_ID);

	task_post_pure_msg(TASK_HIGH_ID, SIG_B);
	task_post_pure_msg(TASK_LOW_ID, AK_SIG_PING);
	run_all();
	/* inside the handler the port knew task + signal; idle again afterwards */
	CHECK_EQ(seen_task, TASK_HIGH_ID);
	CHECK_EQ(seen_sig, SIG_B);
	CHECK_EQ(host_cur_task(), AK_TASK_IDLE_ID);

	/* both tasks ran, MID did not; reading clears the set */
	CHECK_EQ(task_alive_take(), (1UL << TASK_HIGH_ID) | (1UL << TASK_LOW_ID));
	CHECK_EQ(task_alive_take(), 0);
}

static void test_remove_msg(void) {
	setup();
	task_post_pure_msg(TASK_LOW_ID, SIG_A);
	task_post_pure_msg(TASK_LOW_ID, SIG_B);
	task_post_pure_msg(TASK_LOW_ID, SIG_A);
	task_post_pure_msg(TASK_LOW_ID, SIG_C);
	CHECK_EQ(task_remove_msg(TASK_LOW_ID, SIG_A), 2);
	CHECK_EQ(get_pure_msg_pool_used(), 2);
	run_all();
	CHECK_EQ(log_n, 2);
	CHECK_EQ(log_buf[0].sig, SIG_B);
	CHECK_EQ(log_buf[1].sig, SIG_C);
	/* remove the tail message then post again: qtail must stay valid */
	task_post_pure_msg(TASK_LOW_ID, SIG_A);
	task_post_pure_msg(TASK_LOW_ID, SIG_B);
	CHECK_EQ(task_remove_msg(TASK_LOW_ID, SIG_B), 1);
	task_post_pure_msg(TASK_LOW_ID, SIG_C);
	run_all();
	CHECK_EQ(log_n, 4);
	CHECK_EQ(log_buf[2].sig, SIG_A);
	CHECK_EQ(log_buf[3].sig, SIG_C);
}

static void test_fatal_bad_table(void) {
	static task_t bad[] = {
		{TASK_TIMER_TICK_ID,	TASK_PRI_LEVEL_7,	task_timer_tick	},
		{TASK_MID_ID,			TASK_PRI_LEVEL_3,	task_mid		},	/* wrong order */
		{AK_TASK_EOT_ID,		TASK_PRI_LEVEL_0,	(pf_task)0		},
	};
	jmp_buf j;

	setup();
	host_fatal_jmp = &j;
	if (setjmp(j) == 0) {
		task_create(bad);
		CHECK(0);	/* must not get here */
	}
	else {
		CHECK(strcmp(host_fatal_str, "TK") == 0);
		CHECK_EQ(host_fatal_code, 0x08);
	}
	host_fatal_jmp = 0;
}

static void test_fatal_pool_exhausted(void) {
	jmp_buf j;
	volatile int posted = 0;

	setup();
	host_fatal_jmp = &j;
	if (setjmp(j) == 0) {
		for (;;) {
			task_post_pure_msg(TASK_LOW_ID, SIG_A);
			posted++;
		}
	}
	CHECK_EQ(posted, AK_PURE_MSG_POOL_SIZE);
	CHECK(strcmp(host_fatal_str, "MF") == 0);
	host_fatal_jmp = 0;
}

static void test_fatal_bad_dest(void) {
	jmp_buf j;

	setup();
	host_fatal_jmp = &j;
	if (setjmp(j) == 0) {
		task_post_pure_msg(AK_TASK_EOT_ID, SIG_A);
		CHECK(0);
	}
	else {
		CHECK_EQ(host_fatal_code, 0x02);
	}
	host_fatal_jmp = 0;
}

TT_MAIN_BEGIN("test_kernel")
	RUN_TEST(test_log2lkup);
	RUN_TEST(test_priority_order);
	RUN_TEST(test_fifo_same_task);
	RUN_TEST(test_msg_types_and_pools);
	RUN_TEST(test_post_try);
	RUN_TEST(test_post_try_oversize_is_fatal);
	RUN_TEST(test_src_task_id);
	RUN_TEST(test_timer_one_shot);
	RUN_TEST(test_timer_periodic_and_remove);
	RUN_TEST(test_timer_reset_existing);
	RUN_TEST(test_timer_periodic_no_drift);
	RUN_TEST(test_timer_rearm_changes_type);
	RUN_TEST(test_xprintf_numbers);
	RUN_TEST(test_timer_set_after_blocking);
	RUN_TEST(test_timer_tick_only_when_due);
	RUN_TEST(test_src_task_id_idle_and_isr);
	RUN_TEST(test_dispatch_note_and_alive);
	RUN_TEST(test_remove_msg);
	RUN_TEST(test_fatal_bad_table);
	RUN_TEST(test_fatal_pool_exhausted);
	RUN_TEST(test_fatal_bad_dest);
TT_MAIN_END()
