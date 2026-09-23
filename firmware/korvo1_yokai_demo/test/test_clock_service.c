#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "clock_service.h"

static int64_t s_mock_time_us = 10000000LL; /* 10s base */

int64_t clock_service_now_us(void)
{
    return s_mock_time_us;
}

static void test_timer_deadline_and_pause_resume(void)
{
    clock_service_init();
    s_mock_time_us = 10000000LL;

    /* Set 60 second timer */
    clock_timer_set_duration(0, 1, 0);

    clock_timer_snapshot_t snap;
    clock_timer_get(&snap);
    assert(snap.state == CLOCK_TIMER_IDLE);
    assert(snap.duration_us == 60000000LL);
    assert(snap.remaining_us == 60000000LL);

    /* Start timer */
    clock_timer_start();
    clock_timer_get(&snap);
    assert(snap.state == CLOCK_TIMER_RUNNING);
    assert(snap.deadline_us == 70000000LL);

    /* Advance time by 25 seconds -> remaining should be 35 seconds */
    s_mock_time_us += 25000000LL;
    clock_timer_get(&snap);
    assert(snap.state == CLOCK_TIMER_RUNNING);
    assert(snap.remaining_us == 35000000LL);

    /* Pause timer */
    clock_timer_pause();
    clock_timer_get(&snap);
    assert(snap.state == CLOCK_TIMER_PAUSED);
    assert(snap.remaining_us == 35000000LL);

    /* Advance time by 10 seconds while paused -> remaining must stay 35s */
    s_mock_time_us += 10000000LL;
    clock_timer_get(&snap);
    assert(snap.state == CLOCK_TIMER_PAUSED);
    assert(snap.remaining_us == 35000000LL);

    /* Resume timer -> new deadline = now + 35s */
    clock_timer_resume();
    clock_timer_get(&snap);
    assert(snap.state == CLOCK_TIMER_RUNNING);
    assert(snap.deadline_us == s_mock_time_us + 35000000LL);

    /* Advance time past deadline -> state FINISHED */
    s_mock_time_us += 36000000LL;
    clock_timer_get(&snap);
    assert(snap.state == CLOCK_TIMER_FINISHED);
    assert(snap.remaining_us == 0);

    /* Verify event consumption */
    assert(clock_service_consume_timer_finished_event() == true);
    assert(clock_service_consume_timer_finished_event() == false);

    /* Cancel resets to idle */
    clock_timer_cancel();
    clock_timer_get(&snap);
    assert(snap.state == CLOCK_TIMER_IDLE);
}

static void test_stopwatch_and_laps(void)
{
    clock_service_init();
    s_mock_time_us = 10000000LL;

    clock_stopwatch_snapshot_t sw;
    clock_stopwatch_get(&sw);
    assert(sw.state == CLOCK_SW_IDLE);
    assert(sw.elapsed_us == 0);
    assert(sw.lap_count == 0);

    clock_stopwatch_start();
    s_mock_time_us += 5000000LL; /* 5s */
    clock_stopwatch_get(&sw);
    assert(sw.state == CLOCK_SW_RUNNING);
    assert(sw.elapsed_us == 5000000LL);

    /* Record laps */
    for (int i = 0; i < 8; i++) {
        s_mock_time_us += 1000000LL; /* +1s per lap */
        assert(clock_stopwatch_lap() == true);
    }
    clock_stopwatch_get(&sw);
    assert(sw.lap_count == 8);
    /* 9th lap should roll over and preserve 8 laps */
    assert(clock_stopwatch_lap() == true);
    clock_stopwatch_get(&sw);
    assert(sw.lap_count == 8);

    /* Pause and reset */
    clock_stopwatch_pause();
    clock_stopwatch_get(&sw);
    assert(sw.state == CLOCK_SW_PAUSED);

    clock_stopwatch_reset();
    clock_stopwatch_get(&sw);
    assert(sw.state == CLOCK_SW_IDLE);
    assert(sw.elapsed_us == 0);
    assert(sw.lap_count == 0);
}

static void test_formatters(void)
{
    char buf[32];
    /* 1h 2m 3s = 3723s = 3723000000 us */
    clock_format_hms(3723000000LL, buf, sizeof(buf));
    assert(strcmp(buf, "01:02:03") == 0);

    /* 65.43 seconds for stopwatch -> 00:01:05.43 */
    clock_format_stopwatch(65430000LL, buf, sizeof(buf));
    assert(strcmp(buf, "00:01:05.43") == 0);
}

int main(void)
{
    test_timer_deadline_and_pause_resume();
    test_stopwatch_and_laps();
    test_formatters();

    printf("All clock_service tests PASS\n");
    return 0;
}
