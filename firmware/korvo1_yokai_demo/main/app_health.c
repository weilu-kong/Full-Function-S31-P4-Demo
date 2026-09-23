/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_health.h"
#include <string.h>
#include <stdio.h>

#ifndef HOST_TEST
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_health";
#endif

void app_health_capture_heap(app_heap_snapshot_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
#ifndef HOST_TEST
    out->int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->int_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    out->int_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    out->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    out->psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    out->psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    out->psram_simd_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_SIMD);

    out->task_count = (uint32_t)uxTaskGetNumberOfTasks();
#endif
}

void app_health_log_heap(const char *tag)
{
#ifndef HOST_TEST
    app_heap_snapshot_t snap;
    app_health_capture_heap(&snap);
    ESP_LOGI(TAG, "[MEM_HEALTH] %s: int_free=%u (min=%u largest=%u) psram_free=%u (min=%u largest=%u simd=%u) tasks=%u",
             tag ? tag : "checkpoint",
             (unsigned)snap.int_free, (unsigned)snap.int_min, (unsigned)snap.int_largest,
             (unsigned)snap.psram_free, (unsigned)snap.psram_min, (unsigned)snap.psram_largest,
             (unsigned)snap.psram_simd_largest, (unsigned)snap.task_count);
#else
    (void)tag;
#endif
}
