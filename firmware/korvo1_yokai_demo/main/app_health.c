/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_health.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#ifndef HOST_TEST
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_health";
#endif

#if !defined(HOST_TEST) && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && \
    CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64 && \
    CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
#define APP_HEALTH_RUNTIME_ENABLED 1
#else
#define APP_HEALTH_RUNTIME_ENABLED 0
#endif

#if APP_HEALTH_RUNTIME_ENABLED
/* ponytail: bounded 40-task snapshot, raise only if diagnostics report capacity. */
#define HEALTH_TASK_CAPACITY 40
static TaskStatus_t s_resource_tasks[HEALTH_TASK_CAPACITY];
static struct {
    TaskHandle_t handle;
    UBaseType_t id;
    uint64_t runtime;
} s_resource_previous[HEALTH_TASK_CAPACITY];
static UBaseType_t s_resource_previous_count;
static uint64_t s_resource_previous_wall;

static bool resource_delta(uint64_t now, uint64_t before, uint64_t *delta)
{
    /* Unsigned subtraction tolerates wrap; backwards resets are not intervals. */
    *delta = now - before;
    return *delta <= INT64_MAX;
}

static unsigned resource_percent_tenths(uint64_t runtime, uint64_t wall, unsigned cores)
{
    /* Divide before scaling to avoid overflowing a uint64_t counter. */
    return (unsigned)(1000.0 * ((double)runtime / (double)wall) / cores);
}

void app_health_log_resources(void)
{
    if (uxTaskGetNumberOfTasks() > HEALTH_TASK_CAPACITY) {
        ESP_LOGW(TAG, "[CPU_HEALTH] snapshot unavailable: capacity=%u", HEALTH_TASK_CAPACITY);
        s_resource_previous_count = 0;
        return;
    }
    configRUN_TIME_COUNTER_TYPE wall = 0;
    UBaseType_t count = uxTaskGetSystemState(s_resource_tasks, HEALTH_TASK_CAPACITY, &wall);
    if (!count) {
        /* Tasks can be created between the count check and the native snapshot. */
        ESP_LOGW(TAG, "[CPU_HEALTH] snapshot unavailable (task count changed)");
        s_resource_previous_count = 0;
        return;
    }
    uint64_t elapsed = 0;
    bool interval = s_resource_previous_count &&
                    resource_delta(wall, s_resource_previous_wall, &elapsed) && elapsed;
    if (!interval) ESP_LOGI(TAG, "[CPU_HEALTH] baseline tasks=%u", (unsigned)count);
    int idle_tenths[configNUMBER_OF_CORES];
    for (unsigned core = 0; core < configNUMBER_OF_CORES; ++core) idle_tenths[core] = -1;
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    for (UBaseType_t i = 0; i < count; ++i) {
        const TaskStatus_t *task = &s_resource_tasks[i];
        uint64_t delta = 0;
        bool valid = false;
        if (interval) {
            for (UBaseType_t j = 0; j < s_resource_previous_count; ++j) {
                if (task->xHandle == s_resource_previous[j].handle &&
                    task->xTaskNumber == s_resource_previous[j].id) {
                    valid = resource_delta(task->ulRunTimeCounter, s_resource_previous[j].runtime, &delta) &&
                            delta <= elapsed;
                    break;
                }
            }
        }
        const char *label = task->xHandle == self ? "ui_health" : "task";
        for (unsigned core = 0; core < configNUMBER_OF_CORES; ++core) {
            if (task->xHandle == xTaskGetIdleTaskHandleForCore(core)) {
                label = "idle";
                if (valid) idle_tenths[core] = resource_percent_tenths(delta, elapsed, 1);
            }
        }
        int affinity = -1;
#if configTASKLIST_INCLUDE_COREID
        affinity = task->xCoreID;
#endif
        /* pcTaskName may already be freed; log copied scalars and stable labels. */
        if (valid) {
            unsigned pct = resource_percent_tenths(delta, elapsed, configNUMBER_OF_CORES);
            ESP_LOGI(TAG, "[TASK_HEALTH] id=%u handle=%p label=%s affinity=%d runtime_us=%" PRIu64
                     " delta_us=%" PRIu64 " cpu_total=%u.%u%% stack_min_bytes=%u",
                     (unsigned)task->xTaskNumber, (void *)task->xHandle, label, affinity,
                     (uint64_t)task->ulRunTimeCounter, delta, pct / 10, pct % 10,
                     (unsigned)task->usStackHighWaterMark);
        } else {
            ESP_LOGI(TAG, "[TASK_HEALTH] id=%u handle=%p label=%s affinity=%d runtime_us=%" PRIu64
                     " cpu_total=unknown(new/reset) stack_min_bytes=%u",
                     (unsigned)task->xTaskNumber, (void *)task->xHandle, label, affinity,
                     (uint64_t)task->ulRunTimeCounter, (unsigned)task->usStackHighWaterMark);
        }
    }
    if (interval) {
        for (unsigned core = 0; core < configNUMBER_OF_CORES; ++core) {
            int idle = idle_tenths[core];
            if (idle < 0) {
                ESP_LOGI(TAG, "[CPU_HEALTH] core=%u idle=unknown busy=unknown interval_us=%" PRIu64,
                         core, elapsed);
            } else {
                unsigned busy = 1000 - idle;
                ESP_LOGI(TAG, "[CPU_HEALTH] core=%u idle=%u.%u%% busy=%u.%u%% interval_us=%" PRIu64,
                         core, (unsigned)idle / 10, (unsigned)idle % 10, busy / 10, busy % 10, elapsed);
            }
        }
    }
    /* Commit together: snapshot reordering must not overwrite unmatched history. */
    for (UBaseType_t i = 0; i < count; ++i) {
        s_resource_previous[i].handle = s_resource_tasks[i].xHandle;
        s_resource_previous[i].id = s_resource_tasks[i].xTaskNumber;
        s_resource_previous[i].runtime = s_resource_tasks[i].ulRunTimeCounter;
    }
    s_resource_previous_wall = wall;
    s_resource_previous_count = count;
}
#endif /* APP_HEALTH_RUNTIME_ENABLED */

#if !APP_HEALTH_RUNTIME_ENABLED
void app_health_log_resources(void) {}
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
