/**
 ******************************************************************************
 * @brief:  Console: one UART for both the text shell and the fw protocol.
 *
 *  task_poll_console (polling): read bytes -> fw_proto_feed(); bytes outside
 *  a frame go to the line editor; Enter -> post CONSOLE_SIG_LINE.
 *  task_console: execute shell commands.
 ******************************************************************************
**/

#include <string.h>

#include "ak.h"
#include "task.h"
#include "timer.h"
#include "message.h"

#include "hal.h"
#include "ak_log.h"
#include "boot_ctrl.h"
#include "fw_image.h"
#include "fw_update.h"
#include "fw_proto.h"
#include "crash_log.h"
#include "boot_core.h"

#include "app.h"
#include "task_list.h"
#if defined(APP_KIT_DEMO)
#include "ui.h"
#endif

#define CONSOLE_LINE_MAX		(AK_COMMON_MSG_DATA_SIZE - 1)

static char line_buf[CONSOLE_LINE_MAX + 1];
static uint8_t line_len;

static void console_tx(const uint8_t* data, uint32_t len) {
	while (len--) {
		hal_console_putc(*data++);
	}
}

static fw_proto_cfg_t proto_cfg;

/*----------------------------------------------------------------------------
 * shell
 *--------------------------------------------------------------------------*/
typedef struct {
	const char* name;
	const char* help;
	void (*fn)(const char* args);
} shell_cmd_t;

static void cmd_help(const char* args);

static void cmd_ver(const char* args) {
	const fw_version_t* v = app_version();
	(void)args;
	xprintf("app v%d.%d.%d build %u, kernel %s, board %s\n",
			v->major, v->minor, v->patch, v->build, AK_VERSION, hal_board_name());
}

/* full = 0: header only (fast). full = 1: CRC of the whole image, which
 * reads all of it (up to 116K over SPI for STAGING) while no task runs. */
static void print_part(const char* name, flash_part_t part, uint8_t full) {
	const flash_part_info_t* p = hal_flash_info(part);
	fw_image_hdr_t hdr;
	fw_err_t err;

	if (full) {
		err = fw_image_verify(part, &hdr);
	}
	else if (hal_flash_read(part, 0, &hdr, sizeof(hdr)) != HAL_FLASH_OK) {
		err = FW_ERR_FLASH;
	}
	else {
		err = fw_image_check_hdr(&hdr, p->size);
	}

	xprintf("  %-8s 0x%08X %6u B  ", name, p->addr, p->size);
	if (err == FW_OK) {
		xprintf("v%d.%d.%d build %u (%u B)\n", hdr.version.major, hdr.version.minor,
				hdr.version.patch, hdr.version.build, hdr.img_size);
	}
	else {
		xprintf("%s\n", fw_err_str(err));
	}
}

static void cmd_info(const char* args) {
	boot_ctrl_t ctrl;
	(void)args;

	xprintf("partitions (header check, 'verify' for the full CRC):\n");
	print_part("app", FLASH_PART_APP, 0);
	print_part("staging", FLASH_PART_STAGING, 0);
	if (hal_flash_info(FLASH_PART_BACKUP)->size) {
		print_part("backup", FLASH_PART_BACKUP, 0);
	}

	boot_ctrl_load(&ctrl);
	xprintf("boot_ctrl: cmd %d, attempts %d, last %s, installs %u%s%s\n", ctrl.cmd,
			ctrl.install_attempts, fw_err_str((fw_err_t)ctrl.last_result), ctrl.install_count,
			ctrl.trial ? ", on trial" : "", ctrl.rolled_back ? ", rolled back" : "");
	xprintf("reset reason %d, uptime %u ms\n", hal_reset_reason(), hal_millis());
}

static void cmd_verify(const char* args) {
	(void)args;
	print_part("app", FLASH_PART_APP, 1);
	print_part("staging", FLASH_PART_STAGING, 1);
	if (hal_flash_info(FLASH_PART_BACKUP)->size) {
		print_part("backup", FLASH_PART_BACKUP, 1);
	}
}

/* "fw": trial / rollback state. "fw confirm": the running image is fine.
 * "fw rollback": reboot into the image before the last update. */
static void cmd_fw(const char* args) {
	boot_ctrl_t ctrl;

	if (strcmp(args, "confirm") == 0) {
		int r = boot_ctrl_confirm();
		xprintf(r > 0 ? "image confirmed\n" : r == 0 ? "nothing to confirm: not on trial\n" : "boot_ctrl write failed\n");
		return;
	}
	if (strcmp(args, "rollback") == 0) {
		task_post_pure_msg(TASK_FW_ID, FW_SIG_ROLLBACK);
		return;
	}
	boot_ctrl_load(&ctrl);
	if (hal_flash_info(FLASH_PART_BACKUP)->size == 0) {
		xprintf("no backup partition on this board: no rollback\n");
	}
	else if (ctrl.trial) {
		xprintf("image on trial, boot %d of %d: 'fw confirm' keeps it, else the bootloader rolls back\n",
				ctrl.trial_boots, BOOT_MAX_TRIAL_BOOTS);
	}
	else {
		xprintf("image confirmed%s; 'fw rollback' returns to the backup image\n",
				ctrl.rolled_back ? " (restored by a rollback)" : "");
	}
	if (hal_flash_info(FLASH_PART_BACKUP)->size) {
		print_part("backup", FLASH_PART_BACKUP, 0);
	}
}

static void cmd_stat(const char* args) {
	(void)args;
	xprintf("pool      used  max\n");
	xprintf("pure     %4u %4u\n", get_pure_msg_pool_used(), get_pure_msg_pool_used_max());
	xprintf("common   %4u %4u\n", get_common_msg_pool_used(), get_common_msg_pool_used_max());
#if AK_DYNAMIC_MSG_POOL_SIZE > 0
	xprintf("dynamic  %4u %4u\n", get_dynamic_msg_pool_used(), get_dynamic_msg_pool_used_max());
#endif
	xprintf("timer    %4u %4u\n", get_timer_msg_pool_used(), get_timer_msg_pool_used_max());
	xprintf("stack: %u B never used, crash log: %d record(s)\n", hal_stack_unused(), crash_log_count());
}

static void cmd_crash(const char* args) {
	crash_rec_t r;
	uint8_t n;

	if (strcmp(args, "clear") == 0) {
		crash_log_clear();
		xprintf("crash log cleared\n");
		return;
	}
#if APP_CRASH_TEST
	if (strcmp(args, "test fault") == 0) {
		((void (*)(void))0xFFFFFFFFUL)();		/* HardFault */
	}
	if (strcmp(args, "test fatal") == 0) {
		FATAL("TEST", 0x55);
	}
	if (strcmp(args, "test hang") == 0) {
		xprintf("hanging in this handler, the watchdog resets in %d ms\n", APP_WDT_TIMEOUT_MS);
		for (;;) {
		}
	}
	if (strcmp(args, "test starve") == 0) {
		xprintf("starving the tasks below task_fw\n");
		task_post_pure_msg(TASK_FW_ID, FW_SIG_TEST_STARVE);
		return;
	}
#endif
	if (*args) {
		xprintf("usage: crash | crash clear"
#if APP_CRASH_TEST
				" | crash test fault|fatal|hang|starve"
#endif
				"\n");
		return;
	}

	if (crash_log_count() == 0) {
		xprintf("crash log empty\n");
		return;
	}
	for (n = 0; crash_log_read(n, &r); n++) {
		xprintf("#%d %-12s", n, crash_kind_str(r.kind));
		if (r.kind != CRASH_KIND_TASK_STALLED) {
			/* what the scheduler was running when it happened */
			if (r.task == AK_TASK_IDLE_ID) {
				xprintf(" idle/polling");
			}
			else {
				xprintf(" task %d sig %d", r.task, r.sig);
			}
		}
		switch (r.kind) {
		case CRASH_KIND_HARDFAULT:
			xprintf("  pc 0x%08X lr 0x%08X cfsr 0x%08X", r.pc, r.lr, r.info);
			break;
		case CRASH_KIND_FATAL: {
			char tag[5];
			memcpy(tag, &r.info, 4);
			tag[4] = 0;
			xprintf("  %s 0x%02X", tag, r.code);
		}
			break;
		case CRASH_KIND_TASK_STALLED:
			xprintf(" task %d got no CPU time", r.code);
			break;
		default:
			break;
		}
		xprintf("\n");
	}
}

static void cmd_reboot(const char* args) {
	(void)args;
	task_post_pure_msg(TASK_FW_ID, FW_SIG_RESET);
}

static void cmd_loader(const char* args) {
	(void)args;
	task_post_pure_msg(TASK_FW_ID, FW_SIG_LOADER);
}

#if defined(APP_RC_TX) || defined(APP_RC_RX)
extern void cmd_link(const char* args);	/* rc/rc_tx.c, rc/rc_rx.c */
#endif
#if defined(APP_FC_BENCH)
extern void cmd_fcb(const char* args);		/* port/stm32l151/fc_bench.c */
#endif
#if defined(APP_RF_TEST)
extern void cmd_rf(const char* args);		/* port/stm32l151/rf_test.c */
#endif
#if defined(APP_REMOTE)
extern void cmd_rc(const char* args);		/* remote/task_remote.c */
#endif

static const shell_cmd_t shell_cmds[] = {
	{ "help",	"list commands",				cmd_help	},
	{ "ver",	"firmware version",				cmd_ver		},
	{ "info",	"partitions + boot state",		cmd_info	},
	{ "verify",	"full CRC of app + staging",	cmd_verify	},
	{ "fw",		"trial state (fw confirm, fw rollback)",	cmd_fw	},
	{ "stat",	"pools, stack, crash count",	cmd_stat	},
	{ "crash",	"crash log (crash clear)",		cmd_crash	},
#if defined(APP_MODBUS_SLAVE)
	{ "mb",		"modbus slave status",			cmd_mb		},
#elif defined(APP_MODBUS_MASTER) || defined(APP_MODBUS_GATEWAY)
	{ "mb",		"mb read / mb write (gateway: mb = status)",	cmd_mb		},
#endif
#if defined(APP_KIT_DEMO)
	{ "ui",		"demo: ui 1|2|3|back|auto|open <name>|dump|stream",	cmd_ui	},
	{ "th",		"temperature, humidity (th csv | th log | th clear)",	cmd_th		},
	{ "plot",	"plot <number>: point on the Scope screen",	cmd_plot	},
	{ "blk",	"block program: blk | blk run | blk stop",	cmd_blk	},
	{ "i2c",	"scan I2C1, or: i2c <addr> <reg> [n] (hex)",	cmd_i2c	},
#if defined(APP_KIT_SPI_SNIFF)
	{ "spi",	"SPI sniffer on J6: spi start [mode] | spi | spi dump [from] | spi stop",	cmd_spi	},
#endif
#endif
#if defined(APP_RF_TEST)
	{ "rf",		"nRF24 listen: rf | rf <ch> <rate> [crc]",	cmd_rf	},
#endif
#if defined(APP_FC_BENCH)
	{ "fcb",	"flight control bench: fcb | fcb imu | fcb jit <s> [load]",	cmd_fcb	},
#endif
#if defined(APP_RC_TX)
	{ "link",	"remote: link | bind | cal | arm | disarm | angle | acro | stick r p y t | stick off",	cmd_link	},
#elif defined(APP_RC_RX)
	{ "link",	"receiver: link | link bind",	cmd_link	},
#endif
#if defined(APP_REMOTE)
	{ "rc",		"remote: rc | rc 1|2|3|h | rc go <n> | rc on|off | rc p <hex> | rc wd <ms>",	cmd_rc	},
#endif
	{ "reboot",	"software reset",				cmd_reboot	},
	{ "loader",	"reset into bootloader loader",	cmd_loader	},
};

#define SHELL_CMD_NUM	(sizeof(shell_cmds) / sizeof(shell_cmds[0]))

static void cmd_help(const char* args) {
	(void)args;
	for (uint32_t i = 0; i < SHELL_CMD_NUM; i++) {
		xprintf("  %-8s %s\n", shell_cmds[i].name, shell_cmds[i].help);
	}
}

static void shell_exec(char* line) {
	char* args;

	while (*line == ' ') {
		line++;
	}
	if (*line == 0) {
		return;
	}

	args = strchr(line, ' ');
	if (args) {
		*args++ = 0;
	}
	else {
		args = line + strlen(line);
	}

	for (uint32_t i = 0; i < SHELL_CMD_NUM; i++) {
		if (strcmp(line, shell_cmds[i].name) == 0) {
			shell_cmds[i].fn(args);
			return;
		}
	}
	xprintf("unknown command '%s', try 'help'\n", line);
}

/*----------------------------------------------------------------------------
 * task
 *--------------------------------------------------------------------------*/
void task_console(ak_msg_t* msg) {
	switch (msg->sig) {
	case CONSOLE_SIG_INIT:
		line_len = 0;
		proto_cfg.role = FW_ROLE_APP;
		proto_cfg.version = app_version();
		proto_cfg.tx = console_tx;
#if defined(APP_KIT_DEMO)
		proto_cfg.ext = ui_proto_ext;		/* loads files into the media store */
#endif
		fw_update_abort();
		fw_proto_init(&proto_cfg);
		xprintf("type 'help'\n> ");
		break;

	case CONSOLE_SIG_LINE: {
		char line[CONSOLE_LINE_MAX + 1];
		uint8_t len = get_data_len_common_msg(msg);

		memcpy(line, get_data_common_msg(msg), len);
		line[len] = 0;
		shell_exec(line);
		xprintf("> ");
	}
		break;

	default:
		break;
	}
}

void task_poll_console(void) {
	int c;

	while ((c = hal_console_getc()) >= 0) {
		if (fw_proto_feed((uint8_t)c, hal_millis())) {
			continue;
		}

		if (c == '\r' || c == '\n') {
			if (line_len) {
				xprintf("\n");
				/* Lines pasted faster than they are executed (or line noise)
				 * must not exhaust the pool: that is a FATAL reset. Keep one
				 * message spare for a post from an ISR. */
				if (get_common_msg_pool_used() + 2 <= AK_COMMON_MSG_POOL_SIZE) {
					task_post_common_msg(TASK_CONSOLE_ID, CONSOLE_SIG_LINE, (uint8_t*)line_buf, line_len);
				}
				else {
					xprintf("console busy, line dropped\n");
				}
				line_len = 0;
			}
		}
		else if (c == 0x08 || c == 0x7F) {
			if (line_len) {
				line_len--;
				xprintf("\b \b");
			}
		}
		else if (c >= 0x20 && c < 0x7F && line_len < CONSOLE_LINE_MAX) {
			line_buf[line_len++] = (char)c;
			xputc((uint8_t)c);
		}
	}

	switch (fw_proto_take_action()) {
	case FW_PROTO_ACT_INSTALL:
		task_post_pure_msg(TASK_FW_ID, FW_SIG_INSTALL);
		break;

	case FW_PROTO_ACT_LOADER:
		task_post_pure_msg(TASK_FW_ID, FW_SIG_LOADER);
		break;

	case FW_PROTO_ACT_RESET:
		task_post_pure_msg(TASK_FW_ID, FW_SIG_RESET);
		break;

	case FW_PROTO_ACT_ROLLBACK:
		task_post_pure_msg(TASK_FW_ID, FW_SIG_ROLLBACK);
		break;

	default:	/* RUN: already running the app */
		break;
	}
}
