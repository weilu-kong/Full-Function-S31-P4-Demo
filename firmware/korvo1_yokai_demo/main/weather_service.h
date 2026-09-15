/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#ifdef HOST_TEST
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#else
#include "esp_err.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEATHER_COND_SUNNY = 0, /* 晴れ */
    WEATHER_COND_CLOUDY,    /* 雲 */
    WEATHER_COND_RAINY,     /* 雨 */
    WEATHER_COND_SNOWY,     /* 雪 */
    WEATHER_COND_THUNDER,   /* 雷 */
    WEATHER_COND_UNKNOWN,
} weather_cond_t;

typedef enum {
    WEATHER_BACKGROUND_SUNNY = 0,
    WEATHER_BACKGROUND_RAIN,
} weather_background_t;

typedef enum {
    WEATHER_THEME_DAY = 0,
    WEATHER_THEME_NIGHT,
} weather_theme_t;

typedef struct {
    bool is_live;             /* true if fetched from live network, false if DEMO */
    bool is_day;              /* true if daytime, false if nighttime */
    weather_cond_t condition; /* Sunny, Cloudy, Rainy, Snowy, Thunder */
    int temp_c;               /* e.g. 26 */
    char location[48];        /* "天気　妖怪村（東京）" */
    char update_time[16];     /* "14:30" */
    char main_text[64];       /* "晴れ　26℃　更新　14:30" */
    char lore_text[96];       /* "村の上には、青い空と雲の行列。" */
    char badge[16];           /* "DEMO" or "LIVE" */
} weather_info_t;

/**
 * @brief Initialize weather background service (SNTP clock sync and Open-Meteo polling).
 */
esp_err_t weather_service_init(void);

/**
 * @brief Trigger an immediate weather data refresh.
 */
void weather_service_trigger_refresh(void);

/**
 * @brief Thread-safe getter for current weather snapshot.
 */
void weather_service_get_info(weather_info_t *out_info);

/**
 * @brief Check if weather data was updated and needs UI repaint.
 */
bool weather_service_is_dirty(void);

/**
 * @brief Clear the UI dirty flag.
 */
void weather_service_clear_dirty(void);

/**
 * @brief Reset weather service to offline DEMO state.
 */
void weather_service_set_offline(void);

/* Pure logic helpers exposed for unit testability */
weather_cond_t weather_map_wmo_code(int wmo_code);
weather_background_t weather_background_for_condition(weather_cond_t condition);
weather_theme_t weather_theme_for_info(const weather_info_t *info, int local_hour);
void weather_format_info(weather_info_t *info, bool is_live, int temp_c, int wmo_code, bool is_day, const char *time_str);
bool weather_parse_open_meteo_json(const char *json_str, int *out_temp_c, int *out_wmo_code, bool *out_is_day);

#ifdef __cplusplus
}
#endif
