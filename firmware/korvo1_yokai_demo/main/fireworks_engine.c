#include "fireworks_engine.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static fw_particle_t s_particles[FW_MAX_PARTICLES];
static fw_rocket_t s_rockets[FW_MAX_ROCKETS];
static fireworks_style_t s_style = FW_STYLE_KIKU;
static bool s_auto = false;
static float s_auto_wait_s = 0.7f;
static uint32_t s_launches = 0;
static uint32_t s_dropped = 0;
static uint32_t s_rng = 0x7931A55Au;

static uint32_t rng_u32(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng;
}

static float rng01(void)
{
    return (float)(rng_u32() & 0x00FFFFFFu) / 16777215.0f;
}

static fw_particle_t *alloc_particle(void)
{
    size_t oldest = 0;
    float max_ratio = -1.0f;
    for (size_t i = 0; i < FW_MAX_PARTICLES; ++i) {
        if (!s_particles[i].active) return &s_particles[i];
        float ratio = s_particles[i].age_s / s_particles[i].life_s;
        if (ratio > max_ratio) {
            max_ratio = ratio;
            oldest = i;
        }
    }
    /* Pool full: recycle the most faded particle so new explosions are never dropped */
    s_particles[oldest].active = false;
    return &s_particles[oldest];
}

static fw_rocket_t *alloc_rocket(void)
{
    for (size_t i = 0; i < FW_MAX_ROCKETS; ++i) {
        if (!s_rockets[i].active) return &s_rockets[i];
    }
    return NULL;
}

static void spawn_burst(float x, float y, fireworks_style_t style)
{
    if (style == FW_STYLE_BOTAN) {
        /* Concentric dual-shell peony */
        /* Outer shell: 22 vibrant petals */
        static const uint32_t outer_colors[] = {0xFF1744, 0xE040FB, 0xFF4081, 0xFF5252};
        for (int i = 0; i < 22; ++i) {
            fw_particle_t *p = alloc_particle();
            if (!p) continue;
            float angle = (float)(2.0 * M_PI * i / 22.0) + (rng01() - 0.5f) * 0.1f;
            float speed = 115.0f + 25.0f * rng01();
            memset(p, 0, sizeof(*p));
            p->x = x; p->y = y;
            p->prev_x = x; p->prev_y = y;
            p->vx = cosf(angle) * speed;
            p->vy = sinf(angle) * speed;
            p->age_s = 0.0f;
            p->life_s = 0.85f + 0.35f * rng01();
            p->rgb888 = outer_colors[i % 4];
            p->size = 4;
            p->style = (uint8_t)style;
            p->sub_type = 0;
            p->active = true;
        }
        /* Inner core (Shin): 14 bright cyan/gold petals */
        static const uint32_t core_colors[] = {0x00E5FF, 0xFFEA00, 0x76FF03};
        for (int i = 0; i < 14; ++i) {
            fw_particle_t *p = alloc_particle();
            if (!p) continue;
            float angle = (float)(2.0 * M_PI * i / 14.0) + (rng01() - 0.5f) * 0.15f;
            float speed = 55.0f + 20.0f * rng01();
            memset(p, 0, sizeof(*p));
            p->x = x; p->y = y;
            p->prev_x = x; p->prev_y = y;
            p->vx = cosf(angle) * speed;
            p->vy = sinf(angle) * speed;
            p->age_s = 0.0f;
            p->life_s = 0.70f + 0.25f * rng01();
            p->rgb888 = core_colors[i % 3];
            p->size = 3;
            p->style = (uint8_t)style;
            p->sub_type = 1;
            p->active = true;
        }
    } else if (style == FW_STYLE_YANAGI) {
        /* Fountain weeping willow cascade */
        static const uint32_t yanagi_colors[] = {0xE0F7FA, 0x80DEEA, 0xFFF9C4, 0xB2EBF2};
        int count = 32;
        for (int i = 0; i < count; ++i) {
            fw_particle_t *p = alloc_particle();
            if (!p) continue;
            float angle = (float)(M_PI * (0.08f + 0.84f * (float)i / (float)count));
            float speed = 65.0f + 60.0f * rng01();
            memset(p, 0, sizeof(*p));
            p->x = x; p->y = y;
            p->prev_x = x; p->prev_y = y;
            p->vx = cosf(angle) * speed * 1.35f;
            p->vy = -sinf(angle) * speed * 1.15f;
            p->age_s = 0.0f;
            p->life_s = 1.7f + 0.6f * rng01();
            p->rgb888 = yanagi_colors[i % 4];
            p->size = 3;
            p->style = (uint8_t)style;
            p->sub_type = 0;
            p->active = true;
        }
    } else {
        /* Kiku: Radial Chrysanthemum with sparkling comet trails */
        static const uint32_t kiku_colors[] = {0xFFD54F, 0xFFF0A0, 0xFFCA28, 0xFFFFFF};
        int count = 36;
        for (int i = 0; i < count; ++i) {
            fw_particle_t *p = alloc_particle();
            if (!p) continue;
            float angle = (float)(2.0 * M_PI * i / (float)count) + (rng01() - 0.5f) * 0.12f;
            float speed = 80.0f + 65.0f * rng01();
            memset(p, 0, sizeof(*p));
            p->x = x; p->y = y;
            p->prev_x = x; p->prev_y = y;
            p->vx = cosf(angle) * speed;
            p->vy = sinf(angle) * speed;
            p->age_s = 0.0f;
            p->life_s = 0.95f + 0.45f * rng01();
            p->rgb888 = kiku_colors[i % 4];
            p->size = 3;
            p->style = (uint8_t)style;
            p->sub_type = 0;
            p->active = true;
        }
    }
    s_launches++;
}

void fireworks_engine_init(void)
{
    memset(s_particles, 0, sizeof(s_particles));
    memset(s_rockets, 0, sizeof(s_rockets));
    s_style = FW_STYLE_KIKU;
    s_auto = false;
    s_auto_wait_s = 0.7f;
    s_launches = 0;
    s_dropped = 0;
    s_rng = 0x7931A55Au;
}

void fireworks_engine_reset_visuals(void)
{
    memset(s_particles, 0, sizeof(s_particles));
    memset(s_rockets, 0, sizeof(s_rockets));
    s_auto = false;
    s_auto_wait_s = 0.7f;
}

void fireworks_engine_set_style(fireworks_style_t style)
{
    if (style <= FW_STYLE_YANAGI) s_style = style;
}

void fireworks_engine_set_auto(bool enabled)
{
    s_auto = enabled;
    if (enabled && s_auto_wait_s > 0.4f) s_auto_wait_s = 0.15f;
}

void fireworks_engine_tap(float x, float y)
{
    spawn_burst(x, y, s_style);
}

void fireworks_engine_launch(float x, float start_y, float target_y)
{
    fw_rocket_t *r = alloc_rocket();
    if (!r) return;

    memset(r, 0, sizeof(*r));
    r->x = x;
    r->y = start_y;
    r->vx = (rng01() - 0.5f) * 18.0f;
    r->vy = -190.0f - rng01() * 55.0f;
    r->target_y = target_y;
    r->style = s_style;
    r->active = true;
}

void fireworks_engine_update(float dt_s)
{
    if (dt_s <= 0.0f) return;
    if (dt_s > 0.05f) dt_s = 0.05f;

    for (size_t i = 0; i < FW_MAX_ROCKETS; ++i) {
        fw_rocket_t *r = &s_rockets[i];
        if (!r->active) continue;

        r->x += r->vx * dt_s;
        r->y += r->vy * dt_s;
        r->vy += 52.0f * dt_s;

        if (r->y <= r->target_y || r->vy >= -20.0f) {
            float bx = r->x;
            float by = r->y;
            fireworks_style_t style = r->style;
            r->active = false;
            spawn_burst(bx, by, style);
        }
    }

    for (size_t i = 0; i < FW_MAX_PARTICLES; ++i) {
        fw_particle_t *p = &s_particles[i];
        if (!p->active) continue;

        p->age_s += dt_s;
        if (p->age_s >= p->life_s) {
            p->active = false;
            continue;
        }

        p->prev_x = p->x;
        p->prev_y = p->y;

        p->x += p->vx * dt_s;
        p->y += p->vy * dt_s;

        float gravity = 32.0f;
        float drag = 0.972f;
        if (p->style == FW_STYLE_BOTAN) {
            gravity = 44.0f;
            drag = 0.968f;
        } else if (p->style == FW_STYLE_YANAGI) {
            gravity = 92.0f;
            drag = 0.992f;
        }
        p->vy += gravity * dt_s;
        p->vx *= drag;
        p->vy *= drag;

        if (p->x < -20 || p->x > 820 || p->y > 450) {
            p->active = false;
        }
    }

    if (s_auto) {
        s_auto_wait_s -= dt_s;
        if (s_auto_wait_s <= 0.0f) {
            float x = 80.0f + rng01() * 640.0f;
            float target = 105.0f + rng01() * 195.0f;
            fireworks_engine_launch(x, 395.0f, target);
            s_auto_wait_s = 0.85f + rng01() * 0.70f;
        }
    }
}

const fw_particle_t *fireworks_engine_particles(size_t *count)
{
    if (count) *count = FW_MAX_PARTICLES;
    return s_particles;
}

const fw_rocket_t *fireworks_engine_rockets(size_t *count)
{
    if (count) *count = FW_MAX_ROCKETS;
    return s_rockets;
}

void fireworks_engine_get_stats(fireworks_stats_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->launches = s_launches;
    out->dropped_particles = s_dropped;
    out->auto_mode = s_auto;
    out->selected_style = s_style;

    for (size_t i = 0; i < FW_MAX_PARTICLES; ++i)
        if (s_particles[i].active) out->active_particles++;
    for (size_t i = 0; i < FW_MAX_ROCKETS; ++i)
        if (s_rockets[i].active) out->active_rockets++;
}
