/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "storage_service.h"
#include <stdio.h>
#include <string.h>

#ifndef HOST_TEST
#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "storage_svc";
static storage_state_t s_storage_state = STORAGE_STATE_UNINITIALIZED;
#else
static storage_state_t s_storage_state = STORAGE_STATE_READY;
#endif

esp_err_t app_storage_init(void)
{
#ifndef HOST_TEST
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/storage",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = false  /* Do not erase/format automatically */
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        s_storage_state = STORAGE_STATE_MOUNT_FAILED;
        ESP_LOGE(TAG, "Failed to mount /storage SPIFFS (%s); auto-format is disabled to protect user data",
                 esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Storage SPIFFS mounted: total=%u bytes, used=%u bytes",
                 (unsigned)total, (unsigned)used);
    }
    s_storage_state = STORAGE_STATE_READY;
    return ESP_OK;
#else
    s_storage_state = STORAGE_STATE_READY;
    return ESP_OK;
#endif
}

bool app_storage_is_ready(void)
{
    return s_storage_state == STORAGE_STATE_READY;
}

storage_state_t app_storage_state(void)
{
    return s_storage_state;
}

esp_err_t app_storage_format(void)
{
#ifndef HOST_TEST
    ESP_LOGW(TAG, "Explicitly formatting storage partition...");
    esp_err_t ret = esp_spiffs_format("storage");
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Storage partition format successful");
        s_storage_state = STORAGE_STATE_READY;
    } else {
        ESP_LOGE(TAG, "Storage partition format failed: %s", esp_err_to_name(ret));
        s_storage_state = STORAGE_STATE_MOUNT_FAILED;
    }
    return ret;
#else
    s_storage_state = STORAGE_STATE_READY;
    return ESP_OK;
#endif
}
