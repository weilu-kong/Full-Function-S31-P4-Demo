#pragma once

#include <esp_err.h>
#include "lvgl.h"

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

#ifdef __cplusplus
}
#endif
