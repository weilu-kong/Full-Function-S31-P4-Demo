#include "board_ui.h"
#include "ui/ui.h"
#include "esp_lv_adapter.h"
#include "bsp/esp32_s31_korvo_1.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "weather_service.h"
#include <string.h>

static const char *TAG = "board_ui";

static lv_display_t *s_disp = NULL;
static lv_indev_t *s_touch_indev = NULL;
static bool s_wifi_ready = false;
static bool s_wifi_enabled = false;

static void ui_lv_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_tick_periodic();
}

static void on_board_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "Wi-Fi STA started");
            (void)esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGW(TAG, "Wi-Fi STA disconnected");
            weather_service_set_offline();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Wi-Fi STA got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t board_ui_wifi_ensure_started(void)
{
    if (s_wifi_ready) {
        s_wifi_enabled = true;
        return ESP_OK;
    }
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        if (esp_netif_create_default_wifi_sta() == NULL) {
            return ESP_FAIL;
        }
    }
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    config.sta_disconnected_pm = false;
    if ((err = esp_wifi_init(&config)) != ESP_OK ||
        (err = esp_wifi_set_storage(WIFI_STORAGE_FLASH)) != ESP_OK ||
        (err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (err = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK ||
        (err = esp_wifi_start()) != ESP_OK) {
        return err;
    }
    s_wifi_ready = true;
    s_wifi_enabled = true;
    ESP_LOGI(TAG, "Wi-Fi STA started successfully");

    static bool s_events_registered;
    if (!s_events_registered) {
        (void)esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  on_board_wifi_event, NULL, NULL);
        (void)esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  on_board_wifi_event, NULL, NULL);
        s_events_registered = true;
    }
    return ESP_OK;
}

esp_err_t board_ui_start(app_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 1. Initialize Korvo-1 RGB LCD Hardware via BSP */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    bsp_display_config_t display_config = {0};
    ESP_RETURN_ON_ERROR(bsp_display_new(&display_config, &panel, &io), TAG,
                        "create Korvo-1 RGB panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG,
                        "turn on Korvo-1 RGB panel");

    /* 2. Initialize Korvo-1 GT1151 Touch Hardware via BSP */
    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t touch_error = bsp_touch_new(NULL, &touch);
    if (touch_error != ESP_OK) {
        ESP_LOGW(TAG, "GT1151 touch unavailable: %s", esp_err_to_name(touch_error));
        touch = NULL;
    }

    /* 3. Configure and Initialize ESP LVGL Adapter */
    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_stack_size = 32768; /* 32KB stack for ThorVG Lottie rendering */
    adapter_cfg.task_priority = 6;
    adapter_cfg.tick_period_ms = 1;
    adapter_cfg.stack_in_psram = true;
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_cfg), TAG, "init esp_lvgl_adapter");

    /* 4. Register RGB Display with Triple Buffering in PSRAM */
    esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel,
        io,
        800,
        480,
        ESP_LV_ADAPTER_ROTATE_0
    );
    disp_cfg.profile.buffer_height = 60;
    disp_cfg.profile.use_psram = true;
    s_disp = esp_lv_adapter_register_display(&disp_cfg);
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "failed to register RGB display with esp_lvgl_adapter");
        return ESP_FAIL;
    }

    /* 5. Register GT1151 Touch Input */
    if (touch != NULL) {
        esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(s_disp, touch);
        s_touch_indev = esp_lv_adapter_register_touch(&touch_cfg);
        if (s_touch_indev == NULL) {
            ESP_LOGW(TAG, "failed to register touch indev with esp_lvgl_adapter");
        }
    }

    /* 6. Start the LVGL adapter worker task */
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "start esp_lvgl_adapter");

    /* 7. Initialize Yokai UI and install periodic timer under LVGL lock */
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        ui_init(s_disp, state);
        (void)lv_timer_create(ui_lv_timer_cb, 50, NULL);
        esp_lv_adapter_unlock();
    } else {
        ESP_LOGE(TAG, "failed to acquire esp_lv_adapter_lock");
        return ESP_FAIL;
    }

    /* 8. Ensure Wi-Fi STA is started in background */
    (void)board_ui_wifi_ensure_started();

    ESP_LOGI(TAG, "Korvo-1 LVGL v9 + ThorVG Yokai UI successfully started");
    return ESP_OK;
}
