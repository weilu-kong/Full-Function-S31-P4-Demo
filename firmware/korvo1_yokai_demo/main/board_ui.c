#include "board_ui.h"
#include "ui/ui.h"
#include "ui/ui_drawer.h"
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
#include "synth_service.h"
#include "vision_service.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "board_ui";

static lv_display_t *s_disp = NULL;
static lv_indev_t *s_touch_indev = NULL;

static bool s_wifi_ready = false;
static bool s_wifi_enabled = false;
static bool s_wifi_scan_running = false;
static volatile uint32_t s_wifi_scan_generation = 0;
static wifi_ap_record_t s_wifi_aps[MAX_WIFI_APS];
static uint16_t s_wifi_ap_count = 0;
static volatile bool s_wifi_results_dirty = false;
static volatile bool s_wifi_scan_failed = false;
static uint8_t s_wifi_last_disconnect_reason = 0;
static board_wifi_state_t s_wifi_state = BOARD_WIFI_DISCONNECTED;
static char s_connecting_ssid[33] = {0};
static char s_connected_ip[16] = {0};

static volatile uint32_t s_ui_tick_enter_count = 0;
static volatile uint32_t s_ui_tick_exit_count = 0;
static volatile uint32_t s_ui_last_enter_ms = 0;
static volatile uint32_t s_ui_last_exit_ms = 0;
static volatile uint32_t s_ui_max_tick_us = 0;
static volatile ui_health_stage_t s_ui_stage = UI_HEALTH_STAGE_IDLE;
static TaskHandle_t s_ui_health_task_handle = NULL;

void board_ui_health_set_stage(ui_health_stage_t stage)
{
    s_ui_stage = stage;
}

static void ui_health_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t last_enter_ms = s_ui_last_enter_ms;
        uint32_t last_exit_ms = s_ui_last_exit_ms;
        uint32_t enter_age = last_enter_ms && now >= last_enter_ms ? now - last_enter_ms : 0;
        uint32_t exit_age = last_exit_ms && now >= last_exit_ms ? now - last_exit_ms : 0;
        ui_screen_t screen = ui_get_current_screen();
        vision_state_t vision_state = vision_service_get_state();
        ESP_LOGI(TAG,
                 "[UI_HEALTH] up=%u enter=%u exit=%u enter_age=%u exit_age=%u stage=%u max_us=%u screen=%u vstate=%u",
                 (unsigned)now, (unsigned)s_ui_tick_enter_count, (unsigned)s_ui_tick_exit_count,
                 (unsigned)enter_age, (unsigned)exit_age, (unsigned)s_ui_stage,
                 (unsigned)s_ui_max_tick_us, (unsigned)screen, (unsigned)vision_state);
        if (screen == UI_SCREEN_VISION && vision_state == VISION_STATE_RUNNING &&
            s_ui_last_exit_ms && exit_age > 2000) {
            ESP_LOGE(TAG, "UI/LVGL STALL: exit_age=%u ms stage=%u enter=%u exit=%u",
                     (unsigned)exit_age, (unsigned)s_ui_stage,
                     (unsigned)s_ui_tick_enter_count, (unsigned)s_ui_tick_exit_count);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void ui_lv_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    int64_t started_us = esp_timer_get_time();
    s_ui_tick_enter_count++;
    s_ui_last_enter_ms = (uint32_t)(started_us / 1000);
    s_ui_stage = UI_HEALTH_STAGE_TIMER_ENTER;

    /* Global pull-down drawer gesture detection across all screens */
    s_ui_stage = UI_HEALTH_STAGE_TOUCH;
    if (s_touch_indev) {
        static bool s_pull_tracking = false;
        static lv_point_t s_pull_start = {0, 0};
        static bool s_pull_opened = false;

        lv_indev_state_t st = lv_indev_get_state(s_touch_indev);
        lv_point_t p;
        lv_indev_get_point(s_touch_indev, &p);

        if (st == LV_INDEV_STATE_PRESSED) {
            if (!s_pull_tracking) {
                s_pull_tracking = true;
                s_pull_start = p;
                s_pull_opened = false;
            } else if (!s_pull_opened) {
                int32_t dy = p.y - s_pull_start.y;
                int32_t dx = p.x - s_pull_start.x;
                if (dx < 0) dx = -dx;

                if (!ui_drawer_is_visible()) {
                    /* If started in upper area (y <= 150) and pulled down by >= 35 pixels with dominant vertical motion */
                    if (s_pull_start.y <= 150 && dy >= 35 && dy > (dx * 12) / 10) {
                        s_pull_opened = true;
                        ESP_LOGI(TAG, "Pull-down gesture detected (start_y=%d, dy=%d): Opening Quick Settings",
                                 (int)s_pull_start.y, (int)dy);
                        ui_drawer_set_visible(true);
                    }
                } else {
                    /* If drawer is already open and user swipes upward by >= 35 pixels */
                    if (dy <= -35 && -dy > (dx * 12) / 10) {
                        s_pull_opened = true;
                        ESP_LOGI(TAG, "Swipe-up gesture detected (start_y=%d, dy=%d): Closing Quick Settings",
                                 (int)s_pull_start.y, (int)dy);
                        ui_drawer_set_visible(false);
                    }
                }
            }
        } else {
            s_pull_tracking = false;
            s_pull_opened = false;
        }
    }

    s_ui_stage = UI_HEALTH_STAGE_UI_PERIODIC;
    ui_tick_periodic();

    s_ui_stage = UI_HEALTH_STAGE_TIMER_EXIT;
    uint32_t elapsed_us = (uint32_t)(esp_timer_get_time() - started_us);
    if (elapsed_us > s_ui_max_tick_us) s_ui_max_tick_us = elapsed_us;
    s_ui_tick_exit_count++;
    s_ui_last_exit_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_ui_stage = UI_HEALTH_STAGE_IDLE;
}

static void on_board_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "Wi-Fi STA started");
            wifi_config_t cfg = {0};
            if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && strlen((char *)cfg.sta.ssid) > 0) {
                strncpy(s_connecting_ssid, (char *)cfg.sta.ssid, sizeof(s_connecting_ssid) - 1);
                s_wifi_state = BOARD_WIFI_CONNECTING;
                s_wifi_results_dirty = true;
                (void)esp_wifi_connect();
            }
        } else if (id == WIFI_EVENT_STA_CONNECTED) {
            wifi_event_sta_connected_t *conn = (wifi_event_sta_connected_t *)data;
            if (conn && conn->ssid_len > 0) {
                int len = conn->ssid_len < 32 ? conn->ssid_len : 32;
                memcpy(s_connecting_ssid, conn->ssid, len);
                s_connecting_ssid[len] = '\0';
            }
            ESP_LOGI(TAG, "Wi-Fi STA connected to AP: %s", s_connecting_ssid);
            s_wifi_state = BOARD_WIFI_CONNECTING;
            s_wifi_results_dirty = true;
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)data;
            uint8_t reason = disconn ? disconn->reason : 0;
            ESP_LOGW(TAG, "Wi-Fi STA disconnected (reason=%u)", (unsigned)reason);
            if (reason == WIFI_REASON_STA_LEAVING) {
                /* Station explicitly left or called esp_wifi_disconnect.
                 * This is an intentional manual disconnect, not a connection failure.
                 * If we are currently actively connecting, do not overwrite connecting state.
                 */
                if (s_wifi_state != BOARD_WIFI_CONNECTING) {
                    s_wifi_state = BOARD_WIFI_DISCONNECTED;
                }
                s_wifi_last_disconnect_reason = 0;
            } else if (s_wifi_state == BOARD_WIFI_CONNECTING) {
                s_wifi_state = BOARD_WIFI_FAILED;
                s_wifi_last_disconnect_reason = reason;
            } else {
                s_wifi_state = BOARD_WIFI_DISCONNECTED;
            }
            s_connected_ip[0] = '\0';
            weather_service_set_offline();
            s_wifi_results_dirty = true;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_connected_ip, sizeof(s_connected_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_state = BOARD_WIFI_CONNECTED;
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            strncpy(s_connecting_ssid, (const char *)ap_info.ssid, sizeof(s_connecting_ssid) - 1);
            s_connecting_ssid[sizeof(s_connecting_ssid) - 1] = '\0';
        }
        ESP_LOGI(TAG, "Wi-Fi STA got IP: %s (SSID: %s)", s_connected_ip, s_connecting_ssid);
        s_wifi_results_dirty = true;
        weather_service_trigger_refresh();
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

    static bool s_events_registered = false;
    if (!s_events_registered) {
        (void)esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  on_board_wifi_event, NULL, NULL);
        (void)esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  on_board_wifi_event, NULL, NULL);
        s_events_registered = true;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        s_wifi_state = BOARD_WIFI_CONNECTED;
        snprintf(s_connected_ip, sizeof(s_connected_ip), IPSTR, IP2STR(&ip_info.ip));
        wifi_config_t cfg = {0};
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && strlen((char *)cfg.sta.ssid) > 0) {
            strncpy(s_connecting_ssid, (char *)cfg.sta.ssid, sizeof(s_connecting_ssid) - 1);
        }
    }

    return ESP_OK;
}

static void wifi_scan_worker_task(void *arg)
{
    const uint32_t generation = (uint32_t)(uintptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(150));
    if (generation != s_wifi_scan_generation) {
        s_wifi_scan_running = false;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = board_ui_wifi_ensure_started();
    if (err != ESP_OK) {
        s_wifi_scan_failed = true;
        s_wifi_results_dirty = true;
        s_wifi_scan_running = false;
        vTaskDelete(NULL);
        return;
    }

    uint16_t ap_count = 0;
    int retries = 3;
    wifi_scan_config_t scan_cfg = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 150,
    };
    while (retries > 0) {
        if (generation != s_wifi_scan_generation) {
            s_wifi_scan_running = false;
            vTaskDelete(NULL);
            return;
        }
        err = esp_wifi_scan_start(&scan_cfg, true);
        if (err == ESP_OK) {
            esp_wifi_scan_get_ap_num(&ap_count);
            if (ap_count > 0 || retries == 1) {
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(150));
        retries--;
    }

    if (generation != s_wifi_scan_generation) {
        s_wifi_scan_running = false;
        vTaskDelete(NULL);
        return;
    }

    if (err == ESP_OK) {
        uint16_t fetch_count = ap_count > MAX_WIFI_APS ? MAX_WIFI_APS : ap_count;
        if (fetch_count > 0) {
            memset(s_wifi_aps, 0, sizeof(s_wifi_aps));
            esp_wifi_scan_get_ap_records(&fetch_count, s_wifi_aps);
        }
        s_wifi_ap_count = fetch_count;
        s_wifi_scan_failed = false;
    } else {
        s_wifi_scan_failed = true;
    }

    s_wifi_results_dirty = true;
    s_wifi_scan_running = false;
    vTaskDelete(NULL);
}

esp_err_t board_ui_wifi_scan_async(void)
{
    if (s_wifi_scan_running) {
        return ESP_OK;
    }
    s_wifi_scan_running = true;
    s_wifi_scan_failed = false;
    s_wifi_results_dirty = true;
    uint32_t generation = ++s_wifi_scan_generation;
    if (xTaskCreate(wifi_scan_worker_task, "wifi_scan", 4096,
                    (void *)(uintptr_t)generation, 5, NULL) != pdPASS) {
        s_wifi_scan_running = false;
        s_wifi_scan_failed = true;
        s_wifi_results_dirty = true;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void board_ui_wifi_get_info(board_wifi_info_t *out_info)
{
    if (!out_info) return;
    out_info->state = s_wifi_state;
    strncpy(out_info->connected_ssid, s_connecting_ssid, sizeof(out_info->connected_ssid) - 1);
    out_info->connected_ssid[sizeof(out_info->connected_ssid) - 1] = '\0';
    strncpy(out_info->ip_str, s_connected_ip, sizeof(out_info->ip_str) - 1);
    out_info->ip_str[sizeof(out_info->ip_str) - 1] = '\0';
    out_info->last_disconnect_reason = s_wifi_last_disconnect_reason;
    out_info->scan_running = s_wifi_scan_running;
    out_info->scan_failed = s_wifi_scan_failed;
    out_info->ap_count = s_wifi_ap_count;
    out_info->connected_rssi = -100;
    if (s_wifi_state == BOARD_WIFI_CONNECTED) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            out_info->connected_rssi = ap_info.rssi;
            if (out_info->connected_ssid[0] == '\0') {
                strncpy(out_info->connected_ssid, (const char *)ap_info.ssid, sizeof(out_info->connected_ssid) - 1);
                out_info->connected_ssid[sizeof(out_info->connected_ssid) - 1] = '\0';
            }
            if (s_connecting_ssid[0] == '\0') {
                strncpy(s_connecting_ssid, (const char *)ap_info.ssid, sizeof(s_connecting_ssid) - 1);
                s_connecting_ssid[sizeof(s_connecting_ssid) - 1] = '\0';
            }
        } else {
            out_info->connected_rssi = -60;
        }
    }
    memcpy(out_info->aps, s_wifi_aps, sizeof(s_wifi_aps));
}

bool board_ui_wifi_is_dirty(void)
{
    return s_wifi_results_dirty;
}

void board_ui_wifi_clear_dirty(void)
{
    s_wifi_results_dirty = false;
}

void board_ui_wifi_connect(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) return;
    board_ui_wifi_ensure_started();
    s_wifi_enabled = true;
    s_wifi_state = BOARD_WIFI_CONNECTING;
    s_wifi_last_disconnect_reason = 0;
    s_wifi_results_dirty = true;
    strncpy(s_connecting_ssid, ssid, sizeof(s_connecting_ssid) - 1);
    s_connecting_ssid[sizeof(s_connecting_ssid) - 1] = '\0';
    s_connected_ip[0] = '\0';

    ++s_wifi_scan_generation;
    s_wifi_scan_running = false;

    wifi_config_t wifi_cfg = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) == ESP_OK &&
        strcmp((char *)wifi_cfg.sta.ssid, ssid) == 0) {
        /* Matching saved network: only update password if caller supplied a non-empty password */
        if (password && strlen(password) > 0) {
            strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
            wifi_cfg.sta.password[sizeof(wifi_cfg.sta.password) - 1] = '\0';
        }
    } else {
        memset(&wifi_cfg, 0, sizeof(wifi_cfg));
        strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
        if (password && strlen(password) > 0) {
            strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
        }
    }
    /* Configure PMF, SAE, and threshold for maximum AP compatibility (Wi-Fi 6 / WPA2 / WPA3) */
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;
    wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    (void)esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        s_wifi_state = BOARD_WIFI_FAILED;
        s_wifi_results_dirty = true;
    }
}

void board_ui_wifi_reconnect_saved(void)
{
    board_ui_wifi_ensure_started();
    wifi_config_t cfg = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && strlen((char *)cfg.sta.ssid) > 0) {
        cfg.sta.pmf_cfg.capable = true;
        cfg.sta.pmf_cfg.required = false;
        cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
        (void)esp_wifi_set_config(WIFI_IF_STA, &cfg);
        s_wifi_enabled = true;
        s_wifi_state = BOARD_WIFI_CONNECTING;
        s_wifi_last_disconnect_reason = 0;
        strncpy(s_connecting_ssid, (char *)cfg.sta.ssid, sizeof(s_connecting_ssid) - 1);
        s_wifi_results_dirty = true;
        (void)esp_wifi_connect();
    }
}

void board_ui_wifi_forget_saved(void)
{
    wifi_config_t cfg = {0};
    (void)esp_wifi_disconnect();
    (void)esp_wifi_set_config(WIFI_IF_STA, &cfg);
    s_connecting_ssid[0] = '\0';
    s_connected_ip[0] = '\0';
    s_wifi_state = BOARD_WIFI_DISCONNECTED;
    s_wifi_results_dirty = true;
    weather_service_set_offline();
}

void board_ui_wifi_disconnect(void)
{
    (void)esp_wifi_disconnect();
    s_wifi_state = BOARD_WIFI_DISCONNECTED;
    s_connecting_ssid[0] = '\0';
    s_connected_ip[0] = '\0';
    s_wifi_results_dirty = true;
    weather_service_set_offline();
}

bool board_ui_wifi_is_saved(const char *ssid)
{
    if (!ssid) return false;
    wifi_config_t cfg = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) return false;
    if (strlen((char *)cfg.sta.ssid) == 0 || strcmp(ssid, (char *)cfg.sta.ssid) != 0) return false;

    /* Check if AP is known to be open */
    bool ap_is_open = false;
    for (int i = 0; i < s_wifi_ap_count; i++) {
        if (strcmp((const char *)s_wifi_aps[i].ssid, ssid) == 0) {
            if (s_wifi_aps[i].authmode == WIFI_AUTH_OPEN) {
                ap_is_open = true;
            }
            break;
        }
    }
    if (ap_is_open) {
        return true;
    }
    /* For secured networks, it is only validly saved if password length >= 8 */
    return strlen((char *)cfg.sta.password) >= 8;
}

void board_ui_wifi_set_enabled(bool enabled)
{
    s_wifi_enabled = enabled;
}

bool board_ui_wifi_is_enabled(void)
{
    return s_wifi_enabled;
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

    /* 4. Register RGB Display with True Triple Full Frame Buffering in PSRAM */
    esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel,
        io,
        800,
        480,
        ESP_LV_ADAPTER_ROTATE_0
    );
    disp_cfg.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_FULL;
    disp_cfg.profile.buffer_height = 480;
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

    /* 7. Initialize Yokai UI and install periodic timer under LVGL lock (16ms / 60Hz) */
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        ui_init(s_disp, state);
        (void)lv_timer_create(ui_lv_timer_cb, 16, NULL);
        esp_lv_adapter_unlock();
    } else {
        ESP_LOGE(TAG, "failed to acquire esp_lv_adapter_lock");
        return ESP_FAIL;
    }

    if (xTaskCreate(ui_health_task, "ui_health", 3072, NULL, 1,
                    &s_ui_health_task_handle) != pdPASS) {
        ESP_LOGW(TAG, "UI health telemetry task unavailable");
    }

    /* 8. Ensure Wi-Fi STA is started in background */
    (void)board_ui_wifi_ensure_started();

    ESP_LOGI(TAG, "Korvo-1 LVGL v9 + ThorVG Yokai UI successfully started");
    return ESP_OK;
}

esp_err_t board_ui_switch_screen(ui_screen_t target)
{
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        ui_switch_screen(target);
        esp_lv_adapter_unlock();
        return ESP_OK;
    }
    return ESP_FAIL;
}
