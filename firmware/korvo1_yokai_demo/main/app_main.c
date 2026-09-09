#include "esp_log.h"

#include "app_state.h"
#include "board_ui.h"

static const char *TAG = "yokai_demo";

void app_main(void)
{
    app_state_t state;
    app_state_init(&state);
    ESP_ERROR_CHECK(board_ui_start(&state));
    ESP_LOGI(TAG, "Korvo-1 Yokai demo UI started");
}
