#include "board_ui.h"

#include "esp_check.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#include "bsp/display.h"
#include "bsp/esp32_s31_korvo_1.h"
#include "bsp/touch.h"

#include "bundle_gsp.h"

static const char *TAG = "board_ui";

static void board_ui_open_scene(esp_gsp_handle_t ui, app_state_t *state,
                                app_event_t app_event, uint16_t scene_id)
{
    app_state_dispatch(state, app_event);
    esp_gsp_err_t err = esp_gsp_goto_scene(ui, scene_id,
                                           ESP_GSP_FADE_THROUGH_BLACK);
    if (err != ESP_GSP_OK) {
        ESP_LOGE(TAG, "change scene %u failed: %d", (unsigned)scene_id, (int)err);
    }
}

static void board_ui_event(esp_gsp_handle_t ui, const esp_gsp_event_t *event,
                           void *user_ctx)
{
    app_state_t *state = user_ctx;
    if (event == NULL || state == NULL || event->type != ESP_GSP_EVENT_CALL) {
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
        default:
            break;
        }
        return;
    }

    if (event->action_id == GSP_KORVO_SYNTH_ACTION_HOME ||
        event->action_id == GSP_KORVO_WEATHER_ACTION_HOME ||
        event->action_id == GSP_KORVO_VOICE_ACTION_HOME ||
        event->action_id == GSP_KORVO_OBJECT_ACTION_HOME) {
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
    ESP_LOGI(TAG, "Korvo-1 GSP UI started: %dx%d, touch=%s",
             BSP_LCD_H_RES, BSP_LCD_V_RES, touch ? "ready" : "unavailable");
    return ESP_OK;
}
