#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Call on the LVGL task around screen changes and from its periodic hook. */
void ui_food_set_active(bool active);
void ui_food_tick(void);
#ifdef __cplusplus
}
#endif
