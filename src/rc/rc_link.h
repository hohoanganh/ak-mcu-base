/**
 ******************************************************************************
 * @brief:  Radio link between the ak-mcu remote (rc_tx) and a receiver /
 *          flight controller (rc_rx), step P1 of docs/drone-ak-mcu.md.
 *          Chip independent: packets, channel hopping, arming and failsafe,
 *          stick calibration, the EEPROM record. The radio itself is an
 *          nRF24L01+ in Enhanced ShockBurst mode (port/stm32l151/nrf24.c):
 *          the receiver acknowledges every control packet and the ACK
 *          carries telemetry back.
 *
 *  Control packet, remote -> receiver (RC_CTRL_LEN bytes):
 *    [0] RC_TYPE_CTRL   [1] sequence   [2] hop index
 *    [3..7] roll, pitch, yaw (-500..500, +512 offset) and throttle (0..1000),
 *           10 bits each, packed little-endian
 *    [8] flags RC_F_*   [9..10] CRC16 (crc16_update over [0..8])
 *  Telemetry, in the ACK of the next control packet (RC_TELE_LEN bytes):
 *    [0] RC_TYPE_TELE   [1] last sequence received   [2..3] battery mV
 *    [4] state RC_S_*   [5] good packets in the last second / 2
 *    [6..7] CRC16
 *  Bind packet, on RC_BIND_CH with the bind address (RC_BIND_LEN bytes):
 *    [0] RC_TYPE_BIND   [1..4] remote id   [5..6] CRC16
 *
 *  Hopping: RC_HOPS channels from the id. The remote moves to the next
 *  channel after every acknowledged packet; an unacknowledged one is sent once
 *  more on the same channel, then it moves on anyway. The receiver listens on
 *  the channel after the hop index of the last good packet, so it does not
 *  depend on the timing of the remote (a slow handler there only delays a
 *  packet). Silent for RC_FOLLOW_MS (both tries on a channel lost, so the
 *  remote has moved on) it steps one channel ahead once; still silent, it
 *  stays RC_DWELL_MS on each channel, a whole round of the remote even with
 *  every packet repeated, before trying the next one.
 ******************************************************************************
**/

#ifndef __RC_LINK_H__
#define __RC_LINK_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define RC_PERIOD_MS		(4)			/* 250 control packets per second */
#define RC_HOPS				(8)
#define RC_FOLLOW_MS		(7 * RC_PERIOD_MS / 2)	/* receiver: two lost tries, a little late, step ahead once */
#define RC_DWELL_MS			((2 * RC_HOPS + 1) * RC_PERIOD_MS)	/* receiver, out of step */
#define RC_BIND_CH			(76)
#define RC_FAILSAFE_MS		(200)		/* receiver: no good packet for this long = failsafe */
#define RC_LOST_MS			(300)		/* remote: no ACK for this long = link lost */

#define RC_TYPE_CTRL		(0x51)
#define RC_TYPE_TELE		(0x52)
#define RC_TYPE_BIND		(0x53)

#define RC_CTRL_LEN			(11)
#define RC_TELE_LEN			(8)
#define RC_BIND_LEN			(7)

#define RC_F_ARM			(0x01)		/* remote wants the motors armed */
#define RC_F_ANGLE			(0x02)		/* self level (else acro) */
#define RC_F_BEEP			(0x04)		/* find-me beeper */

#define RC_S_ARMED			(0x01)
#define RC_S_FAILSAFE		(0x02)		/* the receiver went through a failsafe since it was armed */

#define RC_STICK_MAX		(500)
#define RC_THR_MAX			(1000)
#define RC_THR_ARM_MAX		(50)		/* arming only with the throttle at or below this */

typedef struct {
	int16_t roll, pitch, yaw;	/* -500..500 */
	uint16_t thr;				/* 0..1000 */
	uint8_t flags;				/* RC_F_* */
} rc_cmd_t;

typedef struct {
	uint8_t seq;
	uint16_t vbat_mv;
	uint8_t state;				/* RC_S_* */
	uint8_t rate_half;			/* good packets per second / 2 */
} rc_tele_t;

/*----------------------------------------------------------------------------
 * packets: return the length written / 1 if the packet is valid
 *--------------------------------------------------------------------------*/
extern uint8_t rc_ctrl_pack(uint8_t* out, uint8_t seq, uint8_t hop, const rc_cmd_t* c);
extern uint8_t rc_ctrl_unpack(const uint8_t* in, uint8_t len, uint8_t* seq, uint8_t* hop, rc_cmd_t* c);
extern uint8_t rc_tele_pack(uint8_t* out, const rc_tele_t* t);
extern uint8_t rc_tele_unpack(const uint8_t* in, uint8_t len, rc_tele_t* t);
extern uint8_t rc_bind_pack(uint8_t* out, uint32_t id);
extern uint8_t rc_bind_unpack(const uint8_t* in, uint8_t len, uint32_t* id);

/*----------------------------------------------------------------------------
 * addresses and channels from the remote id
 *--------------------------------------------------------------------------*/
extern void rc_addr(uint32_t id, uint8_t addr[5]);	/* id 0 = the bind address */
extern void rc_hop_table(uint32_t id, uint8_t ch[RC_HOPS]);	/* distinct, 2..80, 3 apart */

/* remote side of the hopping */
typedef struct {
	uint8_t hop;
	uint8_t repeated;			/* the packet on this channel was already sent twice */
} rc_hop_tx_t;

extern void rc_hop_tx_init(rc_hop_tx_t* h);
/* the last packet was (not) acknowledged: channel index for the next one */
extern uint8_t rc_hop_tx_next(rc_hop_tx_t* h, uint8_t acked);

/* receiver side of the hopping */
typedef struct {
	uint8_t hop;				/* channel index it listens on */
	uint8_t followed;			/* already stepped ahead after the last good packet */
	uint32_t since_ms;			/* on this channel since */
} rc_hop_rx_t;

extern void rc_hop_rx_init(rc_hop_rx_t* h, uint32_t now_ms);
/* a good packet with hop index pkt_hop came: listen on the next one */
extern void rc_hop_rx_got(rc_hop_rx_t* h, uint8_t pkt_hop, uint32_t now_ms);
/* call often; returns 1 when the receiver must change channel (to h->hop) */
extern uint8_t rc_hop_rx_tick(rc_hop_rx_t* h, uint32_t now_ms);

/*----------------------------------------------------------------------------
 * receiver: arming and failsafe. Motors may only run while rc_rx_armed().
 *   - arms on a rising RC_F_ARM with the throttle at or below RC_THR_ARM_MAX
 *   - disarms when RC_F_ARM drops
 *   - failsafe: no good packet for RC_FAILSAFE_MS -> disarmed, throttle 0;
 *     arming again needs RC_F_ARM to drop and rise (a remote that comes back
 *     with the arm switch still on does not start the motors)
 *--------------------------------------------------------------------------*/
typedef struct {
	rc_cmd_t cmd;				/* last good command */
	uint32_t last_ms;
	uint8_t armed;
	uint8_t failsafe;			/* in failsafe now */
	uint8_t failsafe_seen;		/* since the last arming */
	uint8_t prev_arm_flag;
	uint8_t have_packet;
} rc_rx_t;

extern void rc_rx_init(rc_rx_t* r);
extern void rc_rx_packet(rc_rx_t* r, const rc_cmd_t* c, uint32_t now_ms);
extern void rc_rx_tick(rc_rx_t* r, uint32_t now_ms);
/* what the motors should get: the last command, throttle 0 and flags 0 when
 * disarmed or in failsafe */
extern void rc_rx_output(const rc_rx_t* r, rc_cmd_t* out);

/*----------------------------------------------------------------------------
 * remote: stick calibration. raw = ADC counts of one axis.
 *--------------------------------------------------------------------------*/
typedef struct {
	uint16_t min, mid, max;		/* mid unused for the throttle */
} rc_axis_cal_t;

typedef struct {
	rc_axis_cal_t axis[4];		/* roll, pitch, yaw, throttle */
} rc_cal_t;

#define RC_DEADBAND			(12)		/* stick units around the centre that read 0 */

extern void rc_cal_default(rc_cal_t* cal, uint16_t full_scale);
/* raw[4] -> sticks: -500..500 with dead band, throttle 0..1000 */
extern void rc_cal_apply(const rc_cal_t* cal, const uint16_t raw[4], rc_cmd_t* c);

/*----------------------------------------------------------------------------
 * EEPROM: the 32 project bytes [224, 256) of the NVM
 *   [0] magic RC_NVM_MAGIC  [1..4] id  [5] CRC8      (both roles: the remote
 *   [6..29] calibration (remote)        [30] CRC8     stores its own id, the
 *                                                     receiver the bound one)
 *--------------------------------------------------------------------------*/
#define RC_NVM_OFFSET		(224)
#define RC_NVM_SIZE			(32)
#define RC_NVM_MAGIC		(0xA7)

extern uint8_t rc_crc8(const uint8_t* p, uint8_t n);
extern void rc_nvm_put_id(uint8_t rec[RC_NVM_SIZE], uint32_t id);
extern uint8_t rc_nvm_get_id(const uint8_t rec[RC_NVM_SIZE], uint32_t* id);
extern void rc_nvm_put_cal(uint8_t rec[RC_NVM_SIZE], const rc_cal_t* cal);
extern uint8_t rc_nvm_get_cal(const uint8_t rec[RC_NVM_SIZE], rc_cal_t* cal);

#ifdef __cplusplus
}
#endif

#endif /* __RC_LINK_H__ */
