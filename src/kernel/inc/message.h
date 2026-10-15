/**
 ******************************************************************************
 * @author: GaoKong
 * @date:   13/08/2016
 ******************************************************************************
**/

#ifndef __MESSAGE_H__
#define __MESSAGE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stdlib.h>
#include "ak.h"
#include "ak_port.h"

#define AK_MSG_NULL ((ak_msg_t*)0)

#define AK_MSG_NG					(0)
#define AK_MSG_OK					(1)

/* sizeof common message pool */
#ifndef AK_COMMON_MSG_POOL_SIZE
#define AK_COMMON_MSG_POOL_SIZE		(8)
#endif

/* sizeof data common message */
#ifndef AK_COMMON_MSG_DATA_SIZE
#define AK_COMMON_MSG_DATA_SIZE		(64)
#endif

/* sizeof pure message pool */
#ifndef AK_PURE_MSG_POOL_SIZE
#define AK_PURE_MSG_POOL_SIZE		(32)
#endif

/* sizeof dynamic message pool */
#ifndef AK_DYNAMIC_MSG_POOL_SIZE
#define AK_DYNAMIC_MSG_POOL_SIZE	(8)
#endif

/* numbers of dynamic pool data unit */
#ifndef AK_DYNAMIC_DATA_POOL_SIZE
#define AK_DYNAMIC_DATA_POOL_SIZE	(128)
#endif

/* sizeof dynamic pool data unit */
#ifndef AK_DYNAMIC_PDU_SIZE
#define AK_DYNAMIC_PDU_SIZE			(4)
#endif

#define AK_MSG_TYPE_MASK			(0xC0)
#define AK_MSG_REF_COUNT_MASK		(0x3F)

#define AK_MSG_REF_COUNT_MAX		(7)

#define get_msg_ref_count(x)		((((ak_msg_t*)x)->ref_count) & AK_MSG_REF_COUNT_MASK)
#define reset_msg_ref_count(x)		((((ak_msg_t*)x)->ref_count) = \
	(((ak_msg_t*)x)->ref_count) & (~AK_MSG_REF_COUNT_MASK))

#define get_msg_type(x)				((((ak_msg_t*)x)->ref_count) & AK_MSG_TYPE_MASK)

/**************/
/* ak_message */
/**************/
typedef struct ak_msg_t {
	/*******************************
	 * private for kernel.
	 ******************************/
	/* message management */
	struct ak_msg_t*	next;

	/* task header. The old base also carried five if_* fields (external
	 * task routing) here; nothing in this base reads them and they doubled
	 * the header to 16 bytes, so they are gone: a message header is 8 bytes. */
	uint8_t				src_task_id;
	uint8_t				des_task_id;
	uint8_t				ref_count;
	uint8_t				sig;
} ak_msg_t;

/******************/
/* common message */
/******************/
typedef struct {
	ak_msg_t	msg_header;
	uint8_t		len;
	uint8_t		data[AK_COMMON_MSG_DATA_SIZE];
} ak_msg_common_t;

/****************/
/* pure message */
/****************/
typedef struct {
	ak_msg_t	msg_header;
} ak_msg_pure_t;

/*******************/
/* dynamic message */
/*******************/
typedef struct {
	ak_msg_t	msg_header;
	uint32_t	len;
	uint8_t*	data;
} ak_msg_dynamic_t;

/*******************************/
/* ak interface header message */
/*******************************/
typedef struct {
	uint8_t type;
	uint8_t src_task_id;
	uint8_t des_task_id;
	uint8_t sig;
	uint8_t if_src_type;
	uint8_t if_des_type;
} __AK_PACKETED ak_msg_if_header_t;

/*****************************/
/* ak pure interface message */
/*****************************/
typedef struct {
	ak_msg_if_header_t header;
} ak_msg_pure_if_t;

/*******************************/
/* ak common interface message */
/*******************************/
typedef struct {
	ak_msg_if_header_t header;

	uint8_t len;
	uint8_t data[AK_COMMON_MSG_DATA_SIZE];
} __AK_PACKETED ak_msg_common_if_t;

/*******************************/
/* ak dynamic interface message */
/*******************************/
typedef struct {
	ak_msg_if_header_t header;

	uint32_t len;
	uint8_t *data;
} __AK_PACKETED ak_msg_dynamic_if_t;

#define set_msg_sig(m, s)				(((ak_msg_t*)m)->sig = s)
#define set_msg_src_task_id(m, t)		(((ak_msg_t*)m)->src_task_id = t)
#define set_msg_des_task_id(m, t)		(((ak_msg_t*)m)->des_task_id = t)

/* external if interface */
#define set_if_src_task_id(m, t)		(((ak_msg_t*)m)->if_src_task_id = t)
#define set_if_des_task_id(m, t)		(((ak_msg_t*)m)->if_des_task_id = t)
#define set_if_src_type(m, t)			(((ak_msg_t*)m)->if_src_type = t)
#define set_if_des_type(m, t)			(((ak_msg_t*)m)->if_des_type = t)
#define set_if_sig(m, s)				(((ak_msg_t*)m)->if_sig = s)
#define set_if_data_common_msg(m, d, s) \
	set_data_common_msg(m, d, s)
#define set_if_data_dynamic_msg(m, d, s) \
	set_data_dynamic_msg(m, d, s)

extern void msg_init();
extern void msg_free(ak_msg_t* msg); /* free message with check ref_count */
extern void msg_force_free(ak_msg_t* msg); /* free message without check ref_count */
extern void msg_inc_ref_count(ak_msg_t* msg);
extern void msg_dec_ref_count(ak_msg_t* msg);

/*****************************************************************************
 * DEFINITION: dynamic allocate
 *
 *****************************************************************************/
extern void* ak_malloc(size_t);
extern void ak_free(void*);

/*****************************************************************************
 * DEFINITION: message
 *
 *****************************************************************************/
#define PURE_MSG_TYPE					(0x80)
#define COMMON_MSG_TYPE					(0xC0)
#define DYNAMIC_MSG_TYPE				(0x40)

/* pure message
 * message only contain the task signal.
 */
extern ak_msg_t* get_pure_msg();
/* AK_MSG_NULL when the pool is empty (get_pure_msg() is FATAL then) */
extern ak_msg_t* get_pure_msg_try();
extern uint32_t get_pure_msg_pool_used();
extern uint32_t get_pure_msg_pool_used_max();

/* common message
 * message contain a continue block memory with size = AK_COMMON_MSG_DATA_SIZE.
 */
extern ak_msg_t* get_common_msg();
/* AK_MSG_NULL when the pool is empty (get_common_msg() is FATAL then) */
extern ak_msg_t* get_common_msg_try();
extern uint32_t get_common_msg_pool_used();
extern uint32_t get_common_msg_pool_used_max();
extern uint8_t set_data_common_msg(ak_msg_t* msg, uint8_t* data, uint8_t size);
extern uint8_t* get_data_common_msg(ak_msg_t* msg);
extern uint8_t get_data_len_common_msg(ak_msg_t* msg);

/* dynmic message
 * message contain a link list block memory. Only with
 * AK_DYNAMIC_MSG_POOL_SIZE > 0 (0: no dynamic messages, no malloc/free).
 */
#if AK_DYNAMIC_MSG_POOL_SIZE > 0
extern ak_msg_t* get_dynamic_msg();
extern uint32_t get_dynamic_msg_pool_used();
extern uint32_t get_dynamic_msg_pool_used_max();
extern uint8_t set_data_dynamic_msg(ak_msg_t* msg, uint8_t* data, uint32_t size);
extern uint8_t* get_data_dynamic_msg(ak_msg_t* msg);
extern uint32_t get_data_len_dynamic_msg(ak_msg_t* msg);
#endif

/*****************************************************************************
 * DEFINITION: debug message
 *
 *****************************************************************************/
extern void msg_dbg_dum(ak_msg_t* msg);

#ifdef __cplusplus
}
#endif

#endif //__MESSAGE_H__
