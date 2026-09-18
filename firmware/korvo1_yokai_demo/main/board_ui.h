#pragma once

#include "esp_err.h"
#include "esp_wifi.h"
#include "app_state.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_WIFI_APS 14

typedef enum {
    BOARD_WIFI_DISCONNECTED = 0,
    BOARD_WIFI_CONNECTING,
    BOARD_WIFI_CONNECTED,
    BOARD_WIFI_FAILED,
} board_wifi_state_t;

typedef struct {
    board_wifi_state_t state;
    char connected_ssid[33];
    char ip_str[16];
    uint8_t last_disconnect_reason;
    bool scan_running;
    bool scan_failed;
    uint16_t ap_count;
    int8_t connected_rssi;
    wifi_ap_record_t aps[MAX_WIFI_APS];
} board_wifi_info_t;

/** Initialize the Korvo-1 RGB panel, GT1151 touch controller, and Yokai UI. */
esp_err_t board_ui_start(app_state_t *state);

#include "ui/ui.h"

/** Switch Yokai UI screen under LVGL lock. */
esp_err_t board_ui_switch_screen(ui_screen_t target);

/** Ensure Wi-Fi STA subsystem is initialized and ready for scan or connection. */
esp_err_t board_ui_wifi_ensure_started(void);

/** Trigger an asynchronous AP scan. */
esp_err_t board_ui_wifi_scan_async(void);

/** Get current snapshot of Wi-Fi state and AP list. */
void board_ui_wifi_get_info(board_wifi_info_t *out_info);

/** Check if Wi-Fi state or scan results have changed since last check. */
bool board_ui_wifi_is_dirty(void);

/** Clear Wi-Fi dirty flag. */
void board_ui_wifi_clear_dirty(void);

/** Connect to a specific AP with given SSID and password. */
void board_ui_wifi_connect(const char *ssid, const char *password);

/** Reconnect to saved AP in NVS flash. */
void board_ui_wifi_reconnect_saved(void);

/** Forget saved AP configuration. */
void board_ui_wifi_forget_saved(void);

/** Disconnect Wi-Fi. */
void board_ui_wifi_disconnect(void);

/** Check if an SSID matches the saved configuration. */
bool board_ui_wifi_is_saved(const char *ssid);

/** Enable or disable Wi-Fi radio. */
void board_ui_wifi_set_enabled(bool enabled);

/** Check if Wi-Fi radio is enabled. */
bool board_ui_wifi_is_enabled(void);

#ifdef __cplusplus
}
#endif
