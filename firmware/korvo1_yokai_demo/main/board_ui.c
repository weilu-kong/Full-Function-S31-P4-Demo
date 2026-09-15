#include "board_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/display.h"
#include "bsp/esp32_s31_korvo_1.h"
#include "bsp/touch.h"
#include "synth_service.h"
#include "weather_service.h"

#define GSP_BUNDLE_ENABLE_RAW_IDS 1
#define GSP_BUNDLE_ENABLE_LEGACY_NAMES 1
#include "bundle_gsp.h"

static const char *TAG = "board_ui";
static bool s_wifi_ready;
static bool s_wifi_scan_running;
static volatile uint32_t s_wifi_scan_generation;
static esp_gsp_list_t s_bt_list = ESP_GSP_LIST_NONE;

#define MAX_WIFI_APS 14
static wifi_ap_record_t s_wifi_aps[MAX_WIFI_APS];
static uint16_t s_wifi_ap_count;
static volatile bool s_wifi_results_dirty;
static volatile bool s_wifi_scan_failed;
static uint8_t s_wifi_page;
static uint8_t s_wifi_last_disconnect_reason;
static uint16_t s_live_scene;
static uint16_t s_settings_return_scene = GSP_BUNDLE_SCENE_KORVO_HOME;
static app_event_t s_settings_return_event = APP_EVENT_HOME;
static bool s_reopen_drawer;
static bool s_wifi_enabled;
static bool s_weather_status_dirty = true;
static int32_t s_weather_volume = -1;
static weather_theme_t s_weather_theme = (weather_theme_t)-1;

typedef enum {
    WIFI_CONN_STATE_DISCONNECTED,
    WIFI_CONN_STATE_CONNECTING,
    WIFI_CONN_STATE_CONNECTED,
    WIFI_CONN_STATE_FAILED,
} wifi_conn_state_t;

static wifi_conn_state_t s_wifi_conn_state = WIFI_CONN_STATE_DISCONNECTED;
static char s_connecting_ssid[33];
static char s_connected_ip[20];
static wifi_ap_record_t s_selected_ap;

#define MAX_BT_DEVS 10
static const char *const s_bt_devs[MAX_BT_DEVS] = {
    "   周辺機器",
    "   接続済み　なし",
    "   新しい機器を確認中…",
    "   雷神太鼓",
    "   妖怪通信",
    "   狸屋道具",
    "   河童音具",
    "   言霊マイク",
    "   --",
    "   --",
};


static gsp_err_t bt_list_bind_cb(esp_gsp_handle_t gsp, esp_gsp_row_t row,
                                 uint32_t index, void *ctx)
{
    (void)ctx;
    if (index < MAX_BT_DEVS) {
        (void)esp_gsp_row_text(gsp, row, s_bt_devs[index]);
    } else {
        (void)esp_gsp_row_text(gsp, row, "   --");
    }
    return GSP_OK;
}

static void wifi_set_status(esp_gsp_handle_t ui, const char *status)
{
    (void)esp_gsp_set_text(ui, GSP_KORVO_WIFI_BIND_WIFI_SCAN_STATUS, status);
}

static void on_board_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI(TAG, "Wi-Fi STA associated with AP");
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)data;
            uint8_t reason = disconn ? disconn->reason : 0;
            ESP_LOGW(TAG, "Wi-Fi STA disconnected (reason=%d)", (int)reason);
            if (s_wifi_conn_state == WIFI_CONN_STATE_CONNECTING) {
                s_wifi_conn_state = WIFI_CONN_STATE_FAILED;
                s_wifi_last_disconnect_reason = reason;
            } else {
                s_wifi_conn_state = WIFI_CONN_STATE_DISCONNECTED;
            }
            s_connected_ip[0] = '\0';
            weather_service_set_offline();
            s_wifi_results_dirty = true;
            s_weather_status_dirty = true;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        s_wifi_conn_state = WIFI_CONN_STATE_CONNECTED;
        snprintf(s_connected_ip, sizeof(s_connected_ip), IPSTR, IP2STR(&event->ip_info.ip));
        wifi_config_t cfg;
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && strlen((char *)cfg.sta.ssid) > 0) {
            strncpy(s_connecting_ssid, (char *)cfg.sta.ssid, sizeof(s_connecting_ssid) - 1);
        }
        ESP_LOGI(TAG, "Wi-Fi STA got IP: %s (SSID: %s)", s_connected_ip, s_connecting_ssid);
        s_wifi_results_dirty = true;
        s_weather_status_dirty = true;
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
    ESP_LOGI(TAG, "Wi-Fi STA started successfully");

    static bool s_events_registered;
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
        s_wifi_conn_state = WIFI_CONN_STATE_CONNECTED;
        snprintf(s_connected_ip, sizeof(s_connected_ip), IPSTR, IP2STR(&ip_info.ip));
        wifi_config_t cfg;
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && strlen((char *)cfg.sta.ssid) > 0) {
            strncpy(s_connecting_ssid, (char *)cfg.sta.ssid, sizeof(s_connecting_ssid) - 1);
        }
    }

    return ESP_OK;
}

static void wifi_connect_to_ap(const char *ssid, const char *password)
{
    if (ssid == NULL || strlen(ssid) == 0) {
        return;
    }
    ESP_LOGI(TAG, "Connecting to Wi-Fi SSID '%s'...", ssid);
    s_wifi_enabled = true;
    s_wifi_conn_state = WIFI_CONN_STATE_CONNECTING;
    s_weather_status_dirty = true;
    strncpy(s_connecting_ssid, ssid, sizeof(s_connecting_ssid) - 1);
    s_connected_ip[0] = '\0';
    s_wifi_results_dirty = true;

    /* Cancel any ongoing scan */
    ++s_wifi_scan_generation;
    s_wifi_scan_running = false;

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    if (password != NULL && strlen(password) > 0) {
        strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    }
    (void)esp_wifi_disconnect();
    (void)esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        s_wifi_conn_state = WIFI_CONN_STATE_FAILED;
        s_wifi_results_dirty = true;
    }
}

static bool wifi_is_saved_ssid(const char *ssid)
{
    wifi_config_t cfg = {0};
    return ssid && esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK &&
           strcmp(ssid, (const char *)cfg.sta.ssid) == 0;
}

static void wifi_reconnect_saved(void)
{
    s_wifi_enabled = true;
    s_wifi_conn_state = WIFI_CONN_STATE_CONNECTING;
    s_weather_status_dirty = true;
    s_wifi_results_dirty = true;
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saved Wi-Fi reconnect failed: %s", esp_err_to_name(err));
        s_wifi_conn_state = WIFI_CONN_STATE_FAILED;
    }
}

static void wifi_forget_saved(void)
{
    wifi_config_t cfg = {0};
    (void)esp_wifi_disconnect();
    (void)esp_wifi_set_config(WIFI_IF_STA, &cfg);
    s_connecting_ssid[0] = '\0';
    s_connected_ip[0] = '\0';
    s_wifi_conn_state = WIFI_CONN_STATE_DISCONNECTED;
    s_wifi_results_dirty = true;
    s_weather_status_dirty = true;
}

static void wifi_scan_task(void *arg)
{
    const uint32_t generation = (uint32_t)(uintptr_t)arg;

    vTaskDelay(pdMS_TO_TICKS(200));
    if (generation != s_wifi_scan_generation) {
        s_wifi_scan_running = false;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = board_ui_wifi_ensure_started();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_start_once failed: %s", esp_err_to_name(err));
        if (generation == s_wifi_scan_generation) {
            s_wifi_scan_failed = true;
            s_wifi_results_dirty = true;
        }
        s_wifi_scan_running = false;
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

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
        }
        if (err == ESP_OK) {
            if (ap_count > 0 || retries == 1) {
                break;
            }
        }
        ESP_LOGW(TAG, "Wi-Fi scan retry (err=%s, ap_count=%u)", esp_err_to_name(err), (unsigned)ap_count);
        vTaskDelay(pdMS_TO_TICKS(200));
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
        s_wifi_results_dirty = true;
    } else {
        ESP_LOGE(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
        s_wifi_scan_failed = true;
        s_wifi_results_dirty = true;
    }

    s_wifi_scan_running = false;
    vTaskDelete(NULL);
}

static void wifi_scan_async(esp_gsp_handle_t ui)
{
    if (s_wifi_scan_running) {
        return;
    }
    s_wifi_scan_running = true;
    s_wifi_ap_count = 0;
    s_wifi_scan_failed = false;
    s_wifi_results_dirty = true;
    uint32_t generation = ++s_wifi_scan_generation;
    if (xTaskCreate(wifi_scan_task, "wifi_scan", 6144,
                    (void *)(uintptr_t)generation, 5, NULL) != pdPASS) {
        s_wifi_scan_running = false;
        wifi_set_status(ui, "Wi-Fi ERROR");
        return;
    }
}

static bool s_bluetooth_enabled = false;
/* A newly created scene starts its toggle at its authored default.  Ignore it
 * until the shared Wi-Fi state has been pushed into that scene. */
static bool s_wifi_toggle_sync_pending;

static bool wifi_details_action_match(const esp_gsp_event_t *event)
{
    switch (event->scene_id) {
    case GSP_BUNDLE_SCENE_KORVO_HOME:
        return event->action_id == GSP_KORVO_HOME_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_SYNTH:
        return event->action_id == GSP_KORVO_SYNTH_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_WEATHER:
        return event->action_id == GSP_KORVO_WEATHER_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_VOICE:
        return event->action_id == GSP_KORVO_VOICE_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_OBJECT:
        return event->action_id == GSP_KORVO_OBJECT_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_LIGHTING:
        return event->action_id == GSP_KORVO_LIGHTING_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_CLOCK_TIMER:
        return event->action_id == GSP_KORVO_CLOCK_TIMER_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_CALCULATOR:
        return event->action_id == GSP_KORVO_CALCULATOR_ACT_ID_WIFI_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_FOOD:
        return event->action_id == GSP_KORVO_FOOD_ACT_ID_WIFI_DETAILS;
    default:
        return false;
    }
}

static bool is_wifi_details_event(const esp_gsp_event_t *event)
{
    return s_wifi_enabled && wifi_details_action_match(event);
}

static bool bluetooth_details_action_match(const esp_gsp_event_t *event)
{
    switch (event->scene_id) {
    case GSP_BUNDLE_SCENE_KORVO_HOME:
        return event->action_id == GSP_KORVO_HOME_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_SYNTH:
        return event->action_id == GSP_KORVO_SYNTH_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_WEATHER:
        return event->action_id == GSP_KORVO_WEATHER_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_VOICE:
        return event->action_id == GSP_KORVO_VOICE_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_OBJECT:
        return event->action_id == GSP_KORVO_OBJECT_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_LIGHTING:
        return event->action_id == GSP_KORVO_LIGHTING_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_CLOCK_TIMER:
        return event->action_id == GSP_KORVO_CLOCK_TIMER_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_CALCULATOR:
        return event->action_id == GSP_KORVO_CALCULATOR_ACT_ID_BLUETOOTH_DETAILS;
    case GSP_BUNDLE_SCENE_KORVO_FOOD:
        return event->action_id == GSP_KORVO_FOOD_ACT_ID_BLUETOOTH_DETAILS;
    default:
        return false;
    }
}

static bool is_bluetooth_details_event(const esp_gsp_event_t *event)
{
    return s_bluetooth_enabled && bluetooth_details_action_match(event);
}

static bool is_wifi_toggle_event(const esp_gsp_event_t *event)
{
    switch (event->scene_id) {
    case GSP_BUNDLE_SCENE_KORVO_HOME:
        return event->action_id == GSP_KORVO_HOME_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_SYNTH:
        return event->action_id == GSP_KORVO_SYNTH_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_WEATHER:
        return event->action_id == GSP_KORVO_WEATHER_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_VOICE:
        return event->action_id == GSP_KORVO_VOICE_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_OBJECT:
        return event->action_id == GSP_KORVO_OBJECT_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_LIGHTING:
        return event->action_id == GSP_KORVO_LIGHTING_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_CLOCK_TIMER:
        return event->action_id == GSP_KORVO_CLOCK_TIMER_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_CALCULATOR:
        return event->action_id == GSP_KORVO_CALCULATOR_ACT_ID_WIFI_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_FOOD:
        return event->action_id == GSP_KORVO_FOOD_ACT_ID_WIFI_TOGGLE;
    default:
        return false;
    }
}

static bool is_bluetooth_toggle_event(const esp_gsp_event_t *event)
{
    switch (event->scene_id) {
    case GSP_BUNDLE_SCENE_KORVO_HOME:
        return event->action_id == GSP_KORVO_HOME_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_SYNTH:
        return event->action_id == GSP_KORVO_SYNTH_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_WEATHER:
        return event->action_id == GSP_KORVO_WEATHER_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_VOICE:
        return event->action_id == GSP_KORVO_VOICE_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_OBJECT:
        return event->action_id == GSP_KORVO_OBJECT_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_LIGHTING:
        return event->action_id == GSP_KORVO_LIGHTING_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_CLOCK_TIMER:
        return event->action_id == GSP_KORVO_CLOCK_TIMER_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_CALCULATOR:
        return event->action_id == GSP_KORVO_CALCULATOR_ACT_ID_BLUETOOTH_TOGGLE;
    case GSP_BUNDLE_SCENE_KORVO_FOOD:
        return event->action_id == GSP_KORVO_FOOD_ACT_ID_BLUETOOTH_TOGGLE;
    default:
        return false;
    }
}

static void sync_wifi_controls(esp_gsp_handle_t ui)
{
    esp_gsp_err_t err = esp_gsp_component_set_enabled(
        ui, GSP_OBJ_KEY_WIFI_CARD, s_wifi_enabled);
    if (err != ESP_GSP_OK) {
        ESP_LOGE(TAG, "set wifi_card enabled failed: %d", (int)err);
    }
}

static void sync_wifi_toggle(esp_gsp_handle_t ui)
{
    esp_gsp_err_t err = esp_gsp_component_set_checked(
        ui, GSP_OBJ_KEY_WIFI_ENABLED, s_wifi_enabled);
    if (err == ESP_GSP_OK) {
        s_wifi_toggle_sync_pending = true;
    }
}

static void sync_bluetooth_controls(esp_gsp_handle_t ui)
{
    esp_gsp_err_t err = esp_gsp_component_set_enabled(
        ui, GSP_OBJ_KEY_BLUETOOTH_CARD, s_bluetooth_enabled);
    if (err != ESP_GSP_OK) {
        ESP_LOGE(TAG, "set bt_card enabled failed: %d", (int)err);
    }
}

static void update_drawer_quick_controls(esp_gsp_handle_t ui)
{
    sync_wifi_toggle(ui);
    sync_wifi_controls(ui);
    sync_bluetooth_controls(ui);
}

/* ponytail: 50ms poll of toggle checked. GSP toggle does not emit CALL
 * (callback-only and click→call both produced zero events). Upgrade if
 * GSP adds a value-change callback. */
static void apply_toggle_from_widget(esp_gsp_handle_t ui)
{
    if (s_wifi_toggle_sync_pending) {
        bool wifi_checked = s_wifi_enabled;
        esp_gsp_err_t err = esp_gsp_component_get_checked(
            ui, GSP_OBJ_KEY_WIFI_ENABLED, &wifi_checked);
        if (err != ESP_GSP_OK || wifi_checked == s_wifi_enabled) {
            s_wifi_toggle_sync_pending = false;
        }
        return;
    }

    bool wifi_checked = s_wifi_enabled;
    bool bt_checked = s_bluetooth_enabled;
    esp_gsp_err_t wifi_err = esp_gsp_component_get_checked(
        ui, GSP_OBJ_KEY_WIFI_ENABLED, &wifi_checked);
    esp_gsp_err_t bt_err = esp_gsp_component_get_checked(
        ui, GSP_OBJ_KEY_BLUETOOTH_ENABLED, &bt_checked);

    if (wifi_err == ESP_GSP_OK && wifi_checked != s_wifi_enabled) {
        s_wifi_enabled = wifi_checked;
        if (!s_wifi_enabled) {
            (void)esp_wifi_disconnect();
            s_wifi_conn_state = WIFI_CONN_STATE_DISCONNECTED;
            s_connected_ip[0] = '\0';
            weather_service_set_offline();
            s_wifi_results_dirty = true;
        }
        sync_wifi_controls(ui);
    }
    if (bt_err == ESP_GSP_OK && bt_checked != s_bluetooth_enabled) {
        s_bluetooth_enabled = bt_checked;
        sync_bluetooth_controls(ui);
    }
}

static void apply_wifi_scan_ui(esp_gsp_handle_t ui)
{
    if (s_live_scene != GSP_BUNDLE_SCENE_KORVO_WIFI) {
        return;
    }
    static const uint16_t s_ap_binds[7] = {
        GSP_KORVO_WIFI_BIND_AP_TEXT_0,
        GSP_KORVO_WIFI_BIND_AP_TEXT_1,
        GSP_KORVO_WIFI_BIND_AP_TEXT_2,
        GSP_KORVO_WIFI_BIND_AP_TEXT_3,
        GSP_KORVO_WIFI_BIND_AP_TEXT_4,
        GSP_KORVO_WIFI_BIND_AP_TEXT_5,
        GSP_KORVO_WIFI_BIND_AP_TEXT_6,
    };
    static const uint16_t s_ap_locks[7] = {
        GSP_KORVO_WIFI_BIND_WIFI_LOCK_0,
        GSP_KORVO_WIFI_BIND_WIFI_LOCK_1,
        GSP_KORVO_WIFI_BIND_WIFI_LOCK_2,
        GSP_KORVO_WIFI_BIND_WIFI_LOCK_3,
        GSP_KORVO_WIFI_BIND_WIFI_LOCK_4,
        GSP_KORVO_WIFI_BIND_WIFI_LOCK_5,
        GSP_KORVO_WIFI_BIND_WIFI_LOCK_6,
    };
    for (int i = 0; i < 7; i++) {
        int ap_idx = (int)s_wifi_page * 7 + i;
        char ap_label[128];
        if (ap_idx < s_wifi_ap_count && strlen((const char *)s_wifi_aps[ap_idx].ssid) > 0) {
            (void)esp_gsp_set_visible(ui, s_ap_locks[i],
                                      s_wifi_aps[ap_idx].authmode != WIFI_AUTH_OPEN);
            const char *ssid = (const char *)s_wifi_aps[ap_idx].ssid;
            bool is_connected = (s_wifi_conn_state == WIFI_CONN_STATE_CONNECTED &&
                                 s_connecting_ssid[0] != '\0' &&
                                 strcmp(ssid, s_connecting_ssid) == 0);
            if (is_connected) {
                snprintf(ap_label, sizeof(ap_label), "%d: %s  %d dBm  [設定済み・接続済み]",
                         ap_idx + 1, ssid, s_wifi_aps[ap_idx].rssi);
            } else if (wifi_is_saved_ssid(ssid)) {
                snprintf(ap_label, sizeof(ap_label), "%d: %s  %d dBm  [設定済み]",
                         ap_idx + 1, ssid, s_wifi_aps[ap_idx].rssi);
            } else {
                snprintf(ap_label, sizeof(ap_label), "%d: %s  %d dBm",
                         ap_idx + 1, ssid, s_wifi_aps[ap_idx].rssi);
            }
        } else {
            (void)esp_gsp_set_visible(ui, s_ap_locks[i], false);
            snprintf(ap_label, sizeof(ap_label), "%d: --", ap_idx + 1);
        }
        (void)esp_gsp_set_text(ui, s_ap_binds[i], ap_label);
    }

    char page_buf[32];
    int total_pages = (s_wifi_ap_count > 7) ? 2 : 1;
    snprintf(page_buf, sizeof(page_buf), "%d / %d ページ", (int)s_wifi_page + 1, total_pages);
    (void)esp_gsp_set_text(ui, GSP_KORVO_WIFI_BIND_WIFI_PAGE_INFO, page_buf);

    if (s_wifi_conn_state == WIFI_CONN_STATE_CONNECTED) {
        char status[96];
        if (s_connected_ip[0] != '\0') {
            snprintf(status, sizeof(status), "接続済み: %s", s_connected_ip);
        } else {
            snprintf(status, sizeof(status), "接続済み: %s", s_connecting_ssid);
        }
        wifi_set_status(ui, status);
    } else if (s_wifi_conn_state == WIFI_CONN_STATE_CONNECTING) {
        char status[96];
        snprintf(status, sizeof(status), "接続中… %s", s_connecting_ssid);
        wifi_set_status(ui, status);
    } else if (s_wifi_conn_state == WIFI_CONN_STATE_FAILED) {
        char err_msg[96];
        if (s_wifi_last_disconnect_reason == 15 ||
            s_wifi_last_disconnect_reason == 202 ||
            s_wifi_last_disconnect_reason == 204 ||
            s_wifi_last_disconnect_reason == 2) {
            snprintf(err_msg, sizeof(err_msg), "接続できません (PASS ERROR: %u)",
                     (unsigned)s_wifi_last_disconnect_reason);
        } else if (s_wifi_last_disconnect_reason == 201) {
            snprintf(err_msg, sizeof(err_msg), "接続できません (NO AP: %u)",
                     (unsigned)s_wifi_last_disconnect_reason);
        } else {
            snprintf(err_msg, sizeof(err_msg), "接続できません (ERR: %u)",
                     (unsigned)s_wifi_last_disconnect_reason);
        }
        wifi_set_status(ui, err_msg);
    } else if (s_wifi_scan_failed) {
        wifi_set_status(ui, "Wi-Fi ERROR");
    } else if (s_wifi_scan_running) {
        wifi_set_status(ui, "Wi-Fi 確認中…");
    } else if (s_wifi_ap_count > 0) {
        char status[48];
        snprintf(status, sizeof(status), "Wi-Fi %u  未接続", (unsigned)s_wifi_ap_count);
        wifi_set_status(ui, status);
    } else {
        wifi_set_status(ui, "Wi-Fi 0  未接続");
    }
}

static void set_wifi_lock_visibility(esp_gsp_handle_t ui, bool visible)
{
    static const uint16_t locks[] = {
        GSP_KORVO_WIFI_BIND_WIFI_LOCK_0, GSP_KORVO_WIFI_BIND_WIFI_LOCK_1,
        GSP_KORVO_WIFI_BIND_WIFI_LOCK_2, GSP_KORVO_WIFI_BIND_WIFI_LOCK_3,
        GSP_KORVO_WIFI_BIND_WIFI_LOCK_4, GSP_KORVO_WIFI_BIND_WIFI_LOCK_5,
        GSP_KORVO_WIFI_BIND_WIFI_LOCK_6,
    };
    for (size_t i = 0; i < sizeof(locks) / sizeof(locks[0]); ++i) {
        (void)esp_gsp_set_visible(ui, locks[i], visible);
    }
}

static void set_weather_background(esp_gsp_handle_t ui, weather_theme_t theme)
{
    (void)esp_gsp_set_visible(ui, GSP_KORVO_WEATHER_BIND_WEATHER_BG,
                              theme != WEATHER_THEME_NIGHT);
    (void)esp_gsp_set_visible(ui, GSP_KORVO_WEATHER_BIND_WEATHER_BG_NIGHT,
                              theme == WEATHER_THEME_NIGHT);
    s_weather_theme = theme;
}

static void apply_weather_ui(esp_gsp_handle_t ui)
{
    if (s_live_scene != GSP_BUNDLE_SCENE_KORVO_WEATHER) {
        return;
    }
    weather_info_t info;
    weather_service_get_info(&info);

#if defined(GSP_KORVO_WEATHER_BIND_WEATHER_TITLE)
    (void)esp_gsp_set_text(ui, GSP_KORVO_WEATHER_BIND_WEATHER_TITLE, info.location);
#endif
#if defined(GSP_KORVO_WEATHER_BIND_WEATHER_BADGE)
    (void)esp_gsp_set_text(ui, GSP_KORVO_WEATHER_BIND_WEATHER_BADGE, info.badge);
#endif
#if defined(GSP_KORVO_WEATHER_BIND_WEATHER_TEMP)
    char temp_buf[16];
    snprintf(temp_buf, sizeof(temp_buf), "%d℃", info.temp_c);
    (void)esp_gsp_set_text(ui, GSP_KORVO_WEATHER_BIND_WEATHER_TEMP, temp_buf);
#endif
#if defined(GSP_KORVO_WEATHER_BIND_WEATHER_COND)
    const char *cond_name = "晴れ";
    if (info.condition == WEATHER_COND_CLOUDY) cond_name = "雲";
    else if (info.condition == WEATHER_COND_RAINY) cond_name = "雨";
    else if (info.condition == WEATHER_COND_SNOWY) cond_name = "雪";
    else if (info.condition == WEATHER_COND_THUNDER) cond_name = "雷";
    (void)esp_gsp_set_text(ui, GSP_KORVO_WEATHER_BIND_WEATHER_COND, cond_name);
#endif
#if defined(GSP_KORVO_WEATHER_BIND_WEATHER_TIME)
    char time_buf[32];
    snprintf(time_buf, sizeof(time_buf), "更新 %s", info.update_time);
    (void)esp_gsp_set_text(ui, GSP_KORVO_WEATHER_BIND_WEATHER_TIME, time_buf);
#endif
#if defined(GSP_KORVO_WEATHER_BIND_WEATHER_CLOCK_TEXT)
    time_t clock_now = time(NULL);
    struct tm clock_local = {0};
    char clock_buf[48] = "--:--";
    if (localtime_r(&clock_now, &clock_local) != NULL && clock_local.tm_year >= 120) {
        int32_t volume = 60;
        (void)esp_gsp_component_get_value(ui, GSP_KORVO_WEATHER_OBJ_KEY_VOLUME, &volume);
        if (volume < 0) volume = 0;
        if (volume > 100) volume = 100;
        s_weather_volume = volume;
        const char *wifi_mark = "○";
        if (s_wifi_conn_state == WIFI_CONN_STATE_CONNECTING) wifi_mark = "○…";
        else if (s_wifi_conn_state == WIFI_CONN_STATE_CONNECTED) wifi_mark = "●";
        else if (s_wifi_conn_state == WIFI_CONN_STATE_FAILED) wifi_mark = "!";
        snprintf(clock_buf, sizeof(clock_buf), "%02d:%02d   %s  ♩%ld",
                 clock_local.tm_hour, clock_local.tm_min, wifi_mark, (long)volume);
    }
    (void)esp_gsp_set_text(ui, GSP_KORVO_WEATHER_BIND_WEATHER_CLOCK_TEXT, clock_buf);
#endif
#if defined(GSP_KORVO_WEATHER_BIND_WEATHER_BG)
    time_t now = time(NULL);
    struct tm local = {0};
    int hour = 12;
    if (localtime_r(&now, &local) != NULL) hour = local.tm_hour;
    weather_theme_t theme = weather_theme_for_info(&info, hour);
    if (theme != s_weather_theme) {
        set_weather_background(ui, theme);
    }
#endif
#if defined(GSP_KORVO_WEATHER_BIND_WEATHER_MAIN)
    (void)esp_gsp_set_text(ui, GSP_KORVO_WEATHER_BIND_WEATHER_MAIN, info.main_text);
#endif
#if defined(GSP_KORVO_WEATHER_BIND_WEATHER_LORE)
    (void)esp_gsp_set_text(ui, GSP_KORVO_WEATHER_BIND_WEATHER_LORE, info.lore_text);
#endif

    weather_service_clear_dirty();
    s_weather_status_dirty = false;
}

static void remember_settings_return(uint16_t scene_id)
{
    app_event_t ev;
    switch (scene_id) {
    case GSP_BUNDLE_SCENE_KORVO_HOME:
        ev = APP_EVENT_HOME;
        break;
    case GSP_BUNDLE_SCENE_KORVO_SYNTH:
        ev = APP_EVENT_OPEN_SYNTH;
        break;
    case GSP_BUNDLE_SCENE_KORVO_WEATHER:
        ev = APP_EVENT_OPEN_WEATHER;
        break;
    case GSP_BUNDLE_SCENE_KORVO_VOICE:
        ev = APP_EVENT_OPEN_VOICE;
        break;
    case GSP_BUNDLE_SCENE_KORVO_OBJECT:
        ev = APP_EVENT_OPEN_OBJECT_RECOGNITION;
        break;
    case GSP_BUNDLE_SCENE_KORVO_LIGHTING:
        ev = APP_EVENT_OPEN_LIGHTING;
        break;
    case GSP_BUNDLE_SCENE_KORVO_CLOCK_TIMER:
        ev = APP_EVENT_OPEN_CLOCK_TIMER;
        break;
    case GSP_BUNDLE_SCENE_KORVO_CALCULATOR:
        ev = APP_EVENT_OPEN_CALCULATOR;
        break;
    case GSP_BUNDLE_SCENE_KORVO_FOOD:
        ev = APP_EVENT_OPEN_FOOD;
        break;
    default:
        return;
    }
    s_settings_return_scene = scene_id;
    s_settings_return_event = ev;
}

static void toggle_sync_timer_cb(esp_gsp_handle_t ui, void *ctx)
{
    (void)ctx;
    if (s_live_scene == GSP_BUNDLE_SCENE_KORVO_HOME) {
        static uint16_t last_home_page = UINT16_MAX;
        uint16_t page = 0;
        bool dragging = false;
        if (esp_gsp_page_flow_get_page(ui, GSP_KORVO_HOME_OBJ_KEY_HOME_PAGES, &page) == ESP_GSP_OK &&
            esp_gsp_page_flow_is_dragging(ui, GSP_KORVO_HOME_OBJ_KEY_HOME_PAGES, &dragging) == ESP_GSP_OK &&
            (page != last_home_page || dragging)) {
            ESP_LOGI(TAG, "Home PageFlow page=%u dragging=%d", (unsigned)page, (int)dragging);
            last_home_page = page;
        }
    }
    apply_toggle_from_widget(ui);
    if (s_wifi_results_dirty) {
        s_wifi_results_dirty = false;
        apply_wifi_scan_ui(ui);
    }
    if (s_live_scene == GSP_BUNDLE_SCENE_KORVO_WEATHER && weather_service_is_dirty()) {
        apply_weather_ui(ui);
    }
    if (s_live_scene == GSP_BUNDLE_SCENE_KORVO_WEATHER) {
        int32_t volume = s_weather_volume;
        if (esp_gsp_component_get_value(ui, GSP_KORVO_WEATHER_OBJ_KEY_VOLUME, &volume) == ESP_GSP_OK &&
            volume != s_weather_volume) {
            s_weather_status_dirty = true;
        }
        if (s_weather_status_dirty) {
            apply_weather_ui(ui);
        }
        static int last_minute = -1;
        time_t now = time(NULL);
        struct tm local = {0};
        if (localtime_r(&now, &local) != NULL && local.tm_min != last_minute) {
            last_minute = local.tm_min;
            apply_weather_ui(ui);
        }
    }
}

static void board_ui_open_scene(esp_gsp_handle_t ui, app_state_t *state,
                                app_event_t app_event, uint16_t scene_id)
{
    const bool is_details_scene = (scene_id == GSP_BUNDLE_SCENE_KORVO_WIFI ||
                                   scene_id == GSP_BUNDLE_SCENE_KORVO_BLUETOOTH);

    if (!s_reopen_drawer && !is_details_scene) {
        (void)esp_gsp_drawer_close(ui, GSP_OBJ_KEY_QUICK_SETTINGS_DRAWER, false);
    }
    if (scene_id != GSP_BUNDLE_SCENE_KORVO_WIFI) {
        ++s_wifi_scan_generation;
        (void)esp_gsp_keyboard_attach(ui, ESP_GSP_KEYBOARD_NONE, 0);
        (void)esp_gsp_drawer_close(ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_CONNECT_DRAWER, false);
    }
    synth_service_set_active(scene_id == GSP_BUNDLE_SCENE_KORVO_SYNTH);
    app_state_dispatch(state, app_event);
    esp_gsp_transition_t trans = (s_reopen_drawer || is_details_scene)
                                     ? ESP_GSP_NO_TRANSITION
                                     : ESP_GSP_FADE_THROUGH_BLACK;
    esp_gsp_err_t err = esp_gsp_goto_scene(ui, scene_id, trans);
    if (err != ESP_GSP_OK) {
        ESP_LOGE(TAG, "change scene %u failed: %d", (unsigned)scene_id, (int)err);
    }
}

static void board_ui_event(esp_gsp_handle_t ui, const esp_gsp_event_t *event,
                           void *ctx)
{
    app_state_t *state = (app_state_t *)ctx;
    if (event == NULL || state == NULL) {
        return;
    }
    ESP_LOGI(TAG, "EVT: scene=%d, action=%d, type=%d, arg=%u",
             event->scene_id, event->action_id, event->type, (unsigned)event->arg);

    if (event->type == ESP_GSP_EVENT_SCENE_CHANGED) {
        ESP_LOGI(TAG, "Scene changed settled to %d", event->scene_id);
        s_live_scene = event->scene_id;
        if (s_reopen_drawer) {
            (void)esp_gsp_drawer_open(ui, GSP_OBJ_KEY_QUICK_SETTINGS_DRAWER, false);
            s_reopen_drawer = false;
        } else {
            (void)esp_gsp_drawer_close(ui, GSP_OBJ_KEY_QUICK_SETTINGS_DRAWER, false);
        }
        if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_WIFI) {
            (void)esp_gsp_drawer_close(ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_CONNECT_DRAWER, false);
            (void)esp_gsp_keyboard_attach(ui, ESP_GSP_KEYBOARD_NONE, 0);
            apply_wifi_scan_ui(ui);
        } else if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_WEATHER) {
            weather_service_trigger_refresh();
            apply_weather_ui(ui);
        } else if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_BLUETOOTH) {
            if (s_bt_list == ESP_GSP_LIST_NONE) {
                s_bt_list = esp_gsp_list_bind_component(
                    ui, GSP_KORVO_BLUETOOTH_OBJ_KEY_BLUETOOTH_LIST, bt_list_bind_cb, NULL);
                ESP_LOGI(TAG, "Bound s_bt_list in SCENE_CHANGED: %u", (unsigned)s_bt_list);
            }
            if (s_bt_list != ESP_GSP_LIST_NONE) {
                (void)esp_gsp_list_set_total(ui, s_bt_list, MAX_BT_DEVS);
                (void)esp_gsp_list_refresh(ui, s_bt_list);
            }
        }
        if (event->scene_id != GSP_BUNDLE_SCENE_KORVO_WIFI &&
            event->scene_id != GSP_BUNDLE_SCENE_KORVO_BLUETOOTH) {
            update_drawer_quick_controls(ui);
        }
        synth_service_set_active(event->scene_id == GSP_BUNDLE_SCENE_KORVO_SYNTH);
        return;
    }

    if (event->type != ESP_GSP_EVENT_CALL) {
        return;
    }

    if (is_wifi_toggle_event(event)) {
        bool widget_checked = s_wifi_enabled;
        esp_gsp_err_t ck_err = esp_gsp_component_get_checked(
            ui, GSP_OBJ_KEY_WIFI_ENABLED, &widget_checked);
        if (ck_err == ESP_GSP_OK) {
            s_wifi_enabled = widget_checked;
        }
        if (!s_wifi_enabled) {
            (void)esp_wifi_disconnect();
            s_wifi_conn_state = WIFI_CONN_STATE_DISCONNECTED;
            s_connected_ip[0] = '\0';
            weather_service_set_offline();
            s_wifi_results_dirty = true;
            s_weather_status_dirty = true;
        }
        ESP_LOGI(TAG, "Wi-Fi toggled: %s (raw arg=%u checked=%d err=%d)",
                 s_wifi_enabled ? "ON" : "OFF", (unsigned)event->arg,
                 (int)widget_checked, (int)ck_err);
        sync_wifi_controls(ui);
        return;
    }

    if (is_bluetooth_toggle_event(event)) {
        bool widget_checked = s_bluetooth_enabled;
        esp_gsp_err_t ck_err = esp_gsp_component_get_checked(
            ui, GSP_OBJ_KEY_BLUETOOTH_ENABLED, &widget_checked);
        if (ck_err == ESP_GSP_OK) {
            s_bluetooth_enabled = widget_checked;
        }
        ESP_LOGI(TAG, "Bluetooth toggled: %s (raw arg=%u checked=%d err=%d)",
                 s_bluetooth_enabled ? "ON" : "OFF", (unsigned)event->arg,
                 (int)widget_checked, (int)ck_err);
        sync_bluetooth_controls(ui);
        return;
    }

    if (is_wifi_details_event(event)) {
        ESP_LOGI(TAG, "Opening Wi-Fi settings");
        remember_settings_return(event->scene_id);
        board_ui_open_scene(ui, state, APP_EVENT_OPEN_WIFI_SETTINGS,
                            GSP_BUNDLE_SCENE_KORVO_WIFI);
        wifi_scan_async(ui);
        return;
    }

    if (is_bluetooth_details_event(event)) {
        ESP_LOGI(TAG, "is_bluetooth_details_event matched! s_bluetooth_enabled=%d", s_bluetooth_enabled);
        remember_settings_return(event->scene_id);
        board_ui_open_scene(ui, state, APP_EVENT_OPEN_BLUETOOTH_SETTINGS,
                            GSP_BUNDLE_SCENE_KORVO_BLUETOOTH);
        return;
    }

    if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_WIFI) {
        if (event->action_id == GSP_KORVO_WIFI_ACT_ID_RESCAN) {
            wifi_scan_async(ui);
        } else if (event->action_id == GSP_KORVO_WIFI_ACT_ID_WIFI_DISCONNECT) {
            ESP_LOGI(TAG, "User requested Wi-Fi disconnect");
            (void)esp_wifi_disconnect();
            s_wifi_conn_state = WIFI_CONN_STATE_DISCONNECTED;
            s_connected_ip[0] = '\0';
            weather_service_set_offline();
            s_wifi_results_dirty = true;
            s_weather_status_dirty = true;
        } else if (event->action_id == GSP_KORVO_WIFI_ACT_ID_WIFI_PREV_PAGE) {
            if (s_wifi_page > 0) {
                s_wifi_page--;
                s_wifi_results_dirty = true;
            }
        } else if (event->action_id == GSP_KORVO_WIFI_ACT_ID_WIFI_NEXT_PAGE) {
            if ((s_wifi_page + 1) * 7 < s_wifi_ap_count) {
                s_wifi_page++;
                s_wifi_results_dirty = true;
            }
        } else if (event->action_id >= GSP_KORVO_WIFI_ACT_ID_WIFI_SELECT_0 &&
                   event->action_id <= GSP_KORVO_WIFI_ACT_ID_WIFI_SELECT_6) {
            int row_idx = event->action_id - GSP_KORVO_WIFI_ACT_ID_WIFI_SELECT_0;
            int ap_idx = (int)s_wifi_page * 7 + row_idx;
            if (ap_idx < s_wifi_ap_count && strlen((const char *)s_wifi_aps[ap_idx].ssid) > 0) {
                s_selected_ap = s_wifi_aps[ap_idx];
                char ssid_buf[64];
                snprintf(ssid_buf, sizeof(ssid_buf), "設定済み SSID: %.32s", (const char *)s_selected_ap.ssid);
                (void)esp_gsp_set_text(ui, GSP_KORVO_WIFI_BIND_WIFI_CONNECT_SSID, ssid_buf);
                if (s_selected_ap.authmode == WIFI_AUTH_OPEN) {
                    wifi_connect_to_ap((const char *)s_selected_ap.ssid, NULL);
                } else if (wifi_is_saved_ssid((const char *)s_selected_ap.ssid)) {
                    (void)esp_gsp_set_text(ui, GSP_KORVO_WIFI_BIND_WIFI_SAVED_SSID, ssid_buf);
                    set_wifi_lock_visibility(ui, false);
                    (void)esp_gsp_drawer_open(ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_SAVED_DRAWER, true);
                } else {
                    (void)esp_gsp_drawer_open(ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_CONNECT_DRAWER, true);
                    (void)esp_gsp_keyboard_attach(ui, ESP_GSP_KEYBOARD_NONE, 0);
                    (void)esp_gsp_set_text(ui, GSP_KORVO_WIFI_BIND_WIFI_PASS_INPUT, "");
                    (void)esp_gsp_keyboard_attach(ui, GSP_KORVO_WIFI_ACT_ID_WIFI_KEYBOARD_KEY,
                                                  GSP_KORVO_WIFI_BIND_WIFI_PASS_INPUT);
                }
            }
        } else if (event->action_id == GSP_KORVO_WIFI_ACT_ID_WIFI_CONNECT_CONFIRM ||
                   event->action_id == GSP_KORVO_WIFI_ACT_ID_WIFI_KEYBOARD_KEY) {
            const char *target_ssid = (const char *)s_selected_ap.ssid;
            if (strlen(target_ssid) == 0 && strlen(s_connecting_ssid) > 0) {
                target_ssid = s_connecting_ssid;
            }
            char pass_buf[64] = {0};
            (void)esp_gsp_keyboard_text(ui, pass_buf, sizeof(pass_buf));
            (void)esp_gsp_keyboard_attach(ui, ESP_GSP_KEYBOARD_NONE, 0);
            (void)esp_gsp_drawer_close(ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_CONNECT_DRAWER, true);
            if (strlen(target_ssid) > 0) {
                wifi_connect_to_ap(target_ssid, pass_buf);
            }
        } else if (event->action_id == GSP_KORVO_WIFI_ACT_ID_WIFI_CONNECT_CANCEL) {
            (void)esp_gsp_keyboard_attach(ui, ESP_GSP_KEYBOARD_NONE, 0);
            (void)esp_gsp_drawer_close(ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_CONNECT_DRAWER, true);
        } else if (event->action_id == GSP_KORVO_WIFI_ACT_ID_WIFI_SAVED_CONNECT) {
            (void)esp_gsp_drawer_close(ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_SAVED_DRAWER, true);
            wifi_reconnect_saved();
            s_wifi_results_dirty = true;
        } else if (event->action_id == GSP_KORVO_WIFI_ACT_ID_WIFI_SAVED_FORGET) {
            wifi_forget_saved();
            (void)esp_gsp_drawer_close(ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_SAVED_DRAWER, true);
            s_wifi_results_dirty = true;
        } else if (event->action_id == GSP_KORVO_WIFI_ACT_ID_WIFI_SAVED_CANCEL) {
            (void)esp_gsp_drawer_close(ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_SAVED_DRAWER, true);
            s_wifi_results_dirty = true;
        } else if (event->action_id == GSP_KORVO_WIFI_ACT_ID_HOME) {
            (void)esp_gsp_keyboard_attach(ui, ESP_GSP_KEYBOARD_NONE, 0);
            (void)esp_gsp_drawer_close(ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_CONNECT_DRAWER, false);
            s_reopen_drawer = true;
            board_ui_open_scene(ui, state, s_settings_return_event,
                                s_settings_return_scene);
        }
        return;
    }

    if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_WEATHER &&
        event->action_id == GSP_KORVO_WEATHER_ACT_ID_WEATHER_REFRESH) {
        weather_service_trigger_refresh();
        return;
    }

    if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_BLUETOOTH) {
        if (event->action_id == GSP_KORVO_BLUETOOTH_ACT_ID_REFRESH) {
            if (s_bt_list != ESP_GSP_LIST_NONE) {
                (void)esp_gsp_list_refresh(ui, s_bt_list);
            }
        } else if (event->action_id == GSP_KORVO_BLUETOOTH_ACT_ID_HOME) {
            s_reopen_drawer = true;
            board_ui_open_scene(ui, state, s_settings_return_event,
                                s_settings_return_scene);
        }
        return;
    }

    if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_HOME) {
        switch (event->action_id) {
        case GSP_KORVO_HOME_ACTION_OPEN_SYNTH:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_SYNTH,
                                GSP_BUNDLE_SCENE_KORVO_SYNTH);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_WEATHER:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_WEATHER,
                                GSP_BUNDLE_SCENE_KORVO_WEATHER);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_VOICE:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_VOICE,
                                GSP_BUNDLE_SCENE_KORVO_VOICE);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_OBJECT_RECOGNITION:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_OBJECT_RECOGNITION,
                                GSP_BUNDLE_SCENE_KORVO_OBJECT);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_LIGHTING:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_LIGHTING,
                                GSP_BUNDLE_SCENE_KORVO_LIGHTING);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_CLOCK_TIMER:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_CLOCK_TIMER,
                                GSP_BUNDLE_SCENE_KORVO_CLOCK_TIMER);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_CALCULATOR:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_CALCULATOR,
                                GSP_BUNDLE_SCENE_KORVO_CALCULATOR);
            break;
        case GSP_KORVO_HOME_ACTION_OPEN_FOOD:
            board_ui_open_scene(ui, state, APP_EVENT_OPEN_FOOD,
                                GSP_BUNDLE_SCENE_KORVO_FOOD);
            break;
        default:
            break;
        }
        return;
    }

    if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_SYNTH) {
        if (event->action_id == GSP_KORVO_SYNTH_ACT_ID_HOME) {
            synth_service_set_active(false);
            board_ui_open_scene(ui, state, APP_EVENT_HOME, GSP_BUNDLE_SCENE_KORVO_HOME);
            return;
        }

        switch (event->action_id) {
        /* Waveform selection */
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_WAVE_SIN:
            synth_service_set_waveform(SYNTH_WAVE_SIN);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_WAVE_SQR:
            synth_service_set_waveform(SYNTH_WAVE_SQR);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_WAVE_SAW:
            synth_service_set_waveform(SYNTH_WAVE_SAW);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_WAVE_DRUM:
            synth_service_set_waveform(SYNTH_WAVE_DRUM);
            break;

        /* Mode selection */
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_MODE_KEY:
            synth_service_set_mode(SYNTH_MODE_KEY);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_MODE_BT:
            synth_service_set_mode(SYNTH_MODE_BT);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_MODE_WEB:
            synth_service_set_mode(SYNTH_MODE_WEB);
            break;

        /* Piano keys - equal temperament notes */
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_F3:
            synth_service_note_on(174.61f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_FS3:
            synth_service_note_on(185.00f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_G3:
            synth_service_note_on(196.00f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_GS3:
            synth_service_note_on(207.65f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_A3:
            synth_service_note_on(220.00f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_AS3:
            synth_service_note_on(233.08f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_B3:
            synth_service_note_on(246.94f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_C4:
            synth_service_note_on(261.63f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_CS4:
            synth_service_note_on(277.18f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_D4:
            synth_service_note_on(293.66f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_DS4:
            synth_service_note_on(311.13f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_E4:
            synth_service_note_on(329.63f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_F4:
            synth_service_note_on(349.23f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_FS4:
            synth_service_note_on(369.99f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_G4:
            synth_service_note_on(392.00f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_GS4:
            synth_service_note_on(415.30f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_A4:
            synth_service_note_on(440.00f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_AS4:
            synth_service_note_on(466.16f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_B4:
            synth_service_note_on(493.88f, 1.0f);
            break;
        case GSP_KORVO_SYNTH_ACT_ID_SYNTH_C5:
            synth_service_note_on(523.25f, 1.0f);
            break;
        default:
            ESP_LOGI(TAG, "Synth action: %d", (int)event->action_id);
            break;
        }
        return;
    }

    if (event->action_id == GSP_KORVO_WEATHER_ACTION_HOME ||
        event->action_id == GSP_KORVO_VOICE_ACTION_HOME ||
        event->action_id == GSP_KORVO_OBJECT_ACTION_HOME ||
        event->action_id == GSP_KORVO_LIGHTING_ACTION_HOME ||
        event->action_id == GSP_KORVO_CLOCK_TIMER_ACTION_HOME ||
        event->action_id == GSP_KORVO_CALCULATOR_ACTION_HOME ||
        event->action_id == GSP_KORVO_FOOD_ACTION_HOME) {
        board_ui_open_scene(ui, state, APP_EVENT_HOME, GSP_BUNDLE_SCENE_KORVO_HOME);
    }
}

esp_err_t board_ui_start(app_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    bsp_display_config_t display_config = {0};
    ESP_RETURN_ON_ERROR(bsp_display_new(&display_config, &panel, &io), TAG,
                        "create Korvo-1 RGB panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG,
                        "turn on Korvo-1 RGB panel");

    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t touch_error = bsp_touch_new(NULL, &touch);
    if (touch_error != ESP_OK) {
        ESP_LOGW(TAG, "GT1151 touch unavailable: %s", esp_err_to_name(touch_error));
        touch = NULL;
    }

    esp_display_present_target_config_t display = {
        .hw = {
            .panel = panel,
            .panel_type = ESP_DISPLAY_PRESENT_PANEL_RGB,
            .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
        },
        .fb = {
            .mode = ESP_DISPLAY_PRESENT_MODE_AUTO,
        },
    };

    esp_gsp_config_t app_config = gsp_bundle_config();
    esp_gsp_esp_lcd_config_t lcd_config = ESP_GSP_ESP_LCD_CONFIG_INIT();
    lcd_config.display = display;
    lcd_config.touch = touch;
    lcd_config.perf_log = true;

    esp_gsp_handle_t ui = NULL;
    ESP_RETURN_ON_ERROR(esp_gsp_esp_lcd_start(&app_config, &lcd_config, &ui), TAG,
                        "start ESP-GSP bundle");
    ESP_RETURN_ON_ERROR(esp_gsp_on_event(ui, board_ui_event, state), TAG,
                        "register GSP UI events");

    (void)esp_gsp_drawer_close(ui, GSP_OBJ_KEY_QUICK_SETTINGS_DRAWER, false);
    update_drawer_quick_controls(ui);
    if (esp_gsp_timer_create(ui, 50, toggle_sync_timer_cb, NULL) == NULL) {
        ESP_LOGE(TAG, "toggle sync timer failed");
    }

    ESP_LOGI(TAG, "Korvo-1 GSP UI started: %dx%d, touch=%s",
             BSP_LCD_H_RES, BSP_LCD_V_RES, touch ? "ready" : "unavailable");
    return ESP_OK;
}
