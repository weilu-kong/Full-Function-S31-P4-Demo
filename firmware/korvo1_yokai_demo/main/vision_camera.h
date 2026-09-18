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
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
#else
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define VISION_CAM_WIDTH  640
#define VISION_CAM_HEIGHT 480
#define VISION_CAM_BUFFER_COUNT 2

typedef enum {
    VISION_PIXFMT_RGB565 = 0,
    VISION_PIXFMT_RGB888,
    VISION_PIXFMT_YUV422,
    VISION_PIXFMT_JPEG,
} vision_pixel_format_t;

typedef struct {
    void *data;
    uint16_t width;
    uint16_t height;
    uint32_t frame_id;
    vision_pixel_format_t format;
    uint32_t buffer_index;
    void *driver_private;
} vision_camera_frame_t;

/**
 * @brief Initialize camera driver (BSP start + V4L2 device open).
 */
esp_err_t vision_camera_init(void);

/**
 * @brief Start video streaming (VIDIOC_STREAMON).
 */
esp_err_t vision_camera_start(void);

/**
 * @brief Acquire the next available video frame from DMA.
 * Must be returned via vision_camera_release().
 */
esp_err_t vision_camera_acquire(vision_camera_frame_t *frame, TickType_t timeout);

/**
 * @brief Release video frame back to DMA buffer queue.
 */
void vision_camera_release(vision_camera_frame_t *frame);

/**
 * @brief Stop video streaming (VIDIOC_STREAMOFF).
 */
void vision_camera_stop(void);

/**
 * @brief De-initialize camera and close device handle.
 */
void vision_camera_deinit(void);

/**
 * @brief Check if camera is currently streaming.
 */
bool vision_camera_is_streaming(void);

#ifdef __cplusplus
}
#endif
