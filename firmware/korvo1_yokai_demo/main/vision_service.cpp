/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <algorithm>
#include "vision_service.h"
#include "vision_camera.h"

#ifdef HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#define LOG_TAG "vision_service"
#define ESP_LOGI(t, f, ...) printf("[%s] " f "\n", t, ##__VA_ARGS__)
#define ESP_LOGW(t, f, ...) printf("[%s][WARN] " f "\n", t, ##__VA_ARGS__)
#define ESP_LOGE(t, f, ...) printf("[%s][ERR] " f "\n", t, ##__VA_ARGS__)
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "human_face_detect.hpp"
#endif

static const char *TAG = "vision_service";

static bool s_inited = false;
static vision_mode_t s_mode = VISION_MODE_FACE;
static vision_state_t s_state = VISION_STATE_OFF;
static vision_diag_t s_diag = {};

/* Preview double-buffering with tear-free display synchronization */
#define PREVIEW_FRAME_SIZE (VISION_PREVIEW_WIDTH * VISION_PREVIEW_HEIGHT * 2)
static uint8_t *s_preview_buf[2] = {NULL, NULL};
static volatile int s_disp_idx = 0;   /* Buffer currently displayed by LVGL */
static volatile int s_ready_idx = 0;  /* Buffer with latest complete camera frame */
static volatile bool s_preview_dirty = false;
static volatile bool s_capture_running = false;
static volatile bool s_infer_running = false;

#ifndef HOST_TEST
static SemaphoreHandle_t s_lock = NULL;
static SemaphoreHandle_t s_infer_sem = NULL;
static SemaphoreHandle_t s_preview_mutex = NULL;
static QueueHandle_t s_result_queue = NULL;
static TaskHandle_t s_capture_task_handle = NULL;
static TaskHandle_t s_infer_task_handle = NULL;

static HumanFaceDetect *s_face_detect = nullptr;

static void vision_capture_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Vision capture task running (Priority 5)");

    while (s_capture_running) {
        vision_camera_frame_t frame;
        esp_err_t err = vision_camera_acquire(&frame, pdMS_TO_TICKS(100));
        if (err != ESP_OK) {
            s_diag.camera_errors++;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        s_diag.frames_captured++;

        /* Always write to the buffer that is NOT currently displayed by LVGL */
        int write_idx = 1 - s_disp_idx;
        if (s_preview_buf[write_idx] && frame.data) {
            uint16_t *dst = (uint16_t *)s_preview_buf[write_idx];
            if (frame.format == VISION_PIXFMT_RGB565) {
                const uint16_t *src = (const uint16_t *)frame.data;
                for (int y = 0; y < VISION_PREVIEW_HEIGHT; y++) {
                    int src_y = (y * frame.height) / VISION_PREVIEW_HEIGHT;
                    const uint16_t *src_row = &src[src_y * frame.width];
                    uint16_t *dst_row = &dst[y * VISION_PREVIEW_WIDTH];
                    for (int x = 0; x < VISION_PREVIEW_WIDTH; x++) {
                        int src_x = (x * frame.width) / VISION_PREVIEW_WIDTH;
                        dst_row[x] = src_row[src_x];
                    }
                }
            } else if (frame.format == VISION_PIXFMT_YUV422) {
                /* UYVY format: [U0, Y0, V0, Y1] per 2 horizontal pixels */
                const uint8_t *src = (const uint8_t *)frame.data;
                for (int y = 0; y < VISION_PREVIEW_HEIGHT; y++) {
                    int src_y = (y * frame.height) / VISION_PREVIEW_HEIGHT;
                    const uint8_t *src_row = &src[src_y * frame.width * 2];
                    uint16_t *dst_row = &dst[y * VISION_PREVIEW_WIDTH];
                    for (int x = 0; x < VISION_PREVIEW_WIDTH; x++) {
                        int src_x = ((x * frame.width) / VISION_PREVIEW_WIDTH) & ~1;
                        const uint8_t *p = &src_row[src_x * 2];
                        int u = p[0];
                        int y_val = p[1];
                        int v = p[2];
                        int c = y_val - 16;
                        int d = u - 128;
                        int e = v - 128;
                        if (c < 0) c = 0;
                        int r = (298 * c + 409 * e + 128) >> 8;
                        int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
                        int b = (298 * c + 516 * d + 128) >> 8;
                        if (r < 0) r = 0; else if (r > 255) r = 255;
                        if (g < 0) g = 0; else if (g > 255) g = 255;
                        if (b < 0) b = 0; else if (b > 255) b = 255;
                        dst_row[x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                    }
                }
            }

            /* Commit new ready frame under preview mutex */
            if (s_preview_mutex) {
                if (xSemaphoreTake(s_preview_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    s_ready_idx = write_idx;
                    s_preview_dirty = true;
                    xSemaphoreGive(s_preview_mutex);
                }
            } else {
                s_ready_idx = write_idx;
                s_preview_dirty = true;
            }

            /* Trigger inference task if ready */
            if (s_infer_sem && s_infer_running) {
                xSemaphoreGive(s_infer_sem);
            }
        }

        /* Immediately release DMA buffer back to driver */
        vision_camera_release(&frame);
    }

    ESP_LOGI(TAG, "Vision capture task exiting");
    s_capture_task_handle = NULL;
    vTaskDelete(NULL);
}

static void vision_inference_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Vision inference task running (Priority 4)");

    while (s_infer_running) {
        if (!s_infer_sem || xSemaphoreTake(s_infer_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        if (!s_infer_running) {
            break;
        }

        int cur_idx = s_ready_idx;
        if (!s_preview_buf[cur_idx]) {
            continue;
        }

        if (s_mode == VISION_MODE_FACE) {
            if (!s_face_detect) {
                ESP_LOGI(TAG, "Instantiating HumanFaceDetect model...");
                s_face_detect = new HumanFaceDetect();
            }

            dl::image::img_t img = {
                .data = (void *)s_preview_buf[cur_idx],
                .width = VISION_PREVIEW_WIDTH,
                .height = VISION_PREVIEW_HEIGHT,
                .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE
            };

            int64_t t0 = esp_timer_get_time();
            auto &faces = s_face_detect->run(img);
            int64_t t1 = esp_timer_get_time();
            uint32_t infer_ms = (uint32_t)((t1 - t0) / 1000);

            vision_result_t res;
            memset(&res, 0, sizeof(res));
            res.frame_id = s_diag.frames_captured;
            res.mode = VISION_MODE_FACE;
            res.inference_ms = infer_ms;
            res.count = (uint8_t)std::min(faces.size(), (size_t)VISION_MAX_DETECTIONS);

            int b_idx = 0;
            for (auto &f : faces) {
                if (b_idx >= VISION_MAX_DETECTIONS) break;
                res.boxes[b_idx].x = (int16_t)f.box[0];
                res.boxes[b_idx].y = (int16_t)f.box[1];
                res.boxes[b_idx].w = (int16_t)(f.box[2] - f.box[0]);
                res.boxes[b_idx].h = (int16_t)(f.box[3] - f.box[1]);
                res.boxes[b_idx].confidence = f.score;
                res.boxes[b_idx].class_id = (int16_t)f.category;
                snprintf(res.boxes[b_idx].label, sizeof(res.boxes[b_idx].label), "Face (%.0f%%)", f.score * 100.0f);
                b_idx++;
            }

            res.face_known = false;
            res.face_id = faces.empty() ? -1 : 0;

            s_diag.face_inferences++;
            s_diag.avg_face_ms = (s_diag.avg_face_ms == 0) ? infer_ms : (s_diag.avg_face_ms * 3 + infer_ms) / 4;

            if (!faces.empty()) {
                ESP_LOGI(TAG, "Face detected! count=%d, latency=%lums, score=%.2f, box=[%d,%d,%d,%d]",
                         (int)faces.size(), (unsigned long)infer_ms,
                         faces.front().score,
                         faces.front().box[0], faces.front().box[1],
                         faces.front().box[2] - faces.front().box[0],
                         faces.front().box[3] - faces.front().box[1]);
            } else if (s_diag.face_inferences % 30 == 0) {
                ESP_LOGI(TAG, "Face inference heartbeat: count=%lu, avg_latency=%lums",
                         (unsigned long)s_diag.face_inferences, (unsigned long)s_diag.avg_face_ms);
            }

            /* Push latest result to result queue (discard oldest if full) */
            if (s_result_queue) {
                if (uxQueueSpacesAvailable(s_result_queue) == 0) {
                    vision_result_t dummy;
                    xQueueReceive(s_result_queue, &dummy, 0);
                }
                xQueueSend(s_result_queue, &res, 0);
            }
        }
    }

    ESP_LOGI(TAG, "Vision inference task exiting");
    s_infer_task_handle = NULL;
    vTaskDelete(NULL);
}
#else
static vision_result_t s_host_result_slot;
static bool s_host_result_valid = false;
static uint8_t s_host_preview_buf[PREVIEW_FRAME_SIZE];
#endif

extern "C" esp_err_t vision_service_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

#ifndef HOST_TEST
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    s_infer_sem = xSemaphoreCreateBinary();
    if (!s_infer_sem) {
        ESP_LOGE(TAG, "Failed to create inference semaphore");
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_result_queue = xQueueCreate(2, sizeof(vision_result_t));
    if (!s_result_queue) {
        ESP_LOGE(TAG, "Failed to create result queue");
        vSemaphoreDelete(s_infer_sem);
        s_infer_sem = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_preview_mutex = xSemaphoreCreateMutex();
    if (!s_preview_mutex) {
        ESP_LOGE(TAG, "Failed to create preview mutex");
        vSemaphoreDelete(s_result_queue);
        s_result_queue = NULL;
        vSemaphoreDelete(s_infer_sem);
        s_infer_sem = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* Allocate ping-pong preview buffers in PSRAM */
    for (int i = 0; i < 2; i++) {
        s_preview_buf[i] = (uint8_t *)heap_caps_malloc(PREVIEW_FRAME_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_preview_buf[i]) {
            ESP_LOGE(TAG, "Failed to allocate preview buffer %d in PSRAM", i);
            return ESP_ERR_NO_MEM;
        }
        memset(s_preview_buf[i], 0, PREVIEW_FRAME_SIZE);
    }
#else
    s_preview_buf[0] = s_host_preview_buf;
    s_preview_buf[1] = s_host_preview_buf;
#endif

    s_disp_idx = 0;
    s_ready_idx = 0;
    s_preview_dirty = false;
    s_state = VISION_STATE_OFF;
    s_mode = VISION_MODE_FACE;
    memset(&s_diag, 0, sizeof(s_diag));
    s_inited = true;

    ESP_LOGI(TAG, "Vision service initialized (Preview: %dx%d)",
             VISION_PREVIEW_WIDTH, VISION_PREVIEW_HEIGHT);
    return ESP_OK;
}

extern "C" esp_err_t vision_service_start(void)
{
    if (!s_inited) {
        esp_err_t err = vision_service_init();
        if (err != ESP_OK) return err;
    }

#ifndef HOST_TEST
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
#endif

    if (s_state == VISION_STATE_RUNNING) {
#ifndef HOST_TEST
        xSemaphoreGive(s_lock);
#endif
        return ESP_OK;
    }

    s_state = VISION_STATE_STARTING;
    ESP_LOGI(TAG, "Starting vision service in mode %d", s_mode);

#ifndef HOST_TEST
    esp_err_t cam_err = vision_camera_start();
    if (cam_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start camera hardware: %s", esp_err_to_name(cam_err));
        s_state = VISION_STATE_ERROR;
        xSemaphoreGive(s_lock);
        return cam_err;
    }

    s_capture_running = true;
    BaseType_t task_ret = xTaskCreate(
        vision_capture_task,
        "vis_capture",
        4096,
        NULL,
        5, /* Priority 5 (below LVGL 6, below Voice 7/9, below Audio 10) */
        &s_capture_task_handle
    );
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create vision capture task");
        vision_camera_stop();
        s_capture_running = false;
        s_state = VISION_STATE_ERROR;
        xSemaphoreGive(s_lock);
        return ESP_FAIL;
    }

    s_infer_running = true;
    BaseType_t infer_ret = xTaskCreate(
        vision_inference_task,
        "vis_infer",
        8192,
        NULL,
        4, /* Priority 4: below capture 5, below LVGL 6 */
        &s_infer_task_handle
    );
    if (infer_ret != pdPASS) {
        ESP_LOGW(TAG, "Failed to create inference task, running preview only");
        s_infer_running = false;
    }
#endif

    s_state = VISION_STATE_RUNNING;

#ifndef HOST_TEST
    xSemaphoreGive(s_lock);
#endif
    return ESP_OK;
}

extern "C" void vision_service_stop(void)
{
    if (!s_inited) {
        return;
    }

#ifndef HOST_TEST
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_capture_running = false;
        s_infer_running = false;
        if (s_infer_sem) {
            xSemaphoreGive(s_infer_sem);
        }

        /* Wait for capture task and inference task to cleanly exit BEFORE stopping camera */
        int wait_ms = 0;
        while ((s_capture_task_handle != NULL || s_infer_task_handle != NULL) && wait_ms < 500) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait_ms += 10;
        }

        vision_camera_stop();

        s_state = VISION_STATE_OFF;
        s_preview_dirty = false;
        if (s_result_queue) {
            xQueueReset(s_result_queue);
        }
        xSemaphoreGive(s_lock);
    } else {
        s_capture_running = false;
        s_infer_running = false;
        if (s_infer_sem) {
            xSemaphoreGive(s_infer_sem);
        }
        int wait_ms = 0;
        while ((s_capture_task_handle != NULL || s_infer_task_handle != NULL) && wait_ms < 500) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait_ms += 10;
        }
        vision_camera_stop();
        s_state = VISION_STATE_OFF;
        s_preview_dirty = false;
    }
#else
    s_capture_running = false;
    s_infer_running = false;
    s_state = VISION_STATE_OFF;
    s_host_result_valid = false;
    s_preview_dirty = false;
#endif

    ESP_LOGI(TAG, "Vision service stopped");
}

extern "C" esp_err_t vision_service_set_mode(vision_mode_t mode)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef HOST_TEST
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
#endif

    if (s_mode != mode) {
        ESP_LOGI(TAG, "Switching mode: %d -> %d", s_mode, mode);
        s_mode = mode;
#ifndef HOST_TEST
        if (s_result_queue) {
            xQueueReset(s_result_queue);
        }
#else
        s_host_result_valid = false;
#endif
    }

#ifndef HOST_TEST
    xSemaphoreGive(s_lock);
#endif
    return ESP_OK;
}

extern "C" vision_mode_t vision_service_get_mode(void)
{
    return s_mode;
}

extern "C" vision_state_t vision_service_get_state(void)
{
    return s_state;
}

extern "C" bool vision_service_get_preview_frame(const uint8_t **out_data, uint16_t *out_w, uint16_t *out_h)
{
    if (!out_data || !out_w || !out_h || !s_inited || s_state != VISION_STATE_RUNNING) {
        return false;
    }

    if (!s_preview_dirty) {
        return false;
    }

#ifndef HOST_TEST
    if (s_preview_mutex) {
        if (xSemaphoreTake(s_preview_mutex, 0) != pdTRUE) {
            return false;
        }
    }
#endif

    s_disp_idx = s_ready_idx;
    *out_data = s_preview_buf[s_disp_idx];
    *out_w = VISION_PREVIEW_WIDTH;
    *out_h = VISION_PREVIEW_HEIGHT;
    s_preview_dirty = false;
    s_diag.frames_displayed++;

#ifndef HOST_TEST
    if (s_preview_mutex) {
        xSemaphoreGive(s_preview_mutex);
    }
#endif

    return true;
}

extern "C" bool vision_service_poll_result(vision_result_t *result)
{
    if (!result || !s_inited || s_state != VISION_STATE_RUNNING) {
        return false;
    }

#ifndef HOST_TEST
    if (!s_result_queue) {
        return false;
    }
    return (xQueueReceive(s_result_queue, result, 0) == pdTRUE);
#else
    if (s_host_result_valid) {
        *result = s_host_result_slot;
        s_host_result_valid = false;
        return true;
    }
    return false;
#endif
}

extern "C" esp_err_t vision_service_enroll_face(void)
{
    ESP_LOGI(TAG, "Face enrollment requested (Phase 6)");
    return ESP_OK;
}

extern "C" esp_err_t vision_service_delete_last_face(void)
{
    ESP_LOGI(TAG, "Delete last face requested (Phase 6)");
    return ESP_OK;
}

extern "C" esp_err_t vision_service_clear_faces(void)
{
    ESP_LOGI(TAG, "Clear all faces requested (Phase 6)");
    return ESP_OK;
}

extern "C" void vision_service_get_diag(vision_diag_t *diag)
{
    if (!diag) return;
    *diag = s_diag;
}
