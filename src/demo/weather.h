/**
 ******************************************************************************
 * @brief:  Temperature / humidity sampling behind the weather screen
 *          (scr_weather.c). Values are in tenths: 234 = 23.4 C, 652 = 65.2 %.
 ******************************************************************************
**/

#ifndef __WEATHER_H__
#define __WEATHER_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define WEATHER_HIST		(96)		/* points of the graph */

/* Samples taken one a minute or slower are also written to a log on the SPI
 * flash (services/sys/flog.c), so the graph is back after a reset and the
 * shell can print days of readings ("th log"). The log takes up to this many
 * sectors at the end of the media store, below the block program. */
#define WEATHER_LOG_SECTORS	(16)

typedef struct {
	uint8_t  hh, mm, ss;	/* RTC time of the sample, 0xFF = no RTC */
	uint8_t  period;		/* index into the rates: 1 = 1 min, 2 = 15 min */
	int16_t  t10;
	uint16_t rh10;
} weather_rec_t;

/* Forget the RAM state, mount the log and reload the graph from it (the
 * newest samples of the rate they were taken at). weather_poll() does this
 * on its first call; tests call it to play a reset. */
extern void weather_init(void);

/* Call every UI frame: reads the sensor once a second without waiting. */
extern void weather_poll(uint32_t now_ms);

/* The log on flash: records in time order, 0 = oldest. weather_log_read()
 * returns 1, or 0 for a record cut by a power loss (skip it). */
extern uint32_t weather_log_count(void);
extern int weather_log_read(uint32_t i, weather_rec_t* rec);
extern void weather_log_clear(void);

/* Last reading. Returns 0 if the sensor does not answer. */
extern uint8_t weather_now(int16_t* t10, uint16_t* rh10);

/* The graph: weather_count() samples, 0 = oldest, one every weather_period_s(). */
extern uint8_t weather_count(void);
extern void weather_sample(uint8_t i, int16_t* t10, uint16_t* rh10);
extern uint16_t weather_period_s(void);

/* Tenths as text: 234 -> "23.4". buf: at least 8 chars. Returns buf. */
extern char* weather_fmt(char* buf, int v10);

#ifdef __cplusplus
}
#endif

#endif /* __WEATHER_H__ */
