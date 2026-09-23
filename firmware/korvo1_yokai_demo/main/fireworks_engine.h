#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FW_MAX_PARTICLES 160
#define FW_MAX_ROCKETS   4

typedef enum {
    FW_STYLE_KIKU = 0,
    FW_STYLE_BOTAN,
    FW_STYLE_YANAGI,
} fireworks_style_t;

typedef struct {
    float x, y;
    float vx, vy;
    float age_s;
    float life_s;
    uint32_t rgb888;
    uint8_t size;
    uint8_t style;
    bool active;
} fw_particle_t;

typedef struct {
    float x, y;
    float vx, vy;
    float target_y;
    fireworks_style_t style;
    bool active;
} fw_rocket_t;

typedef struct {
    uint32_t launches;
    uint32_t dropped_particles;
    uint16_t active_particles;
    uint8_t active_rockets;
    bool auto_mode;
    fireworks_style_t selected_style;
} fireworks_stats_t;

void fireworks_engine_init(void);
void fireworks_engine_reset_visuals(void);
void fireworks_engine_set_style(fireworks_style_t style);
void fireworks_engine_set_auto(bool enabled);
void fireworks_engine_tap(float x, float y);
void fireworks_engine_launch(float x, float start_y, float target_y);
void fireworks_engine_update(float dt_s);
const fw_particle_t *fireworks_engine_particles(size_t *count);
const fw_rocket_t *fireworks_engine_rockets(size_t *count);
void fireworks_engine_get_stats(fireworks_stats_t *out);

#ifdef __cplusplus
}
#endif
