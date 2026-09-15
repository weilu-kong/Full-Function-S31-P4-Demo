#pragma once

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_synth_home_cb_t)(void);

/** Create the Yokai Synthesizer Groovebox Screen. */
lv_obj_t *ui_synth_screen_create(ui_synth_home_cb_t home_cb);

/** Periodic update for synth oscilloscope waveform and knob animations. */
void ui_synth_update_waveform(void);

#ifdef __cplusplus
}
#endif
