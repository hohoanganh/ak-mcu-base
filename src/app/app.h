#ifndef __APP_H__
#define __APP_H__

#include <stdint.h>

#include "ak.h"
#include "fw_types.h"

#ifndef APP_VER_MAJOR
#define APP_VER_MAJOR		1
#define APP_VER_MINOR		3
#define APP_VER_PATCH		8
#endif

#ifndef APP_VER_BUILD
#define APP_VER_BUILD		0
#endif

#define APP_WDT_TIMEOUT_MS		(8000)
#define APP_HEARTBEAT_MS		(1000)

/* A task that handles nothing (not even the ping) for this many heartbeats is
 * reported as stalled: crash log entry + reset. Must stay well below
 * APP_WDT_TIMEOUT_MS / APP_HEARTBEAT_MS. */
#define APP_TASK_STALL_ROUNDS	(3)

/* 1: shell command "crash test ..." that provokes each kind of crash, to check
 * the crash log on a board. Set 0 in a product. */
#ifndef APP_CRASH_TEST
#define APP_CRASH_TEST			(1)
#endif

/* Modbus on the RS485 port (role: -DAPP_MODBUS_SLAVE, -DAPP_MODBUS_MASTER or
 * -DAPP_MODBUS_GATEWAY = Modbus TCP server on the Ethernet chip, RTU master
 * on RS485) */
#ifndef APP_MB_BAUD
#define APP_MB_BAUD						(9600)
#endif
#ifndef APP_MB_UNIT_ID
#define APP_MB_UNIT_ID					(1)			/* slave address, 1..247 */
#endif
#define APP_MB_RESPONSE_TIMEOUT_MS		(200)		/* master: wait for an answer */

/* gateway: static address of the Ethernet chip (no DHCP) and the TCP port.
 * The MAC is locally administered (bit 1 of the first byte). */
#ifndef APP_NET_MAC
#define APP_NET_MAC						{ 0x02, 0x41, 0x4B, 0x00, 0x00, 0x01 }
#endif
#ifndef APP_NET_IP
#define APP_NET_IP						{ 192, 168, 1, 80 }
#endif
#ifndef APP_NET_MASK
#define APP_NET_MASK					{ 255, 255, 255, 0 }
#endif
#ifndef APP_NET_GW
#define APP_NET_GW						{ 192, 168, 1, 1 }
#endif
#ifndef APP_MB_TCP_PORT
#define APP_MB_TCP_PORT					(502)
#endif

/* A freshly installed image runs on trial (boot_ctrl.trial): the bootloader
 * rolls back to the previous one unless the app confirms itself. The sample
 * app confirms after this long without a reset: kernel, watchdog, console
 * and the heartbeat all proved to work. 0 = never automatically (the app
 * calls boot_ctrl_confirm() itself, or the shell: "fw confirm"). */
#ifndef APP_FW_CONFIRM_MS
#define APP_FW_CONFIRM_MS		(30000)
#endif

/* TASK_SYSTEM_ID */
enum {
	SYSTEM_SIG_INIT = AK_USER_DEFINE_SIG,
	SYSTEM_SIG_HEARTBEAT,
	SYSTEM_SIG_CONFIRM,		/* timer: the trial image has run long enough */
};

/* TASK_CONSOLE_ID */
enum {
	CONSOLE_SIG_INIT = AK_USER_DEFINE_SIG,
	CONSOLE_SIG_LINE,		/* common msg: one command line */
};

/* TASK_FW_ID */
enum {
	FW_SIG_INSTALL = AK_USER_DEFINE_SIG,	/* verified image ready in STAGING */
	FW_SIG_LOADER,							/* reset into bootloader */
	FW_SIG_ROLLBACK,						/* reset, bootloader puts the previous image back */
	FW_SIG_RESET,
	FW_SIG_DO_RESET,						/* timer: reset once logs are flushed */
	FW_SIG_TEST_STARVE,						/* crash test: keeps the CPU, starves lower tasks */
};

/* Running version: APP header in flash if valid, else the compiled version.
 * On MCU both match (header is linked into the .elf). */
extern const fw_version_t* app_version(void);

/* 1: something else drives the status LED (a demo screen): the heartbeat of
 * task_system does not toggle it. */
extern uint8_t app_led_held;

extern int app_main(void);

/* task_modbus.c */
extern void app_modbus_init(void);

/* Everything app_main() does before the kernel loop: for a port that cannot
 * give the kernel an endless loop (port/web) and calls task_run_once() itself. */
extern void app_init(void);
extern void cmd_mb(const char* args);

#endif /* __APP_H__ */
