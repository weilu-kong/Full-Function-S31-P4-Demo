#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_YOKAI_ART_FIREWORKS_B = 0,
    UI_YOKAI_ART_CLOCK_B,
    UI_YOKAI_ART_CALCULATOR_A,
} ui_yokai_art_scene_t;

/*
 * Attach one zero-bitmap, procedural Yokai scene to an LVGL object.
 * The object should normally be 800x480, non-scrollable, non-clickable,
 * with its normal background/border opacity set to transparent.
 */
void ui_yokai_art_attach(lv_obj_t *obj, ui_yokai_art_scene_t scene);

/* Draw a scene directly inside an existing LV_EVENT_DRAW_MAIN callback. */
void ui_yokai_art_draw(lv_layer_t *layer, const lv_area_t *area,
                       ui_yokai_art_scene_t scene);

#ifdef __cplusplus
}
#endif
