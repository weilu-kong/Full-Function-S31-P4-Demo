#include "clock_service.h"
#include <stdio.h>
#include <string.h>

#ifndef HOST_TEST
#include "esp_timer.h"
#endif

typedef struct {
    clock_timer_state_t state;
    int64_t duration_us;
    int64_t remaining_us;
    int64_t deadline_us;
    bool finished_event;
} timer_state_t;

typedef struct {
    clock_stopwatch_state_t state;
    int64_t accumulated_us;
    int64_t running_since_us;
    uint8_t lap_count;
    int64_t laps_us[8];
} stopwatch_state_t;

static timer_state_t s_timer;
static stopwatch_state_t s_sw;

__attribute__((weak)) int64_t clock_service_now_us(void)
{
#ifndef HOST_TEST
    return esp_timer_get_time();
#else
    return 0;
#endif
}

static int64_t clamp_nonnegative(int64_t v) { return v < 0 ? 0 : v; }

static void timer_refresh(void)
{
    if (s_timer.state != CLOCK_TIMER_RUNNING) return;
    int64_t now = clock_service_now_us();
    s_timer.remaining_us = s_timer.deadline_us - now;
    if (s_timer.remaining_us <= 0) {
        s_timer.remaining_us = 0;
        s_timer.state = CLOCK_TIMER_FINISHED;
        s_timer.finished_event = true;
    }
}

static int64_t sw_elapsed_now(void)
{
    if (s_sw.state == CLOCK_SW_RUNNING) {
        return s_sw.accumulated_us +
               (clock_service_now_us() - s_sw.running_since_us);
    }
    return s_sw.accumulated_us;
}

void clock_service_init(void)
{
    memset(&s_timer, 0, sizeof(s_timer));
    memset(&s_sw, 0, sizeof(s_sw));
    s_timer.state = CLOCK_TIMER_IDLE;
    s_sw.state = CLOCK_SW_IDLE;
}

void clock_timer_set_duration(int32_t h, int32_t m, int32_t s)
{
    if (h < 0) h = 0;
    if (m < 0) m = 0;
    if (s < 0) s = 0;
    if (m > 59) m = 59;
    if (s > 59) s = 59;
    if (h > 99) h = 99;

    int64_t sec = (int64_t)h * 3600 + (int64_t)m * 60 + s;
    s_timer.duration_us = sec * 1000000LL;
    s_timer.remaining_us = s_timer.duration_us;
    s_timer.deadline_us = 0;
    s_timer.state = CLOCK_TIMER_IDLE;
    s_timer.finished_event = false;
}

void clock_timer_start(void)
{
    if (s_timer.remaining_us <= 0) return;
    s_timer.deadline_us = clock_service_now_us() + s_timer.remaining_us;
    s_timer.state = CLOCK_TIMER_RUNNING;
    s_timer.finished_event = false;
}

void clock_timer_pause(void)
{
    timer_refresh();
    if (s_timer.state == CLOCK_TIMER_RUNNING) {
        s_timer.remaining_us =
            clamp_nonnegative(s_timer.deadline_us - clock_service_now_us());
        s_timer.state = CLOCK_TIMER_PAUSED;
    }
}

void clock_timer_resume(void)
{
    if (s_timer.state != CLOCK_TIMER_PAUSED || s_timer.remaining_us <= 0) return;
    s_timer.deadline_us = clock_service_now_us() + s_timer.remaining_us;
    s_timer.state = CLOCK_TIMER_RUNNING;
}

void clock_timer_cancel(void)
{
    s_timer.remaining_us = s_timer.duration_us;
    s_timer.deadline_us = 0;
    s_timer.state = CLOCK_TIMER_IDLE;
    s_timer.finished_event = false;
}

void clock_timer_get(clock_timer_snapshot_t *out)
{
    if (!out) return;
    timer_refresh();
    out->state = s_timer.state;
    out->duration_us = s_timer.duration_us;
    out->remaining_us = s_timer.remaining_us;
    out->deadline_us = s_timer.deadline_us;
}

void clock_stopwatch_start(void)
{
    if (s_sw.state == CLOCK_SW_RUNNING) return;
    s_sw.running_since_us = clock_service_now_us();
    s_sw.state = CLOCK_SW_RUNNING;
}

void clock_stopwatch_pause(void)
{
    if (s_sw.state != CLOCK_SW_RUNNING) return;
    s_sw.accumulated_us = sw_elapsed_now();
    s_sw.state = CLOCK_SW_PAUSED;
}

void clock_stopwatch_reset(void)
{
    memset(&s_sw, 0, sizeof(s_sw));
    s_sw.state = CLOCK_SW_IDLE;
}

bool clock_stopwatch_lap(void)
{
    if (s_sw.state != CLOCK_SW_RUNNING) return false;
    int64_t elapsed = sw_elapsed_now();

    if (s_sw.lap_count < 8) {
        s_sw.laps_us[s_sw.lap_count++] = elapsed;
    } else {
        memmove(&s_sw.laps_us[0], &s_sw.laps_us[1], 7 * sizeof(int64_t));
        s_sw.laps_us[7] = elapsed;
    }
    return true;
}

void clock_stopwatch_get(clock_stopwatch_snapshot_t *out)
{
    if (!out) return;
    out->state = s_sw.state;
    out->elapsed_us = sw_elapsed_now();
    out->lap_count = s_sw.lap_count;
    memcpy(out->laps_us, s_sw.laps_us, sizeof(out->laps_us));
}

bool clock_service_consume_timer_finished_event(void)
{
    timer_refresh();
    bool v = s_timer.finished_event;
    s_timer.finished_event = false;
    return v;
}

void clock_format_hms(int64_t usec, char *out, uint32_t out_len)
{
    if (!out || out_len == 0) return;
    if (usec < 0) usec = 0;
    int64_t sec = (usec + 999999LL) / 1000000LL;
    int h = (int)(sec / 3600);
    int m = (int)((sec / 60) % 60);
    int s = (int)(sec % 60);
    snprintf(out, out_len, "%02d:%02d:%02d", h, m, s);
}

void clock_format_stopwatch(int64_t usec, char *out, uint32_t out_len)
{
    if (!out || out_len == 0) return;
    if (usec < 0) usec = 0;
    int64_t cs = usec / 10000LL;
    int hh = (int)(cs / 360000);
    int mm = (int)((cs / 6000) % 60);
    int ss = (int)((cs / 100) % 60);
    int cc = (int)(cs % 100);
    snprintf(out, out_len, "%02d:%02d:%02d.%02d", hh, mm, ss, cc);
}
