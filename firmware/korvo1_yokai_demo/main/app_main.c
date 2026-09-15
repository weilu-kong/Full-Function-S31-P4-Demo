#include "esp_log.h"
#include "nvs_flash.h"

#include "app_state.h"
#include "board_ui.h"
#include "synth_service.h"
#include "weather_service.h"

static const char *TAG = "yokai_demo";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Initialize synthesizer audio engine with esp-audio-effects */
    ESP_ERROR_CHECK(synth_service_init());

    /* Initialize weather service with SNTP & Open-Meteo polling */
    ESP_ERROR_CHECK(weather_service_init());

    /* board_ui keeps this pointer after app_main returns. */
    static app_state_t state;
    app_state_init(&state);
    ESP_ERROR_CHECK(board_ui_start(&state));
    ESP_LOGI(TAG, "Korvo-1 Yokai demo UI started");
}
