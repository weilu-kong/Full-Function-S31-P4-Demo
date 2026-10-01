/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "weather_service.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"

#ifndef HOST_TEST
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "board_ui.h"

static const char *TAG = "weather_svc";
#endif

weather_cond_t weather_map_wmo_code(int wmo)
{
    if (wmo == 0 || wmo == 1) {
        return WEATHER_COND_SUNNY;
    }
    if (wmo == 2 || wmo == 3 || wmo == 45 || wmo == 48) {
        return WEATHER_COND_CLOUDY;
    }
    if ((wmo >= 51 && wmo <= 67) || (wmo >= 80 && wmo <= 82)) {
        return WEATHER_COND_RAINY;
    }
    if ((wmo >= 71 && wmo <= 77) || (wmo >= 85 && wmo <= 86)) {
        return WEATHER_COND_SNOWY;
    }
    if (wmo >= 95) {
        return WEATHER_COND_THUNDER;
    }
    return WEATHER_COND_SUNNY;
}

weather_background_t weather_background_for_condition(weather_cond_t condition)
{
    return (condition == WEATHER_COND_RAINY || condition == WEATHER_COND_SNOWY ||
            condition == WEATHER_COND_THUNDER) ? WEATHER_BACKGROUND_RAIN : WEATHER_BACKGROUND_SUNNY;
}

weather_theme_t weather_theme_for_info(const weather_info_t *info, int local_hour)
{
    if (info && info->is_live) {
        return info->is_day ? WEATHER_THEME_DAY : WEATHER_THEME_NIGHT;
    }
    return (local_hour >= 6 && local_hour < 18) ? WEATHER_THEME_DAY : WEATHER_THEME_NIGHT;
}

bool weather_parse_open_meteo_json(const char *json_str, int *out_temp_c, int *out_wmo_code, bool *out_is_day)
{
    if (!json_str || !out_temp_c || !out_wmo_code) {
        return false;
    }
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        return false;
    }
    cJSON *current = cJSON_GetObjectItem(root, "current");
    if (!cJSON_IsObject(current)) {
        cJSON_Delete(root);
        return false;
    }
    cJSON *temp_item = cJSON_GetObjectItem(current, "temperature_2m");
    cJSON *code_item = cJSON_GetObjectItem(current, "weather_code");
    cJSON *is_day_item = cJSON_GetObjectItem(current, "is_day");
    static const int wmo_codes[] = {0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57,
                                  61, 63, 65, 66, 67, 71, 73, 75, 77,
                                  80, 81, 82, 85, 86, 95, 96, 99};
    bool valid_code = false;
    if (cJSON_IsNumber(code_item) && isfinite(code_item->valuedouble)) {
        for (size_t i = 0; i < sizeof(wmo_codes) / sizeof(wmo_codes[0]); ++i) {
            if (code_item->valuedouble == wmo_codes[i]) valid_code = true;
        }
    }
    /* Celsius bounds exclude nonsensical payloads before rounding to int. */
    if (!cJSON_IsNumber(temp_item) || !isfinite(temp_item->valuedouble) ||
        temp_item->valuedouble < -100 || temp_item->valuedouble > 100 || !valid_code ||
        (is_day_item && (!cJSON_IsNumber(is_day_item) ||
                        (is_day_item->valuedouble != 0 && is_day_item->valuedouble != 1)))) {
        cJSON_Delete(root);
        return false;
    }
    double temp = temp_item->valuedouble;
    *out_temp_c = (int)(temp >= 0 ? (temp + 0.5) : (temp - 0.5));
    *out_wmo_code = code_item->valueint;
    if (out_is_day) {
        *out_is_day = is_day_item ? (is_day_item->valueint != 0) : true;
    }
    cJSON_Delete(root);
    return true;
}

void weather_format_info(weather_info_t *info, bool is_live, int temp_c, int wmo_code, bool is_day, const char *time_str)
{
    if (!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
    info->is_live = is_live;
    info->has_last_success = is_live;
    info->is_day = is_day;
    info->temp_c = temp_c;
    info->condition = weather_map_wmo_code(wmo_code);

    const char *cond_str = "晴れ";
    const char *lore_str = "村の上には、青い空と雲の行列。";

    if (is_day) {
        switch (info->condition) {
        case WEATHER_COND_SUNNY:
            cond_str = "晴れ";
            lore_str = "村の上には、青い空と雲の行列。";
            break;
        case WEATHER_COND_CLOUDY:
            cond_str = "雲";
            lore_str = "村の上空には、静かに連なる青白い雲。";
            break;
        case WEATHER_COND_RAINY:
            cond_str = "雨";
            lore_str = "しとしと降る雨と提灯の灯り。";
            break;
        case WEATHER_COND_SNOWY:
            cond_str = "雪";
            lore_str = "静かに雪が降り、白くなる妖怪村。";
            break;
        case WEATHER_COND_THUNDER:
            cond_str = "雷";
            lore_str = "夜空を照らす雷、神の太鼓の音。";
            break;
        default:
            break;
        }
    } else {
        switch (info->condition) {
        case WEATHER_COND_SUNNY:
            cond_str = "晴れ（夜）";
            lore_str = "夜の妖怪村、提灯が灯り、静かに暮れる。";
            break;
        case WEATHER_COND_CLOUDY:
            cond_str = "雲（夜）";
            lore_str = "夜の妖怪村、提灯が灯り、静かに暮れる。";
            break;
        case WEATHER_COND_RAINY:
            cond_str = "雨（夜）";
            lore_str = "しとしと降る雨と提灯の灯り。";
            break;
        case WEATHER_COND_SNOWY:
            cond_str = "雪（夜）";
            lore_str = "静かに雪が降り、白くなる妖怪村。";
            break;
        case WEATHER_COND_THUNDER:
            cond_str = "雷（夜）";
            lore_str = "夜空を照らす雷、神の太鼓の音。";
            break;
        default:
            cond_str = "晴れ（夜）";
            lore_str = "夜の妖怪村、提灯が灯り、静かに暮れる。";
            break;
        }
    }

    if (time_str && strlen(time_str) > 0) {
        snprintf(info->update_time, sizeof(info->update_time), "%s", time_str);
    } else {
        snprintf(info->update_time, sizeof(info->update_time), "%s", is_live ? "--:--" : "09:41");
    }

    if (is_live) {
        snprintf(info->location, sizeof(info->location), "天気　妖怪村（東京）");
        snprintf(info->badge, sizeof(info->badge), "LIVE");
    } else {
        snprintf(info->location, sizeof(info->location), "天気　妖怪村");
        snprintf(info->badge, sizeof(info->badge), "DEMO");
    }

    snprintf(info->main_text, sizeof(info->main_text), "%s　%d℃　更新　%s",
             cond_str, temp_c, info->update_time);
    snprintf(info->lore_text, sizeof(info->lore_text), "%s", lore_str);
}

bool weather_info_is_fresh(const weather_info_t *info, int64_t now_ms)
{
    return info && info->has_last_success && !info->refresh_failed &&
           now_ms >= info->last_success_ms &&
           now_ms - info->last_success_ms < 15 * 60 * 1000;
}

void weather_set_refresh_state(weather_info_t *info, bool refreshing, bool failed)
{
    if (!info) return;
    info->refreshing = refreshing;
    if (failed) info->refresh_failed = true;
    if (failed) {
        info->is_live = false;
        snprintf(info->badge, sizeof(info->badge), "%s", info->has_last_success ? "STALE" : "DEMO");
    }
}

#ifndef HOST_TEST

static weather_info_t s_current_info;
static bool s_dirty = true;
static SemaphoreHandle_t s_weather_mutex = NULL;
static TaskHandle_t s_worker_task_handle = NULL;
static bool s_sntp_initialized = false;
static uint32_t s_connection_generation;

static void set_refresh_state(bool refreshing, bool failed)
{
    if (s_weather_mutex && xSemaphoreTake(s_weather_mutex, portMAX_DELAY) == pdTRUE) {
        weather_set_refresh_state(&s_current_info, refreshing, failed);
        s_dirty = true;
        xSemaphoreGive(s_weather_mutex);
    }
}

static void init_sntp_if_needed(void)
{
    if (s_sntp_initialized) {
        return;
    }
    setenv("TZ", "JST-9", 1);
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
    s_sntp_initialized = true;
    ESP_LOGI(TAG, "SNTP initialized (JST-9, pool.ntp.org)");
}

static bool fetch_open_meteo_http(char *response_buf, size_t max_len)
{
    const char *lat = "35.6895";
    const char *lon = "139.6917";
#ifdef CONFIG_YOKAI_WEATHER_LATITUDE
    if (strlen(CONFIG_YOKAI_WEATHER_LATITUDE) > 0) {
        lat = CONFIG_YOKAI_WEATHER_LATITUDE;
    }
#endif
#ifdef CONFIG_YOKAI_WEATHER_LONGITUDE
    if (strlen(CONFIG_YOKAI_WEATHER_LONGITUDE) > 0) {
        lon = CONFIG_YOKAI_WEATHER_LONGITUDE;
    }
#endif

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&current=temperature_2m,weather_code,is_day",
             lat, lon);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTPS failed (%s), trying HTTP fallback", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        snprintf(url, sizeof(url),
                 "http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&current=temperature_2m,weather_code,is_day",
                 lat, lon);
        config.url = url;
        config.crt_bundle_attach = NULL;
        client = esp_http_client_init(&config);
        if (!client) {
            return false;
        }
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP fallback open failed: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return false;
        }
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP status error: %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    int total_read = 0;
    while (total_read < (int)max_len - 1) {
        int read_len = esp_http_client_read(client, response_buf + total_read, max_len - 1 - total_read);
        if (read_len <= 0) {
            break;
        }
        total_read += read_len;
    }
    response_buf[total_read] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "Fetched weather JSON (%d bytes, content_length=%d)", total_read, content_length);
    return total_read > 0;
}

static void on_got_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "Wi-Fi Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    weather_service_trigger_refresh();
}

static void weather_worker_task(void *arg)
{
    (void)arg;
    static char s_json_buffer[1024];

    /* Ensure Wi-Fi STA subsystem is initialized */
    (void)board_ui_wifi_ensure_started();

    board_wifi_info_t wifi_info;
    board_ui_wifi_get_info(&wifi_info);
    if (wifi_info.state != BOARD_WIFI_CONNECTING && wifi_info.state != BOARD_WIFI_CONNECTED) {
#ifdef CONFIG_YOKAI_WIFI_SSID
        if (CONFIG_YOKAI_WIFI_SSID[0] != '\0') {
#ifdef CONFIG_YOKAI_WIFI_PASSWORD
            board_ui_wifi_connect(CONFIG_YOKAI_WIFI_SSID, CONFIG_YOKAI_WIFI_PASSWORD);
#else
            board_ui_wifi_connect(CONFIG_YOKAI_WIFI_SSID, "");
#endif
        } else
#endif
        {
            board_ui_wifi_reconnect_saved();
        }
    }

    while (1) {
        uint32_t generation;
        xSemaphoreTake(s_weather_mutex, portMAX_DELAY);
        generation = s_connection_generation;
        xSemaphoreGive(s_weather_mutex);
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info;
        bool has_ip = (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0);

        if (!has_ip) {
            set_refresh_state(false, true);
            /* Sleep until explicitly woken up by IP_EVENT_STA_GOT_IP or manual trigger.
             * NEVER call esp_wifi_connect() here so user disconnect/turn-off is respected! */
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        xSemaphoreTake(s_weather_mutex, portMAX_DELAY);
        if (generation != s_connection_generation) {
            xSemaphoreGive(s_weather_mutex);
            continue;
        }
        weather_set_refresh_state(&s_current_info, true, false);
        s_dirty = true;
        xSemaphoreGive(s_weather_mutex);
        init_sntp_if_needed();

        char time_buf[16] = {0};
        time_t now = 0;
        struct tm timeinfo = {0};
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year > (2020 - 1900)) {
            snprintf(time_buf, sizeof(time_buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        }

        if (fetch_open_meteo_http(s_json_buffer, sizeof(s_json_buffer))) {
            int temp_c = 0;
            int wmo_code = 0;
            bool is_day = true;
            if (weather_parse_open_meteo_json(s_json_buffer, &temp_c, &wmo_code, &is_day)) {
                weather_info_t new_info;
                weather_format_info(&new_info, true, temp_c, wmo_code, is_day, time_buf[0] ? time_buf : NULL);

                new_info.last_success_ms = esp_timer_get_time() / 1000;
                xSemaphoreTake(s_weather_mutex, portMAX_DELAY);
                /* A disconnect while HTTP was in flight invalidates its result. */
                if (generation == s_connection_generation) {
                    s_current_info = new_info;
                    s_dirty = true;
                }
                xSemaphoreGive(s_weather_mutex);
                ESP_LOGI(TAG, "Weather updated: %s", new_info.main_text);
                /* Wait 15 minutes before next normal poll */
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(15 * 60 * 1000));
                continue;
            }
        }

        set_refresh_state(false, true);
        /* Fetch failed; retry in 30 seconds if still connected */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));
    }
}

esp_err_t weather_service_init(void)
{
    if (s_weather_mutex != NULL) {
        return ESP_OK;
    }
    s_weather_mutex = xSemaphoreCreateMutex();
    if (!s_weather_mutex) {
        return ESP_ERR_NO_MEM;
    }

    /* Initialize with DEMO baseline */
    weather_format_info(&s_current_info, false, 26, 0, true, "14:30");
    s_dirty = true;

    /* Register IP event listener to wake up weather worker upon Wi-Fi connect */
    (void)esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              on_got_ip_event, NULL, NULL);

    BaseType_t res = xTaskCreate(weather_worker_task, "weather_worker", 8192, NULL, 4, &s_worker_task_handle);
    if (res != pdPASS) {
        vSemaphoreDelete(s_weather_mutex);
        s_weather_mutex = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Weather service started with DEMO baseline");
    return ESP_OK;
}

void weather_service_trigger_refresh(void)
{
    if (s_worker_task_handle && s_weather_mutex) {
        xSemaphoreTake(s_weather_mutex, portMAX_DELAY);
        bool notify = !s_current_info.refreshing;
        if (notify) {
            weather_set_refresh_state(&s_current_info, true, false);
            s_dirty = true;
        }
        xSemaphoreGive(s_weather_mutex);
        if (notify) xTaskNotifyGive(s_worker_task_handle);
    }
}

void weather_service_get_info(weather_info_t *out_info)
{
    if (!out_info) return;
    if (s_weather_mutex) {
        xSemaphoreTake(s_weather_mutex, portMAX_DELAY);
        bool fresh = weather_info_is_fresh(&s_current_info, esp_timer_get_time() / 1000);
        if (s_current_info.is_live != fresh) {
            s_dirty = true;
            snprintf(s_current_info.badge, sizeof(s_current_info.badge), "%s",
                     fresh ? "LIVE" : s_current_info.has_last_success ? "STALE" : "DEMO");
        }
        s_current_info.is_live = fresh;
        *out_info = s_current_info;
        xSemaphoreGive(s_weather_mutex);
    } else {
        weather_format_info(out_info, false, 26, 0, true, "14:30");
    }
}

bool weather_service_is_dirty(void)
{
    if (!s_weather_mutex) return false;
    xSemaphoreTake(s_weather_mutex, portMAX_DELAY);
    bool fresh = weather_info_is_fresh(&s_current_info, esp_timer_get_time() / 1000);
    if (s_current_info.is_live != fresh) {
        s_current_info.is_live = fresh;
        snprintf(s_current_info.badge, sizeof(s_current_info.badge), "%s",
                 fresh ? "LIVE" : s_current_info.has_last_success ? "STALE" : "DEMO");
        s_dirty = true;
    }
    bool dirty = s_dirty;
    xSemaphoreGive(s_weather_mutex);
    return dirty;
}

void weather_service_clear_dirty(void)
{
    if (!s_weather_mutex) return;
    xSemaphoreTake(s_weather_mutex, portMAX_DELAY);
    s_dirty = false;
    xSemaphoreGive(s_weather_mutex);
}

void weather_service_set_offline(void)
{
    if (!s_weather_mutex) return;
    xSemaphoreTake(s_weather_mutex, portMAX_DELAY);
    ++s_connection_generation;
    weather_set_refresh_state(&s_current_info, false, true);
    s_dirty = true;
    xSemaphoreGive(s_weather_mutex);
}

#endif /* !HOST_TEST */
