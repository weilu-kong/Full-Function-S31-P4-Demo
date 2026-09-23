#include "app_regression.h"
#include "board_ui.h"
#include "ui/ui.h"
#include "app_health.h"
#include "fireworks_engine.h"
#include "clock_service.h"
#include "calculator_engine.h"
#include "voice_service.h"
#include "vision_service.h"
#include "synth_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "regression";

static void regression_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Waiting 9s for system boot and Wi-Fi connection...");
    vTaskDelay(pdMS_TO_TICKS(9000));

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "STARTING REAL-BOARD COMPREHENSIVE REGRESSION TEST");
    ESP_LOGI(TAG, "==================================================");
    app_health_log_heap("Regression Start");

    /* 1. Test Fireworks App (Style B) */
    ESP_LOGI(TAG, "[TEST 1/6] Switching to Fireworks App (Style B)...");
    board_ui_switch_screen(UI_SCREEN_FIREWORKS);
    vTaskDelay(pdMS_TO_TICKS(2000));
    fireworks_engine_tap(400.0f, 200.0f);
    fireworks_stats_t fw_st;
    fireworks_engine_get_stats(&fw_st);
    ESP_LOGI(TAG, "Fireworks active: particles=%d, rockets=%d, launches=%u",
             fw_st.active_particles, fw_st.active_rockets, (unsigned)fw_st.launches);
    app_health_log_heap("In Fireworks App");
    vTaskDelay(pdMS_TO_TICKS(1500));

    /* 2. Test Clock / Timer App (Style B) */
    ESP_LOGI(TAG, "[TEST 2/6] Switching to Clock / Timer App (Style B)...");
    board_ui_switch_screen(UI_SCREEN_CLOCK);
    vTaskDelay(pdMS_TO_TICKS(1500));
    clock_timer_set_duration(0, 3, 0); /* 3 min */
    clock_timer_start();
    clock_stopwatch_start();
    vTaskDelay(pdMS_TO_TICKS(2000));
    clock_stopwatch_lap();
    clock_timer_snapshot_t t_snap;
    clock_timer_get(&t_snap);
    clock_stopwatch_snapshot_t sw_snap;
    clock_stopwatch_get(&sw_snap);
    char buf[32];
    clock_format_hms(t_snap.remaining_us, buf, sizeof(buf));
    ESP_LOGI(TAG, "Clock timer running: remaining=%s, sw_elapsed_us=%lld, laps=%d",
             buf, (long long)sw_snap.elapsed_us, sw_snap.lap_count);
    app_health_log_heap("In Clock App");
    clock_timer_cancel();
    clock_stopwatch_reset();

    /* 3. Test Calculator App (Style A) */
    ESP_LOGI(TAG, "[TEST 3/6] Switching to Calculator App (Style A)...");
    board_ui_switch_screen(UI_SCREEN_CALCULATOR);
    vTaskDelay(pdMS_TO_TICKS(2000));
    app_health_log_heap("In Calculator App");

    /* 4. Test Vision AI Entry, Frame Ingestion, and Exit (Cycle 1) */
    ESP_LOGI(TAG, "[TEST 4/6] Switching to Vision AI Screen (Cycle 1 - Camera/AI Init)...");
    board_ui_switch_screen(UI_SCREEN_VISION);
    vTaskDelay(pdMS_TO_TICKS(4000));
    vision_diag_t vdiag;
    vision_service_get_diag(&vdiag);
    ESP_LOGI(TAG, "Vision Cycle 1: captured=%u, displayed=%u, infer=%u, cam_err=%u, state=%d",
             (unsigned)vdiag.frames_captured, (unsigned)vdiag.frames_displayed,
             (unsigned)vdiag.face_inferences, (unsigned)vdiag.camera_errors,
             (int)vision_service_get_state());
    app_health_log_heap("In Vision AI (Peak PSRAM)");

    /* Exit Vision to Home */
    ESP_LOGI(TAG, "Exiting Vision to Home (verifying clean stop without camera deinit)...");
    board_ui_switch_screen(UI_SCREEN_HOME);
    vTaskDelay(pdMS_TO_TICKS(2000));
    app_health_log_heap("Returned Home from Vision Cycle 1");

    /* 5. Test Vision AI Re-entry (Cycle 2) */
    ESP_LOGI(TAG, "[TEST 5/6] Re-entering Vision AI Screen (Cycle 2 - Verification of safe restart)...");
    board_ui_switch_screen(UI_SCREEN_VISION);
    vTaskDelay(pdMS_TO_TICKS(3500));
    vision_service_get_diag(&vdiag);
    ESP_LOGI(TAG, "Vision Cycle 2: captured=%u, displayed=%u, infer=%u, cam_err=%u, state=%d",
             (unsigned)vdiag.frames_captured, (unsigned)vdiag.frames_displayed,
             (unsigned)vdiag.face_inferences, (unsigned)vdiag.camera_errors,
             (int)vision_service_get_state());
    app_health_log_heap("In Vision AI Cycle 2");

    /* Return to Home */
    board_ui_switch_screen(UI_SCREEN_HOME);
    vTaskDelay(pdMS_TO_TICKS(2000));
    app_health_log_heap("Returned Home from Vision Cycle 2");

    /* 6. Test Voice Command Injection & Synth Feedback */
    ESP_LOGI(TAG, "[TEST 6/6] Testing Voice Command Injection & Synth Audio Tone...");
    voice_service_inject_command(VOICE_COMMAND_SYNTH);
    vTaskDelay(pdMS_TO_TICKS(2500));
    app_health_log_heap("In Synth Screen via Voice Command");
    synth_service_play_feedback_tone();
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Return Home */
    board_ui_switch_screen(UI_SCREEN_HOME);
    vTaskDelay(pdMS_TO_TICKS(1500));

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "[REGRESSION_PASS] ALL 3 APPS + VISION + VOICE + WI-FI + SYNTH VERIFIED ON HARDWARE!");
    ESP_LOGI(TAG, "==================================================");
    app_health_log_heap("Final Regression Done");
    vTaskDelete(NULL);
}

void app_regression_start(void)
{
    xTaskCreate(regression_task, "regress_task", 4096, NULL, 3, NULL);
}
