/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "weather_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"

#ifndef HOST_TEST
#include "esp_log.h"
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
    if (!current) {
        cJSON_Delete(root);
        return false;
    }
    cJSON *temp_item = cJSON_GetObjectItem(current, "temperature_2m");
    cJSON *code_item = cJSON_GetObjectItem(current, "weather_code");
    if (!temp_item || !code_item) {
        cJSON_Delete(root);
        return false;
    }
    double temp = temp_item->valuedouble;
    *out_temp_c = (int)(temp >= 0 ? (temp + 0.5) : (temp - 0.5));
    *out_wmo_code = code_item->valueint;
    if (out_is_day) {
        cJSON *is_day_item = cJSON_GetObjectItem(current, "is_day");
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
        snprintf(info->update_time, sizeof(info->update_time), "09:41");
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

#ifndef HOST_TEST

static weather_info_t s_current_info;
static bool s_dirty = true;
static SemaphoreHandle_t s_weather_mutex = NULL;
static TaskHandle_t s_worker_task_handle = NULL;
static bool s_sntp_initialized = false;

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

    wifi_config_t wifi_cfg = {0};
    bool has_credentials = false;
#ifdef CONFIG_YOKAI_WIFI_SSID
    if (strlen(CONFIG_YOKAI_WIFI_SSID) > 0) {
        strncpy((char *)wifi_cfg.sta.ssid, CONFIG_YOKAI_WIFI_SSID, sizeof(wifi_cfg.sta.ssid) - 1);
#ifdef CONFIG_YOKAI_WIFI_PASSWORD
        strncpy((char *)wifi_cfg.sta.password, CONFIG_YOKAI_WIFI_PASSWORD, sizeof(wifi_cfg.sta.password) - 1);
#endif
        has_credentials = true;
        (void)esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
        (void)esp_wifi_connect();
        ESP_LOGI(TAG, "Connecting to Kconfig Wi-Fi SSID '%s'...", CONFIG_YOKAI_WIFI_SSID);
    }
#endif

    if (!has_credentials) {
        if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) == ESP_OK && strlen((char *)wifi_cfg.sta.ssid) > 0) {
            if (strlen((char *)wifi_cfg.sta.password) >= 8) {
                wifi_cfg.sta.pmf_cfg.capable = true;
                wifi_cfg.sta.pmf_cfg.required = false;
                wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
                wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
                (void)esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
                has_credentials = true;
                (void)esp_wifi_connect();
                ESP_LOGI(TAG, "Connecting to stored Wi-Fi SSID '%s'...", (char *)wifi_cfg.sta.ssid);
            }
        }
    }

    while (1) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info;
        bool has_ip = (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0);

        if (!has_ip) {
            /* Sleep until explicitly woken up by IP_EVENT_STA_GOT_IP or manual trigger.
             * NEVER call esp_wifi_connect() here so user disconnect/turn-off is respected! */
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

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

                if (xSemaphoreTake(s_weather_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    s_current_info = new_info;
                    s_dirty = true;
                    xSemaphoreGive(s_weather_mutex);
                }
                ESP_LOGI(TAG, "Weather updated: %s", new_info.main_text);
                /* Wait 15 minutes before next normal poll */
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(15 * 60 * 1000));
                continue;
            }
        }

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

    BaseType_t res = xTaskCreate(weather_worker_task, "weather_worker", 4096, NULL, 4, &s_worker_task_handle);
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
    if (s_worker_task_handle) {
        xTaskNotifyGive(s_worker_task_handle);
    }
}

void weather_service_get_info(weather_info_t *out_info)
{
    if (!out_info) {
        return;
    }
    if (s_weather_mutex && xSemaphoreTake(s_weather_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        *out_info = s_current_info;
        xSemaphoreGive(s_weather_mutex);
    } else {
        *out_info = s_current_info;
    }
}

bool weather_service_is_dirty(void)
{
    return s_dirty;
}

void weather_service_clear_dirty(void)
{
    s_dirty = false;
}

void weather_service_set_offline(void)
{
    if (s_weather_mutex && xSemaphoreTake(s_weather_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        weather_format_info(&s_current_info, false, 26, 0, true, "14:30");
        s_dirty = true;
        xSemaphoreGive(s_weather_mutex);
    } else {
        weather_format_info(&s_current_info, false, 26, 0, true, "14:30");
        s_dirty = true;
    }
}

#endif /* !HOST_TEST */
