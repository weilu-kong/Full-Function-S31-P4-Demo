#pragma once

#include <esp_err.h>
#include "lvgl.h"
#include "weather_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decompress all 6 Yokai background JPEG images into 16-byte aligned
 *        PSRAM buffers using ESP32-S31 hardware acceleration, and populate
 *        the corresponding global lv_image_dsc_t descriptors.
 *
 * Must be called in ui_init() before screen creation.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t ui_images_init(void);

/** Decode the requested weather background into the shared 800x480 buffer. */
esp_err_t ui_weather_background_load(weather_cond_t condition, bool is_day);

typedef enum {
    UI_APP_BG_NONE = 0,
    UI_APP_BG_FIREWORKS,
    UI_APP_BG_CLOCK,
    UI_APP_BG_CALCULATOR,
} ui_app_bg_t;

/** Shared 800x480 image descriptor for the 3 sub-apps (reusing 1 PSRAM buffer). */
extern lv_image_dsc_t ui_app_shared_bg;

/** Decode requested app background into the shared 750KB PSRAM buffer. */
esp_err_t ui_app_background_load(ui_app_bg_t bg);

/** Free the shared app background buffer to return 750KB to PSRAM (e.g. for Vision AI). */
void ui_app_background_free(void);

#ifdef __cplusplus
}
#endif
