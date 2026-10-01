#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CLOCK_TIMER_IDLE = 0,
    CLOCK_TIMER_RUNNING,
    CLOCK_TIMER_PAUSED,
    CLOCK_TIMER_FINISHED,
} clock_timer_state_t;

typedef enum {
    CLOCK_SW_IDLE = 0,
    CLOCK_SW_RUNNING,
    CLOCK_SW_PAUSED,
} clock_stopwatch_state_t;

typedef struct {
    clock_timer_state_t state;
    int64_t duration_us;
    int64_t remaining_us;
    int64_t deadline_us;
} clock_timer_snapshot_t;

typedef struct {
    clock_stopwatch_state_t state;
    int64_t elapsed_us;
    uint8_t lap_count;
    int64_t laps_us[8];
} clock_stopwatch_snapshot_t;

void clock_service_init(void);
void clock_timer_set_duration(int32_t h, int32_t m, int32_t s);
void clock_timer_start(void);
void clock_timer_pause(void);
void clock_timer_resume(void);
void clock_timer_cancel(void);
void clock_timer_get(clock_timer_snapshot_t *out);

void clock_stopwatch_start(void);
void clock_stopwatch_pause(void);
void clock_stopwatch_reset(void);
bool clock_stopwatch_lap(void);
void clock_stopwatch_get(clock_stopwatch_snapshot_t *out);

bool clock_service_consume_timer_finished_event(void);
int64_t clock_service_now_us(void);

void clock_format_hms(int64_t usec, char *out, uint32_t out_len);
void clock_format_stopwatch(int64_t usec, char *out, uint32_t out_len);

#ifdef __cplusplus
}
#endif
