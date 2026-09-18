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
#define ESP_ERR_NOT_SUPPORTED 0x106
#else
#include "esp_err.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define VISION_MAX_DETECTIONS 8
#define VISION_LABEL_SIZE     24

typedef enum {
    VISION_MODE_FACE = 0,
    VISION_MODE_OBJECT,
} vision_mode_t;

typedef enum {
    VISION_STATE_OFF = 0,
    VISION_STATE_STARTING,
    VISION_STATE_RUNNING,
    VISION_STATE_ERROR,
} vision_state_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    float confidence;
    int16_t class_id;
    char label[VISION_LABEL_SIZE];
} vision_box_t;

typedef struct {
    uint32_t frame_id;
    vision_mode_t mode;
    uint8_t count;
    vision_box_t boxes[VISION_MAX_DETECTIONS];
    int16_t face_id;
    float face_similarity;
    bool face_known;
    uint32_t inference_ms;
} vision_result_t;

typedef struct {
    uint32_t frames_captured;
    uint32_t frames_displayed;
    uint32_t frames_dropped;
    uint32_t face_inferences;
    uint32_t object_inferences;
    uint32_t camera_errors;
    uint32_t inference_errors;
    uint32_t avg_face_ms;
    uint32_t avg_object_ms;
} vision_diag_t;

/**
 * @brief Initialize the vision service infrastructure (queues, mutexes).
 * Does NOT start camera or load AI models.
 */
esp_err_t vision_service_init(void);

/**
 * @brief Start vision service (camera capture, inference pipeline).
 */
esp_err_t vision_service_start(void);

/**
 * @brief Stop vision service cleanly (drains queues, stops camera).
 */
void vision_service_stop(void);

/**
 * @brief Switch detection mode (FACE or OBJECT).
 */
esp_err_t vision_service_set_mode(vision_mode_t mode);

/**
 * @brief Get current active mode.
 */
vision_mode_t vision_service_get_mode(void);

/**
 * @brief Get current lifecycle state.
 */
vision_state_t vision_service_get_state(void);

#define VISION_PREVIEW_WIDTH  320
#define VISION_PREVIEW_HEIGHT 240

/**
 * @brief Retrieve latest RGB565 preview frame for display.
 * @param out_data Pointer to receiving pointer of image data
 * @param out_w Output image width (320)
 * @param out_h Output image height (240)
 * @return true if a fresh preview frame is ready, false otherwise.
 */
bool vision_service_get_preview_frame(const uint8_t **out_data, uint16_t *out_w, uint16_t *out_h);

/**
 * @brief Non-blocking poll for the latest inference result.
 * @return true if a result was retrieved, false otherwise.
 */
bool vision_service_poll_result(vision_result_t *result);

/**
 * @brief Request enrollment of the currently visible face.
 */
esp_err_t vision_service_enroll_face(void);

/**
 * @brief Delete the most recently enrolled face.
 */
esp_err_t vision_service_delete_last_face(void);

/**
 * @brief Clear all enrolled faces from database.
 */
esp_err_t vision_service_clear_faces(void);

/**
 * @brief Get vision diagnostic statistics.
 */
void vision_service_get_diag(vision_diag_t *diag);

#ifdef __cplusplus
}
#endif
