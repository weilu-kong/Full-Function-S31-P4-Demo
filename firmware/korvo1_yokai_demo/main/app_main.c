#include "esp_log.h"
#include "nvs_flash.h"

#include "app_state.h"
#include "board_ui.h"
#include "synth_service.h"
#include "voice_service.h"
#include "weather_service.h"
#include "vision_service.h"
#include "vision_camera.h"

static const char *TAG = "yokai_demo";

#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_lv_adapter.h"
#include "bsp/esp32_s31_korvo_1.h"

#include "storage_service.h"
#include "app_health.h"
#include "app_regression.h"

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Mount /storage SPIFFS partition (safe, no auto-formatting) */
    esp_err_t storage_err = app_storage_init();
    if (storage_err != ESP_OK) {
        ESP_LOGE(TAG, "Storage initialization failed: %s (face DB will be read-only/unavailable)",
                 esp_err_to_name(storage_err));
    }

    /* Initialize synthesizer audio engine with esp-audio-effects */
    ESP_ERROR_CHECK(synth_service_init());

    if (!voice_service_start()) {
        ESP_LOGE(TAG, "Voice control unavailable: %s", voice_service_error());
    }

    /* Initialize weather service with SNTP & Open-Meteo polling */
    ESP_ERROR_CHECK(weather_service_init());

    vision_memory_checkpoint("M0 boot before Vision init");
    /* Initialize vision service skeleton (Camera/AI deferred to UI entry) */
    esp_err_t vision_err = vision_service_init();
    if (vision_err != ESP_OK) {
        ESP_LOGW(TAG, "Vision service init failed: %s", esp_err_to_name(vision_err));
    }

    /* board_ui keeps this pointer after app_main returns. */
    static app_state_t state;
    app_state_init(&state);
    ESP_ERROR_CHECK(board_ui_start(&state));
    ESP_LOGI(TAG, "Korvo-1 Yokai demo UI started");
    vision_memory_checkpoint("M0 UI ready");
    app_health_log_heap("M0 UI ready");
    ESP_LOGI(TAG, "PSRAM boot/UI ready: free=%u largest=%u SIMD_largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_SIMD));

    app_regression_start();
}
