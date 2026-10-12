/**
 ******************************************************************************
 * @brief:  Leveled, timestamped logging through xprintf (port console).
 *          Compile-time filter: -DAK_LOG_LEVEL=AK_LOG_LEVEL_WARN ...
 *          Disable all: -DAK_LOG_LEVEL=AK_LOG_LEVEL_NONE (macros expand to nothing).
 *
 *          LOG_I("FW", "got %d bytes\n", n);
 *          -> "[    1234] I FW: got 128 bytes"
 ******************************************************************************
**/

#ifndef __AK_LOG_H__
#define __AK_LOG_H__

#include "ak_port.h"

#define AK_LOG_LEVEL_NONE		(0)
#define AK_LOG_LEVEL_ERROR		(1)
#define AK_LOG_LEVEL_WARN		(2)
#define AK_LOG_LEVEL_INFO		(3)
#define AK_LOG_LEVEL_DEBUG		(4)

#ifndef AK_LOG_LEVEL
#define AK_LOG_LEVEL			AK_LOG_LEVEL_INFO
#endif

/* "[    1234] I FW: " in one function instead of an xprintf call with four
 * arguments at every LOG_x site (about 35 of them: 300 B of flash). */
extern void ak_log_hdr(char lvl, const char* tag);

#define AK_LOG_RAW(lvl, tag, ...) do { \
	ak_log_hdr((lvl), (tag)); \
	xprintf(__VA_ARGS__); \
} while (0)

#if (AK_LOG_LEVEL >= AK_LOG_LEVEL_ERROR)
#define LOG_E(tag, ...)			AK_LOG_RAW('E', tag, __VA_ARGS__)
#else
#define LOG_E(tag, ...)			do {} while (0)
#endif

#if (AK_LOG_LEVEL >= AK_LOG_LEVEL_WARN)
#define LOG_W(tag, ...)			AK_LOG_RAW('W', tag, __VA_ARGS__)
#else
#define LOG_W(tag, ...)			do {} while (0)
#endif

#if (AK_LOG_LEVEL >= AK_LOG_LEVEL_INFO)
#define LOG_I(tag, ...)			AK_LOG_RAW('I', tag, __VA_ARGS__)
#else
#define LOG_I(tag, ...)			do {} while (0)
#endif

#if (AK_LOG_LEVEL >= AK_LOG_LEVEL_DEBUG)
#define LOG_D(tag, ...)			AK_LOG_RAW('D', tag, __VA_ARGS__)
#else
#define LOG_D(tag, ...)			do {} while (0)
#endif

#endif /* __AK_LOG_H__ */
