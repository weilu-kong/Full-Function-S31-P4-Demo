#include "board_ui.h"

#include <stdio.h>
#include <inttypes.h>

#include "esp_check.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/display.h"
#include "bsp/esp32_s31_korvo_1.h"
#include "bsp/touch.h"
#include "synth_service.h"

#define GSP_BUNDLE_ENABLE_RAW_IDS 1
#define GSP_BUNDLE_ENABLE_LEGACY_NAMES 1
#include "bundle_gsp.h"

static const char *TAG = "board_ui";
static bool s_wifi_ready;
static bool s_wifi_scan_running;
static volatile uint32_t s_wifi_scan_generation;
static app_state_t *s_wifi_scan_state;
static esp_gsp_handle_t s_wifi_scan_ui;
static esp_gsp_list_t s_wifi_list = ESP_GSP_LIST_NONE;
static esp_gsp_list_t s_bt_list = ESP_GSP_LIST_NONE;

#define MAX_WIFI_APS 10
static wifi_ap_record_t s_wifi_aps[MAX_WIFI_APS];
static uint16_t s_wifi_ap_count;

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

static gsp_err_t wifi_list_bind_cb(esp_gsp_handle_t gsp, esp_gsp_row_t row,
                                   uint32_t index, void *ctx)
{
    (void)ctx;
    if (index < s_wifi_ap_count) {
        char buf[72];
        snprintf(buf, sizeof(buf), "   %s    %d dBm",
                 (const char *)s_wifi_aps[index].ssid, s_wifi_aps[index].rssi);
        (void)esp_gsp_row_text(gsp, row, buf);
    } else {
        (void)esp_gsp_row_text(gsp, row, "   --");
    }
    return GSP_OK;
}

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

static esp_err_t wifi_start_once(void)
{
    if (s_wifi_ready) {
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
        (err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (err = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK ||
        (err = esp_wifi_start()) != ESP_OK) {
        return err;
    }
    s_wifi_ready = true;
    ESP_LOGI(TAG, "Wi-Fi STA started successfully");
    return ESP_OK;
}

static void wifi_scan_task(void *arg)
{
    const uint32_t generation = (uint32_t)(uintptr_t)arg;
    esp_gsp_handle_t ui = s_wifi_scan_ui;
    app_state_t *state = s_wifi_scan_state;

    vTaskDelay(pdMS_TO_TICKS(200));
    if (generation != s_wifi_scan_generation || state->screen != APP_SCREEN_WIFI_SETTINGS) {
        s_wifi_scan_running = false;
        vTaskDelete(NULL);
        return;
    }

    if (s_wifi_ap_count == 0) {
        wifi_set_status(ui, "Wi-Fi 確認中…");
    }

    esp_err_t err = wifi_start_once();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_start_once failed: %s", esp_err_to_name(err));
        if (generation == s_wifi_scan_generation && state->screen == APP_SCREEN_WIFI_SETTINGS) {
            wifi_set_status(ui, "Wi-Fi ERROR");
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
        if (generation != s_wifi_scan_generation || state->screen != APP_SCREEN_WIFI_SETTINGS) {
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
        ESP_LOGW(TAG, "Wi-Fi scan retry (err=%s, ap_count=%u)", esp_err_to_name(err), (unsigned)ap_count);
        vTaskDelay(pdMS_TO_TICKS(200));
        retries--;
    }

    if (generation != s_wifi_scan_generation || state->screen != APP_SCREEN_WIFI_SETTINGS) {
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

        char status[48];
        snprintf(status, sizeof(status), "Wi-Fi %u", (unsigned)fetch_count);
        wifi_set_status(ui, status);

        if (s_wifi_list == ESP_GSP_LIST_NONE) {
            s_wifi_list = esp_gsp_list_bind_component(
                ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_LIST, wifi_list_bind_cb, NULL);
            ESP_LOGI(TAG, "Bound s_wifi_list in scan task: %u", (unsigned)s_wifi_list);
        }
        if (s_wifi_list != ESP_GSP_LIST_NONE) {
            (void)esp_gsp_list_set_total(ui, s_wifi_list, MAX_WIFI_APS);
            (void)esp_gsp_list_refresh(ui, s_wifi_list);
        }
    } else {
        ESP_LOGE(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
        wifi_set_status(ui, "Wi-Fi ERROR");
    }

    s_wifi_scan_running = false;
    vTaskDelete(NULL);
}

static void wifi_scan_async(esp_gsp_handle_t ui, app_state_t *state)
{
    if (s_wifi_scan_running) {
        return;
    }
    s_wifi_scan_running = true;
    s_wifi_scan_ui = ui;
    s_wifi_scan_state = state;
    uint32_t generation = ++s_wifi_scan_generation;
    if (xTaskCreate(wifi_scan_task, "wifi_scan", 6144,
                    (void *)(uintptr_t)generation, 5, NULL) != pdPASS) {
        s_wifi_scan_running = false;
        wifi_set_status(ui, "Wi-Fi ERROR");
    }
}

static bool s_wifi_enabled = false;
static bool s_bluetooth_enabled = false;

static bool is_wifi_details_event(const esp_gsp_event_t *event)
{
    if (!s_wifi_enabled) {
        return false;  /* OFF: card tap is a no-op */
    }
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

static bool is_bluetooth_details_event(const esp_gsp_event_t *event)
{
    if (!s_bluetooth_enabled) {
        return false;  /* OFF: card tap is a no-op */
    }
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

/* GSP pipeline is rgb565: bind-based set_color takes native 16-bit value.
 * wifi_card / bluetooth_card use bind_target:"color", so the bind slot
 * overwrites any direct component_set_color every frame — we MUST use
 * esp_gsp_set_color(ui, bind_index, color) to drive the card appearance. */
#define GSP_CARD_COLOR_ON  0x231Du  /* #2563EB in RGB565 */
#define GSP_CARD_COLOR_OFF 0x10E5u  /* #141C2B in RGB565 */

/* Bind indices are identical across all scenes (verified in bundle_gsp.h).
 * The bind variable is named "wifi_card_bg" / "bluetooth_card_bg" in JSON. */
#define GSP_BIND_WIFI_CARD_IDX  GSP_KORVO_HOME_BIND_WIFI_CARD_BG       /* 8 */
#define GSP_BIND_BT_CARD_IDX    GSP_KORVO_HOME_BIND_BLUETOOTH_CARD_BG  /* 7 */

static void sync_wifi_controls(esp_gsp_handle_t ui)
{
    esp_gsp_err_t err = esp_gsp_set_color(ui, GSP_BIND_WIFI_CARD_IDX,
                                          s_wifi_enabled ? GSP_CARD_COLOR_ON : GSP_CARD_COLOR_OFF);
    if (err != ESP_GSP_OK) {
        ESP_LOGE(TAG, "set wifi_card bind color failed: %d", (int)err);
    }
}

static void sync_bluetooth_controls(esp_gsp_handle_t ui)
{
    esp_gsp_err_t err = esp_gsp_set_color(ui, GSP_BIND_BT_CARD_IDX,
                                          s_bluetooth_enabled ? GSP_CARD_COLOR_ON : GSP_CARD_COLOR_OFF);
    if (err != ESP_GSP_OK) {
        ESP_LOGE(TAG, "set bt_card bind color failed: %d", (int)err);
    }
}

static void update_drawer_quick_controls(esp_gsp_handle_t ui)
{
    /* Do NOT call set_checked here — that may fire a spurious TOGGLE callback
     * that immediately re-flips s_wifi/bluetooth_enabled. Just sync colors. */
    sync_wifi_controls(ui);
    sync_bluetooth_controls(ui);
}

static void board_ui_open_scene(esp_gsp_handle_t ui, app_state_t *state,
                                app_event_t app_event, uint16_t scene_id)
{
    (void)esp_gsp_drawer_close(ui, GSP_OBJ_KEY_QUICK_SETTINGS_DRAWER, false);
    if (scene_id != GSP_BUNDLE_SCENE_KORVO_WIFI) {
        ++s_wifi_scan_generation;
    }
    synth_service_set_active(scene_id == GSP_BUNDLE_SCENE_KORVO_SYNTH);
    app_state_dispatch(state, app_event);
    esp_gsp_err_t err = esp_gsp_goto_scene(ui, scene_id,
                                           ESP_GSP_FADE_THROUGH_BLACK);
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
        (void)esp_gsp_drawer_close(ui, GSP_OBJ_KEY_QUICK_SETTINGS_DRAWER, false);
        if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_WIFI) {
            if (s_wifi_list == ESP_GSP_LIST_NONE) {
                s_wifi_list = esp_gsp_list_bind_component(
                    ui, GSP_KORVO_WIFI_OBJ_KEY_WIFI_LIST, wifi_list_bind_cb, NULL);
                ESP_LOGI(TAG, "Bound s_wifi_list in SCENE_CHANGED: %u", (unsigned)s_wifi_list);
            }
            if (s_wifi_list != ESP_GSP_LIST_NONE) {
                (void)esp_gsp_list_set_total(ui, s_wifi_list, MAX_WIFI_APS);
                (void)esp_gsp_list_refresh(ui, s_wifi_list);
            }
            if (s_wifi_ap_count > 0) {
                char status[48];
                snprintf(status, sizeof(status), "Wi-Fi %u", (unsigned)s_wifi_ap_count);
                wifi_set_status(ui, status);
            }
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
        } else {
            update_drawer_quick_controls(ui);
        }
        synth_service_set_active(event->scene_id == GSP_BUNDLE_SCENE_KORVO_SYNTH);
        return;
    }

    if (event->type != ESP_GSP_EVENT_CALL) {
        return;
    }

    if (is_wifi_toggle_event(event)) {
        /* GSP toggle manages its own visual animation. event->arg carries
         * animation progress (always ~100 when complete), NOT the new toggle
         * state. Use a simple boolean flip — one event fires per user tap. */
        s_wifi_enabled = !s_wifi_enabled;
        ESP_LOGI(TAG, "Wi-Fi toggled: %s (raw arg=%u)",
                 s_wifi_enabled ? "ON" : "OFF", (unsigned)event->arg);
        sync_wifi_controls(ui);
        return;
    }

    if (is_bluetooth_toggle_event(event)) {
        s_bluetooth_enabled = !s_bluetooth_enabled;
        ESP_LOGI(TAG, "Bluetooth toggled: %s (raw arg=%u)",
                 s_bluetooth_enabled ? "ON" : "OFF", (unsigned)event->arg);
        sync_bluetooth_controls(ui);
        return;
    }

    if (is_wifi_details_event(event)) {
        ESP_LOGI(TAG, "Opening Wi-Fi settings");
        board_ui_open_scene(ui, state, APP_EVENT_OPEN_WIFI_SETTINGS,
                            GSP_BUNDLE_SCENE_KORVO_WIFI);
        wifi_scan_async(ui, state);
        return;
    }

    if (is_bluetooth_details_event(event)) {
        ESP_LOGI(TAG, "is_bluetooth_details_event matched! s_bluetooth_enabled=%d", s_bluetooth_enabled);
        board_ui_open_scene(ui, state, APP_EVENT_OPEN_BLUETOOTH_SETTINGS,
                            GSP_BUNDLE_SCENE_KORVO_BLUETOOTH);
        return;
    }

    if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_WIFI) {
        if (event->action_id == GSP_KORVO_WIFI_ACT_ID_RESCAN) {
            wifi_scan_async(ui, state);
        } else if (event->action_id == GSP_KORVO_WIFI_ACT_ID_HOME) {
            board_ui_open_scene(ui, state, APP_EVENT_HOME,
                                GSP_BUNDLE_SCENE_KORVO_HOME);
        }
        return;
    }

    if (event->scene_id == GSP_BUNDLE_SCENE_KORVO_BLUETOOTH) {
        if (event->action_id == GSP_KORVO_BLUETOOTH_ACT_ID_REFRESH) {
            if (s_bt_list != ESP_GSP_LIST_NONE) {
                (void)esp_gsp_list_refresh(ui, s_bt_list);
            }
        } else if (event->action_id == GSP_KORVO_BLUETOOTH_ACT_ID_HOME) {
            board_ui_open_scene(ui, state, APP_EVENT_HOME,
                                GSP_BUNDLE_SCENE_KORVO_HOME);
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

    ESP_LOGI(TAG, "Korvo-1 GSP UI started: %dx%d, touch=%s",
             BSP_LCD_H_RES, BSP_LCD_V_RES, touch ? "ready" : "unavailable");
    return ESP_OK;
}
