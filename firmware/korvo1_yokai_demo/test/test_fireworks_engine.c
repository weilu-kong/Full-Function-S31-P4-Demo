#include <assert.h>
#include <stdio.h>
#include "fireworks_engine.h"

static void test_fixed_pool_and_limits(void)
{
    fireworks_engine_init();

    fireworks_stats_t stats;
    fireworks_engine_get_stats(&stats);
    assert(stats.active_particles == 0);
    assert(stats.active_rockets == 0);

    /* Launch 4 rockets (max capacity) */
    for (int i = 0; i < 4; i++) {
        fireworks_engine_launch(100.0f + i * 50.0f, 400.0f, 150.0f);
    }
    fireworks_engine_get_stats(&stats);
    assert(stats.active_rockets == 4);

    /* 5th rocket should be ignored (no rocket slot) */
    fireworks_engine_launch(500.0f, 400.0f, 150.0f);
    fireworks_engine_get_stats(&stats);
    assert(stats.active_rockets == 4);

    /* Direct tap triggers immediate explosion particles */
    fireworks_engine_tap(400.0f, 200.0f);
    fireworks_engine_get_stats(&stats);
    assert(stats.active_particles > 0);
    assert(stats.active_particles <= FW_MAX_PARTICLES);

    /* Saturate particle pool with multiple taps */
    for (int i = 0; i < 20; i++) {
        fireworks_engine_tap(200.0f + i * 10.0f, 200.0f);
    }
    fireworks_engine_get_stats(&stats);
    assert(stats.active_particles <= FW_MAX_PARTICLES);
    assert(stats.dropped_particles > 0); /* Particles dropped due to pool exhaustion */
}

static void test_particle_lifecycle_and_gravity(void)
{
    fireworks_engine_init();
    fireworks_engine_set_auto(false);

    /* Tap creates burst */
    fireworks_engine_tap(400.0f, 240.0f);

    size_t count = 0;
    const fw_particle_t *p = fireworks_engine_particles(&count);
    assert(count == FW_MAX_PARTICLES);

    int active_before = 0;
    for (size_t i = 0; i < count; i++) {
        if (p[i].active) active_before++;
    }
    assert(active_before > 0);

    /* Advance by 0.1s */
    fireworks_engine_update(0.1f);

    /* Advance by 5.0s in 50ms increments -> all particles should exceed life_s and deactivate */
    for (int step = 0; step < 100; step++) {
        fireworks_engine_update(0.05f);
    }

    fireworks_stats_t stats;
    fireworks_engine_get_stats(&stats);
    assert(stats.active_particles == 0);
}

static void test_styles_and_auto_mode(void)
{
    fireworks_engine_init();

    fireworks_engine_set_style(FW_STYLE_YANAGI);
    fireworks_stats_t stats;
    fireworks_engine_get_stats(&stats);
    assert(stats.selected_style == FW_STYLE_YANAGI);

    fireworks_engine_set_auto(true);
    fireworks_engine_get_stats(&stats);
    assert(stats.auto_mode == true);

    /* In auto mode, updating triggers launches */
    for (int step = 0; step < 30; step++) {
        fireworks_engine_update(0.05f);
    }
    fireworks_engine_get_stats(&stats);
    assert(stats.launches > 0);

    fireworks_engine_reset_visuals();
    fireworks_engine_get_stats(&stats);
    assert(stats.active_particles == 0);
    assert(stats.active_rockets == 0);
}

int main(void)
{
    test_fixed_pool_and_limits();
    test_particle_lifecycle_and_gravity();
    test_styles_and_auto_mode();

    printf("All fireworks_engine tests PASS\n");
    return 0;
}
