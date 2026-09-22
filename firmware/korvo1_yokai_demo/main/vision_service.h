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
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NOT_SUPPORTED 0x106
#else
#include "esp_err.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define VISION_MAX_DETECTIONS 8
#define VISION_LABEL_SIZE     24
#define VISION_FACE_NAME_MAX_BYTES 48
#define VISION_MAX_PERSONS 10
#define VISION_FACE_SAMPLES_PER_PERSON 5
#define VISION_FACE_MATCH_THRESHOLD_INITIAL 0.60f
#define VISION_RECOGNITION_MIN_INTERVAL_MS 250

typedef enum {
    VISION_MODE_FACE = 0,
    VISION_MODE_OBJECT,
} vision_mode_t;

typedef enum {
    VISION_STATE_OFF = 0,
    VISION_STATE_STARTING,
    VISION_STATE_RUNNING,
    VISION_STATE_STOPPING,
    VISION_STATE_ERROR,
} vision_state_t;

typedef enum {
    VISION_FACE_MATCH_NONE = 0,
    VISION_FACE_MATCH_UNKNOWN,
    VISION_FACE_MATCH_KNOWN,
    VISION_FACE_MATCH_ERROR,
} vision_face_match_state_t;

typedef enum {
    VISION_ENROLL_IDLE = 0,
    VISION_ENROLL_WAIT_FACE,
    VISION_ENROLL_SAMPLING,
    VISION_ENROLL_COMMITTING,
    VISION_ENROLL_SUCCESS,
    VISION_ENROLL_CANCELLED,
    VISION_ENROLL_ERROR,
} vision_enroll_state_t;

typedef enum {
    VISION_ENROLL_SAMPLE_WAITING = 0,
    VISION_ENROLL_SAMPLE_STABILIZING,
    VISION_ENROLL_SAMPLE_CAPTURING,
    VISION_ENROLL_SAMPLE_ACCEPTED,
    VISION_ENROLL_SAMPLE_RETRY,
} vision_enroll_sample_state_t;

typedef enum {
    VISION_ENROLL_ERR_NONE                     = 0,

    VISION_ENROLL_ERR_NO_FACE                  = 1001,
    VISION_ENROLL_ERR_MULTIPLE_FACES           = 1002,
    VISION_ENROLL_ERR_FACE_TOO_SMALL           = 1003,
    VISION_ENROLL_ERR_FACE_OFF_CENTER          = 1004,
    VISION_ENROLL_ERR_LOW_DETECT_SCORE         = 1005,
    VISION_ENROLL_ERR_WRONG_POSE               = 1006,
    VISION_ENROLL_ERR_FACE_UNSTABLE            = 1007,

    VISION_ENROLL_ERR_MFN_NO_MEMORY             = 2001,
    VISION_ENROLL_ERR_FEATURE_EXTRACT_FAILED    = 2002,
    VISION_ENROLL_ERR_FEATURE_ID_INVALID        = 2003,

    VISION_ENROLL_ERR_METADATA_SAVE_FAILED      = 3001,
    VISION_ENROLL_ERR_FACE_DB_FAILED            = 3002,

    VISION_ENROLL_ERR_EMPTY_NAME                = 4001,
    VISION_ENROLL_ERR_DUPLICATE_NAME            = 4002,
    VISION_ENROLL_ERR_MAX_PERSONS               = 4003,
    VISION_ENROLL_ERR_COMMAND_TIMEOUT           = 4004,
    VISION_ENROLL_ERR_INVALID_SLOT              = 4005,
    VISION_ENROLL_ERR_CANCELLED                 = 4006,
} vision_enroll_error_code_t;

#define ENROLL_STABLE_MIN_MS       700
#define ENROLL_STABLE_MIN_FRAMES   5
#define ENROLL_SUCCESS_HOLD_MS     900
#define ENROLL_RETRY_HOLD_MS       1200
#define ENROLL_FINAL_HOLD_MS       2000

typedef struct {
    uint8_t slot;
    char name[VISION_FACE_NAME_MAX_BYTES + 1];
    uint8_t feature_count;
} vision_person_summary_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    float confidence;
    int16_t class_id;
    int16_t person_slot;              /* -1 if not mapped */
    uint16_t matched_feature_id;      /* raw ESP-DL feature ID */
    float similarity;                 /* cosine similarity returned by ESP-DL */
    vision_face_match_state_t match_state;
    char name[VISION_FACE_NAME_MAX_BYTES + 1];
    char label[VISION_LABEL_SIZE];
} vision_box_t;

typedef struct {
    uint32_t frame_id;
    vision_mode_t mode;
    uint8_t count;
    vision_box_t boxes[VISION_MAX_DETECTIONS];
    /* Primary face summary (for side HUD) */
    int16_t primary_person_slot;
    uint16_t primary_feature_id;
    float primary_similarity;
    vision_face_match_state_t primary_match_state;
    char primary_name[VISION_FACE_NAME_MAX_BYTES + 1];
    uint32_t inference_ms;
    uint32_t recognition_ms;

    /* Enrollment progress status */
    vision_enroll_state_t enroll_state;
    vision_enroll_sample_state_t enroll_sample_state;
    vision_enroll_error_code_t enroll_error_code;
    esp_err_t enroll_backend_error;
    uint8_t enroll_sample_count;
    uint8_t enroll_target_count;
    char enroll_name[VISION_FACE_NAME_MAX_BYTES + 1];
    char enroll_prompt[64];
    esp_err_t enroll_error;
} vision_result_t;

typedef struct {
    uint32_t frames_captured;
    uint32_t frames_displayed;
    uint32_t frames_dropped;
    uint32_t face_inferences;
    uint32_t face_recognitions;
    uint32_t recognition_known;
    uint32_t recognition_unknown;
    uint32_t object_inferences;
    uint32_t camera_errors;
    uint32_t inference_errors;
    uint32_t avg_face_ms;
    uint32_t avg_recognition_ms;
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
 */
bool vision_service_get_preview_frame(const uint8_t **out_data, uint16_t *out_w, uint16_t *out_h);

/**
 * @brief Non-blocking poll for the latest inference result.
 */
bool vision_service_poll_result(vision_result_t *result);

/**
 * @brief Start enrollment for a person with given UTF-8 name.
 */
esp_err_t vision_service_begin_enrollment(const char *utf8_name);

/**
 * @brief Cancel active enrollment session.
 */
esp_err_t vision_service_cancel_enrollment(void);

/**
 * @brief Delete a registered person by slot index.
 */
esp_err_t vision_service_delete_person(uint8_t person_slot);

/**
 * @brief Re-register an existing person slot with new face samples.
 */
esp_err_t vision_service_reregister_person(uint8_t person_slot, const char *utf8_name);

/**
 * @brief Clear all registered people and feature database.
 */
esp_err_t vision_service_clear_all_people(void);

/**
 * @brief Get number of actively enrolled people (0..10).
 */
int vision_service_get_enrolled_count(void);

/**
 * @brief Get summary list of all registered people.
 */
esp_err_t vision_service_get_people_summary(vision_person_summary_t *out, size_t capacity, size_t *out_count);

/**
 * @brief Legacy stub: enroll face.
 */
esp_err_t vision_service_enroll_face(void);

/**
 * @brief Legacy stub: delete last face.
 */
esp_err_t vision_service_delete_last_face(void);

/**
 * @brief Legacy stub: clear all faces.
 */
esp_err_t vision_service_clear_faces(void);

/**
 * @brief Get human-readable description for an enrollment error code.
 */
const char *vision_enroll_error_to_str(vision_enroll_error_code_t code);

/**
 * @brief Get vision diagnostic statistics.
 */
void vision_service_get_diag(vision_diag_t *diag);

#ifdef __cplusplus
}
#endif
