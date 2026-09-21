/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "vision_service.h"
#include "vision_camera.h"

#ifdef HOST_TEST
#define LOG_TAG "vision_service"
#define ESP_LOGI(t, f, ...) printf("[%s] " f "\n", t, ##__VA_ARGS__)
#define ESP_LOGW(t, f, ...) printf("[%s][WARN] " f "\n", t, ##__VA_ARGS__)
#define ESP_LOGE(t, f, ...) printf("[%s][ERR] " f "\n", t, ##__VA_ARGS__)
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#endif

static const char *TAG = "vision_service";

static bool s_inited = false;
static vision_mode_t s_mode = VISION_MODE_FACE;
static vision_state_t s_state = VISION_STATE_OFF;
static vision_diag_t s_diag = {};

/* Preview buffers have distinct display, ready, and inference ownership. */
#define PREVIEW_FRAME_SIZE (VISION_PREVIEW_WIDTH * VISION_PREVIEW_HEIGHT * 2)
static uint8_t *s_preview_buf[3] = {NULL, NULL, NULL};
static volatile int s_disp_idx = 0;   /* Buffer currently displayed by LVGL */
static volatile int s_ready_idx = 0;  /* Buffer with latest complete camera frame */
static volatile int s_infer_idx = -1; /* Buffer owned by ESP-DL */
static volatile int s_write_idx = -1; /* Buffer reserved by the capture task */
static volatile bool s_preview_dirty = false;
static volatile uint32_t s_preview_publish_count = 0;
static volatile uint32_t s_preview_consume_call_count = 0;
static volatile uint32_t s_preview_consume_ok_count = 0;
static volatile uint32_t s_preview_dirty_miss_count = 0;
static volatile uint32_t s_preview_consume_lock_busy_count = 0;
static volatile uint32_t s_preview_writer_lock_busy_count = 0;
static volatile uint32_t s_preview_publish_lock_busy_count = 0;
static volatile uint32_t s_preview_no_free_buffer_count = 0;
static volatile uint32_t s_preview_last_publish_ms = 0;
static volatile uint32_t s_preview_last_consume_ms = 0;
static volatile bool s_capture_running = false;
static volatile bool s_infer_running = false;

static int find_free_preview_buffer(void)
{
    for (int i = 0; i < 3; ++i) {
        if (i != s_disp_idx && i != s_infer_idx && i != s_write_idx) return i;
    }
    return -1;
}

/* --- Metadata and Storage Models (Sections 11 & 12) --- */
#define VISION_PEOPLE_META_MAGIC   0x59464D44 /* "YFMD" */
#define VISION_PEOPLE_META_VERSION 1
#define VISION_PEOPLE_META_PATH    "/storage/face_people.meta"
#define VISION_PEOPLE_TMP_PATH     "/storage/face_people.tmp"
#define VISION_FACE_DB_PATH        "/storage/face_db.bin"

typedef struct {
    bool active;
    uint8_t slot;
    char name[VISION_FACE_NAME_MAX_BYTES + 1];
    uint8_t feature_count;
    uint16_t feature_ids[VISION_FACE_SAMPLES_PER_PERSON];
    uint32_t created_at_seq;
} vision_person_record_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    uint32_t generation;
    uint8_t person_count;
    uint8_t reserved[3];
    vision_person_record_t persons[VISION_MAX_PERSONS];
    uint32_t crc32;
} vision_people_file_t;

static vision_people_file_t s_people_file;
static float s_match_threshold = VISION_FACE_MATCH_THRESHOLD_INITIAL;

/* Standard CRC32 */
static uint32_t calc_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

static void face_people_init_empty(void)
{
    memset(&s_people_file, 0, sizeof(s_people_file));
    s_people_file.magic = VISION_PEOPLE_META_MAGIC;
    s_people_file.version = VISION_PEOPLE_META_VERSION;
    s_people_file.record_size = sizeof(vision_person_record_t);
    s_people_file.generation = 1;
    s_people_file.person_count = 0;
    for (int i = 0; i < VISION_MAX_PERSONS; i++) {
        s_people_file.persons[i].slot = (uint8_t)i;
        s_people_file.persons[i].active = false;
    }
    s_people_file.crc32 = calc_crc32((const uint8_t *)&s_people_file,
                                     offsetof(vision_people_file_t, crc32));
}

static bool face_people_validate(const vision_people_file_t *file)
{
    if (!file) return false;
    if (file->magic != VISION_PEOPLE_META_MAGIC) return false;
    if (file->version != VISION_PEOPLE_META_VERSION) return false;
    if (file->record_size != sizeof(vision_person_record_t)) return false;
    if (file->person_count > VISION_MAX_PERSONS) return false;

    uint32_t expected_crc = calc_crc32((const uint8_t *)file,
                                       offsetof(vision_people_file_t, crc32));
    if (file->crc32 != expected_crc) {
        ESP_LOGE(TAG, "Metadata CRC mismatch: expected 0x%08lx, got 0x%08lx",
                 (unsigned long)expected_crc, (unsigned long)file->crc32);
        return false;
    }

    uint8_t active_count = 0;
    for (int i = 0; i < VISION_MAX_PERSONS; i++) {
        if (file->persons[i].active) {
            active_count++;
            if (file->persons[i].slot != i) return false;
            if (file->persons[i].feature_count != VISION_FACE_SAMPLES_PER_PERSON) return false;
            if (strnlen(file->persons[i].name, VISION_FACE_NAME_MAX_BYTES + 1) > VISION_FACE_NAME_MAX_BYTES) return false;
        }
    }
    return (active_count == file->person_count);
}

static esp_err_t face_people_save_atomic(void)
{
    s_people_file.generation++;
    s_people_file.crc32 = calc_crc32((const uint8_t *)&s_people_file,
                                     offsetof(vision_people_file_t, crc32));

    FILE *f = fopen(VISION_PEOPLE_TMP_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", VISION_PEOPLE_TMP_PATH);
        return ESP_FAIL;
    }

    size_t written = fwrite(&s_people_file, sizeof(s_people_file), 1, f);
    fflush(f);
    fclose(f);

    if (written != 1) {
        ESP_LOGE(TAG, "Failed to write complete metadata to %s", VISION_PEOPLE_TMP_PATH);
        remove(VISION_PEOPLE_TMP_PATH);
        return ESP_FAIL;
    }

    /* Atomic rename tmp -> meta */
    if (rename(VISION_PEOPLE_TMP_PATH, VISION_PEOPLE_META_PATH) != 0) {
        ESP_LOGE(TAG, "Failed to rename %s to %s", VISION_PEOPLE_TMP_PATH, VISION_PEOPLE_META_PATH);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved %d people metadata (gen=%lu) to %s",
             s_people_file.person_count, (unsigned long)s_people_file.generation, VISION_PEOPLE_META_PATH);
    return ESP_OK;
}

static esp_err_t face_people_load(void)
{
    FILE *f = fopen(VISION_PEOPLE_META_PATH, "rb");
    if (!f) {
        ESP_LOGI(TAG, "No existing people metadata at %s, initializing empty", VISION_PEOPLE_META_PATH);
        face_people_init_empty();
        return ESP_OK;
    }

    vision_people_file_t candidate;
    size_t n = fread(&candidate, sizeof(candidate), 1, f);
    fclose(f);

    if (n == 1 && face_people_validate(&candidate)) {
        s_people_file = candidate;
        ESP_LOGI(TAG, "Successfully loaded metadata: %d people (gen=%lu)",
                 s_people_file.person_count, (unsigned long)s_people_file.generation);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Corrupt or invalid metadata at %s, keeping existing without overwriting", VISION_PEOPLE_META_PATH);
    face_people_init_empty();
    return ESP_FAIL;
}

static const vision_person_record_t *find_person_by_feature_id(uint16_t id)
{
    if (id == 0) return NULL;
    for (int i = 0; i < VISION_MAX_PERSONS; i++) {
        if (!s_people_file.persons[i].active) continue;
        for (int s = 0; s < s_people_file.persons[i].feature_count; s++) {
            if (s_people_file.persons[i].feature_ids[s] == id) {
                return &s_people_file.persons[i];
            }
        }
    }
    return NULL;
}

static const vision_person_record_t *find_person_by_name(const char *name)
{
    if (!name || name[0] == '\0') return NULL;
    for (int i = 0; i < VISION_MAX_PERSONS; i++) {
        if (s_people_file.persons[i].active && strcmp(s_people_file.persons[i].name, name) == 0) {
            return &s_people_file.persons[i];
        }
    }
    return NULL;
}

static int find_free_person_slot(void)
{
    for (int i = 0; i < VISION_MAX_PERSONS; i++) {
        if (!s_people_file.persons[i].active) {
            return i;
        }
    }
    return -1;
}

/* Helper to trim leading & trailing whitespace */
static void trim_whitespace(char *str)
{
    if (!str) return;
    char *start = str;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

/* --- Concurrency Protection Command Queue (Section 51 & 52) --- */
typedef enum {
    VISION_CMD_NONE = 0,
    VISION_CMD_BEGIN_ENROLL,
    VISION_CMD_CANCEL_ENROLL,
    VISION_CMD_DELETE_PERSON,
    VISION_CMD_REREGISTER_PERSON,
    VISION_CMD_CLEAR_ALL,
} vision_cmd_type_t;

typedef struct {
    vision_cmd_type_t type;
    uint8_t slot;
    char name[VISION_FACE_NAME_MAX_BYTES + 1];
} vision_cmd_t;

/* Transaction Journal for Power-Loss Recovery (Section 40) */
#define VISION_ENROLL_TXN_MAGIC 0x54584E46 /* "TXNF" */
#define VISION_ENROLL_TXN_PATH  "/storage/face_enroll.txn"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t slot;
    uint8_t accepted_count;
    char name[VISION_FACE_NAME_MAX_BYTES + 1];
    uint16_t feature_ids[VISION_FACE_SAMPLES_PER_PERSON];
    uint32_t crc32;
} vision_enroll_journal_t;

static void enroll_journal_save(const char *name, uint8_t slot, uint8_t count, const uint16_t *feats)
{
    vision_enroll_journal_t j;
    memset(&j, 0, sizeof(j));
    j.magic = VISION_ENROLL_TXN_MAGIC;
    j.version = 1;
    j.slot = slot;
    j.accepted_count = count;
    snprintf(j.name, sizeof(j.name), "%s", name);
    for (int i = 0; i < count && i < VISION_FACE_SAMPLES_PER_PERSON; i++) {
        j.feature_ids[i] = feats[i];
    }
    j.crc32 = calc_crc32((const uint8_t *)&j, offsetof(vision_enroll_journal_t, crc32));
    FILE *f = fopen(VISION_ENROLL_TXN_PATH, "wb");
    if (f) {
        fwrite(&j, sizeof(j), 1, f);
        fflush(f);
        fclose(f);
    }
}

static void enroll_journal_remove(void)
{
    remove(VISION_ENROLL_TXN_PATH);
}

/* Pending Enrollment Transaction (Section 38) */
typedef struct {
    bool active;
    bool is_reregister;
    uint8_t target_slot;
    char name[VISION_FACE_NAME_MAX_BYTES + 1];
    uint8_t accepted_count;
    uint16_t new_feature_ids[VISION_FACE_SAMPLES_PER_PERSON];
    uint16_t old_feature_ids[VISION_FACE_SAMPLES_PER_PERSON];
    uint32_t last_sample_ms;
    vision_enroll_state_t state;
    char prompt[64];
    esp_err_t last_error;
} vision_enroll_txn_t;

static vision_enroll_txn_t s_enroll_txn = {};

#ifndef HOST_TEST
static SemaphoreHandle_t s_lock = NULL;
static SemaphoreHandle_t s_infer_sem = NULL;
static SemaphoreHandle_t s_preview_mutex = NULL;
static QueueHandle_t s_result_queue = NULL;
static QueueHandle_t s_cmd_queue = NULL;
static TaskHandle_t s_capture_task_handle = NULL;
static TaskHandle_t s_infer_task_handle = NULL;
static TaskHandle_t s_health_task_handle = NULL;
static EventGroupHandle_t s_lifecycle_events = NULL;
static volatile uint32_t s_capture_progress_ms = 0;
static volatile uint32_t s_infer_progress_ms = 0;

#define VISION_EVT_CAPTURE_EXITED BIT0
#define VISION_EVT_INFER_EXITED   BIT1
#define VISION_EVT_ALL_EXITED (VISION_EVT_CAPTURE_EXITED | VISION_EVT_INFER_EXITED)

static void release_infer_buffer(int idx)
{
    if (s_preview_mutex) {
        xSemaphoreTake(s_preview_mutex, portMAX_DELAY);
    }
    if (s_infer_idx == idx) {
        s_infer_idx = -1;
    }
    if (s_preview_mutex) {
        xSemaphoreGive(s_preview_mutex);
    }
}

class InferBufferLease {
public:
    explicit InferBufferLease(int idx) : idx_(idx) {}
    ~InferBufferLease() { release_infer_buffer(idx_); }
private:
    int idx_;
};

static HumanFaceDetect *s_face_detect = nullptr;
static HumanFaceRecognizer *s_face_recognizer = nullptr;
static bool s_mfn_loaded = false;

#define MFN_SAFE_LARGEST_BLOCK (900 * 1024)

static size_t log_psram(const char *stage)
{
    size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    size_t simd_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_SIMD);
    ESP_LOGI(TAG, "PSRAM %s: free=%u largest=%u SIMD_largest=%u", stage,
             (unsigned)free_bytes, (unsigned)largest, (unsigned)simd_largest);
    return std::min(largest, simd_largest);
}

static bool mfn_memory_ready(const char *stage)
{
    if (s_mfn_loaded) return true;
    size_t largest = log_psram(stage);
    if (largest >= MFN_SAFE_LARGEST_BLOCK) return true;
    ESP_LOGE(TAG, "MFN load blocked: largest compatible PSRAM block %u < %u",
             (unsigned)largest, (unsigned)MFN_SAFE_LARGEST_BLOCK);
    return false;
}

/* Low-rate, read-only telemetry. It intentionally does not access LVGL. */
static void vision_health_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t int_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        size_t simd_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_SIMD);
        UBaseType_t cap_stack = s_capture_task_handle ? uxTaskGetStackHighWaterMark(s_capture_task_handle) : 0;
        UBaseType_t infer_stack = s_infer_task_handle ? uxTaskGetStackHighWaterMark(s_infer_task_handle) : 0;
        uint32_t cap_age = s_capture_progress_ms && now >= s_capture_progress_ms ? now - s_capture_progress_ms : 0;
        uint32_t infer_age = s_infer_progress_ms && now >= s_infer_progress_ms ? now - s_infer_progress_ms : 0;
        uint32_t pub_age = s_preview_last_publish_ms && now >= s_preview_last_publish_ms ? now - s_preview_last_publish_ms : 0;
        uint32_t consume_age = s_preview_last_consume_ms && now >= s_preview_last_consume_ms ? now - s_preview_last_consume_ms : 0;
        ESP_LOGI(TAG,
                 "[HEALTH] up=%u vstate=%u mode=%u tasks=%u int_free=%u int_min=%u int_largest=%u psram_free=%u psram_min=%u psram_largest=%u simd_largest=%u cap=%u pub=%u consume_calls=%u consumed=%u infer=%u cam_err=%u dirty_miss=%u consume_lock_busy=%u writer_lock_busy=%u publish_lock_busy=%u no_free_buf=%u ready_idx=%d disp_idx=%d infer_idx=%d write_idx=%d dirty=%u cap_stack=%u infer_stack=%u cap_age=%u infer_age=%u pub_age=%u consume_age=%u",
                 (unsigned)now, (unsigned)s_state, (unsigned)s_mode, (unsigned)uxTaskGetNumberOfTasks(),
                 (unsigned)int_free, (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned)int_largest,
                 (unsigned)psram_free, (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)psram_largest, (unsigned)simd_largest, (unsigned)s_diag.frames_captured,
                 (unsigned)s_preview_publish_count, (unsigned)s_preview_consume_call_count,
                 (unsigned)s_preview_consume_ok_count, (unsigned)s_diag.face_inferences, (unsigned)s_diag.camera_errors,
                 (unsigned)s_preview_dirty_miss_count, (unsigned)s_preview_consume_lock_busy_count,
                 (unsigned)s_preview_writer_lock_busy_count, (unsigned)s_preview_publish_lock_busy_count,
                 (unsigned)s_preview_no_free_buffer_count, s_ready_idx, s_disp_idx, s_infer_idx,
                 s_write_idx, (unsigned)s_preview_dirty,
                 (unsigned)cap_stack, (unsigned)infer_stack,
                 (unsigned)cap_age, (unsigned)infer_age, (unsigned)pub_age, (unsigned)consume_age);
        if (s_state == VISION_STATE_RUNNING && s_capture_progress_ms && cap_age > 5000) {
            ESP_LOGE(TAG, "CAPTURE STALL: age=%u ms", (unsigned)cap_age);
        }
        if (s_state == VISION_STATE_RUNNING && s_infer_progress_ms && infer_age > 5000) {
            ESP_LOGE(TAG, "INFERENCE STALL: age=%u ms", (unsigned)infer_age);
        }
        if (s_state == VISION_STATE_RUNNING && cap_age < 2000 && infer_age < 2000 &&
            pub_age < 2000 && s_preview_last_consume_ms && consume_age > 2000) {
            ESP_LOGE(TAG,
                     "PREVIEW CONSUMER STALL: consume_age=%u ready=%d disp=%d infer=%d write=%d dirty=%u pub=%u consumed=%u consume_lock_busy=%u",
                     (unsigned)consume_age, s_ready_idx, s_disp_idx, s_infer_idx,
                     s_write_idx, (unsigned)s_preview_dirty,
                     (unsigned)s_preview_publish_count, (unsigned)s_preview_consume_ok_count,
                     (unsigned)s_preview_consume_lock_busy_count);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

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

        int write_idx = -1;
        bool writer_lock_acquired = false;
        if (s_preview_mutex && xSemaphoreTake(s_preview_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            writer_lock_acquired = true;
            write_idx = find_free_preview_buffer();
            s_write_idx = write_idx;
            xSemaphoreGive(s_preview_mutex);
        } else {
            s_preview_writer_lock_busy_count = s_preview_writer_lock_busy_count + 1;
        }
        if (writer_lock_acquired && write_idx < 0) {
            s_preview_no_free_buffer_count = s_preview_no_free_buffer_count + 1;
        }
        if (write_idx >= 0 && s_preview_buf[write_idx] && frame.data) {
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

            /* Publish only a fully written frame. */
            if (s_preview_mutex) {
                xSemaphoreTake(s_preview_mutex, portMAX_DELAY);
                s_ready_idx = write_idx;
                s_preview_dirty = true;
                s_preview_publish_count = s_preview_publish_count + 1;
                s_preview_last_publish_ms = (uint32_t)(esp_timer_get_time() / 1000);
                s_write_idx = -1;
                xSemaphoreGive(s_preview_mutex);
            } else {
                s_ready_idx = write_idx;
                s_preview_dirty = true;
                s_preview_publish_count = s_preview_publish_count + 1;
                s_preview_last_publish_ms = (uint32_t)(esp_timer_get_time() / 1000);
                s_write_idx = -1;
            }

            /* Trigger inference task if ready */
            if (s_infer_sem && s_infer_running) {
                xSemaphoreGive(s_infer_sem);
            }
        } else if (write_idx >= 0 && s_preview_mutex) {
            xSemaphoreTake(s_preview_mutex, portMAX_DELAY);
            if (s_write_idx == write_idx) s_write_idx = -1;
            xSemaphoreGive(s_preview_mutex);
        }

        /* Immediately release DMA buffer back to driver */
        vision_camera_release(&frame);
        s_capture_progress_ms = (uint32_t)(esp_timer_get_time() / 1000);
        vTaskDelay(1);
    }

    ESP_LOGI(TAG, "Vision capture task exiting");
    xEventGroupSetBits(s_lifecycle_events, VISION_EVT_CAPTURE_EXITED);
    vTaskDelete(NULL);
}

/* Pose guidance prompt generator */
static const char *get_enroll_pose_prompt(uint8_t sample_idx)
{
    switch (sample_idx) {
        case 0: return "正面を向いてください";
        case 1: return "正面を向いてください";
        case 2: return "少し左を向いてください";
        case 3: return "少し右を向いてください";
        case 4: return "正面を向いてください";
        default: return "登録中…";
    }
}

static void vision_inference_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Vision inference task running (Priority 4)");
    uint32_t last_recog_time = 0;
    bool logged_first_inference = false;

    while (s_infer_running) {
        if (!s_infer_sem || xSemaphoreTake(s_infer_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        if (!s_infer_running) {
            break;
        }

        /* Process any asynchronous commands from UI thread (Section 51 & 52) */
        vision_cmd_t cmd;
        while (s_cmd_queue && xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE) {
            switch (cmd.type) {
                case VISION_CMD_BEGIN_ENROLL: {
                    int slot = find_free_person_slot();
                    if (slot < 0 || s_people_file.person_count >= VISION_MAX_PERSONS) {
                        s_enroll_txn.state = VISION_ENROLL_ERROR;
                        snprintf(s_enroll_txn.prompt, sizeof(s_enroll_txn.prompt), "登録数が上限(10名)です");
                        s_enroll_txn.last_error = ESP_ERR_NO_MEM;
                        ESP_LOGW(TAG, "Enrollment rejected: max persons reached");
                        break;
                    }
                    memset(&s_enroll_txn, 0, sizeof(s_enroll_txn));
                    s_enroll_txn.active = true;
                    s_enroll_txn.target_slot = (uint8_t)slot;
                    snprintf(s_enroll_txn.name, sizeof(s_enroll_txn.name), "%s", cmd.name);
                    s_enroll_txn.state = VISION_ENROLL_WAIT_FACE;
                    snprintf(s_enroll_txn.prompt, sizeof(s_enroll_txn.prompt), "%s", get_enroll_pose_prompt(0));
                    ESP_LOGI(TAG, "Starting enrollment for '%s' in slot %d", s_enroll_txn.name, slot);
                    break;
                }
                case VISION_CMD_CANCEL_ENROLL: {
                    if (s_enroll_txn.active) {
                        /* Rollback any partially enrolled feature IDs (Section 39) */
                        if (s_face_recognizer) {
                            for (int i = 0; i < s_enroll_txn.accepted_count; i++) {
                                if (s_enroll_txn.new_feature_ids[i] > 0) {
                                    s_face_recognizer->delete_feat(s_enroll_txn.new_feature_ids[i]);
                                }
                            }
                        }
                        enroll_journal_remove();
                        s_enroll_txn.active = false;
                        s_enroll_txn.state = VISION_ENROLL_CANCELLED;
                        snprintf(s_enroll_txn.prompt, sizeof(s_enroll_txn.prompt), "登録を中止しました");
                        ESP_LOGI(TAG, "Enrollment cancelled & rolled back");
                    }
                    break;
                }
                case VISION_CMD_DELETE_PERSON: {
                    if (cmd.slot < VISION_MAX_PERSONS && s_people_file.persons[cmd.slot].active) {
                        if (s_face_recognizer) {
                            for (int i = 0; i < s_people_file.persons[cmd.slot].feature_count; i++) {
                                s_face_recognizer->delete_feat(s_people_file.persons[cmd.slot].feature_ids[i]);
                            }
                        }
                        s_people_file.persons[cmd.slot].active = false;
                        s_people_file.person_count--;
                        face_people_save_atomic();
                        ESP_LOGI(TAG, "Deleted person slot %d", cmd.slot);
                    }
                    break;
                }
                case VISION_CMD_REREGISTER_PERSON: {
                    if (cmd.slot < VISION_MAX_PERSONS && s_people_file.persons[cmd.slot].active) {
                        memset(&s_enroll_txn, 0, sizeof(s_enroll_txn));
                        s_enroll_txn.active = true;
                        s_enroll_txn.is_reregister = true;
                        s_enroll_txn.target_slot = cmd.slot;
                        snprintf(s_enroll_txn.name, sizeof(s_enroll_txn.name), "%s",
                                 cmd.name[0] != '\0' ? cmd.name : s_people_file.persons[cmd.slot].name);
                        for (int i = 0; i < VISION_FACE_SAMPLES_PER_PERSON; i++) {
                            s_enroll_txn.old_feature_ids[i] = s_people_file.persons[cmd.slot].feature_ids[i];
                        }
                        s_enroll_txn.state = VISION_ENROLL_WAIT_FACE;
                        snprintf(s_enroll_txn.prompt, sizeof(s_enroll_txn.prompt), "%s", get_enroll_pose_prompt(0));
                        ESP_LOGI(TAG, "Starting re-registration for '%s' in slot %d", s_enroll_txn.name, cmd.slot);
                    }
                    break;
                }
                case VISION_CMD_CLEAR_ALL: {
                    if (s_face_recognizer) {
                        s_face_recognizer->clear_all_feats();
                    }
                    enroll_journal_remove();
                    face_people_init_empty();
                    face_people_save_atomic();
                    s_enroll_txn.active = false;
                    s_enroll_txn.state = VISION_ENROLL_IDLE;
                    ESP_LOGI(TAG, "Cleared all people and face database");
                    break;
                }
                default:
                    break;
            }
        }

        int cur_idx = -1;
        if (s_preview_mutex && xSemaphoreTake(s_preview_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            cur_idx = s_ready_idx;
            s_infer_idx = cur_idx;
            xSemaphoreGive(s_preview_mutex);
        }
        if (cur_idx < 0 || !s_preview_buf[cur_idx]) {
            continue;
        }
        InferBufferLease infer_lease(cur_idx);

        if (s_mode == VISION_MODE_FACE) {
            if (!s_face_detect) {
                ESP_LOGI(TAG, "Instantiating HumanFaceDetect model...");
                s_face_detect = new HumanFaceDetect();
            }
            if (!s_face_recognizer && (s_people_file.person_count > 0 || s_enroll_txn.active)) {
                ESP_LOGI(TAG, "Instantiating HumanFaceRecognizer model (MFN_S8_V1)...");
                s_face_recognizer = new HumanFaceRecognizer(VISION_FACE_DB_PATH, HumanFaceFeat::MFN_S8_V1, true);
                /* Recover any interrupted enrollment from previous boot (Section 40) */
                FILE *jf = fopen(VISION_ENROLL_TXN_PATH, "rb");
                if (jf) {
                    vision_enroll_journal_t j;
                    size_t jn = fread(&j, sizeof(j), 1, jf);
                    fclose(jf);
                    if (jn == 1 && j.magic == VISION_ENROLL_TXN_MAGIC && j.version == 1) {
                        uint32_t exp_crc = calc_crc32((const uint8_t *)&j, offsetof(vision_enroll_journal_t, crc32));
                        if (j.crc32 == exp_crc) {
                            ESP_LOGW(TAG, "Recovering power-loss journal for '%s' (%d features)", j.name, j.accepted_count);
                            for (int i = 0; i < j.accepted_count; i++) {
                                if (j.feature_ids[i] > 0) {
                                    s_face_recognizer->delete_feat(j.feature_ids[i]);
                                }
                            }
                        }
                    }
                    enroll_journal_remove();
                }
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
            if (!logged_first_inference) {
                ESP_LOGI(TAG, "First face detection: %u ms, faces=%u", (unsigned)infer_ms, (unsigned)faces.size());
                logged_first_inference = true;
            }

            vision_result_t res;
            memset(&res, 0, sizeof(res));
            res.frame_id = s_diag.frames_captured;
            res.mode = VISION_MODE_FACE;
            res.inference_ms = infer_ms;
            res.count = (uint8_t)std::min(faces.size(), (size_t)VISION_MAX_DETECTIONS);
            res.primary_person_slot = -1;
            res.primary_match_state = VISION_FACE_MATCH_NONE;

            /* Base coordinate mapping */
            int b_idx = 0;
            for (auto &f : faces) {
                if (b_idx >= VISION_MAX_DETECTIONS) break;
                res.boxes[b_idx].x = (int16_t)f.box[0];
                res.boxes[b_idx].y = (int16_t)f.box[1];
                res.boxes[b_idx].w = (int16_t)(f.box[2] - f.box[0]);
                res.boxes[b_idx].h = (int16_t)(f.box[3] - f.box[1]);
                res.boxes[b_idx].confidence = f.score;
                res.boxes[b_idx].class_id = (int16_t)f.category;
                res.boxes[b_idx].person_slot = -1;
                res.boxes[b_idx].match_state = VISION_FACE_MATCH_UNKNOWN;
                snprintf(res.boxes[b_idx].label, sizeof(res.boxes[b_idx].label), "未登録");
                b_idx++;
            }

            /* --- Enrollment State Machine Path (Mutually Exclusive: Section 50) --- */
            if (s_enroll_txn.active) {
                res.enroll_state = s_enroll_txn.state;
                res.enroll_sample_count = s_enroll_txn.accepted_count;
                res.enroll_target_count = VISION_FACE_SAMPLES_PER_PERSON;
                snprintf(res.enroll_name, sizeof(res.enroll_name), "%s", s_enroll_txn.name);
                snprintf(res.enroll_prompt, sizeof(res.enroll_prompt), "%s", s_enroll_txn.prompt);

                uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

                if (faces.empty()) {
                    snprintf(s_enroll_txn.prompt, sizeof(s_enroll_txn.prompt), "顔を映してください");
                } else if (faces.size() > 1) {
                    snprintf(s_enroll_txn.prompt, sizeof(s_enroll_txn.prompt), "一人だけ映してください");
                } else {
                    auto &f = faces.front();
                    int bw = f.box[2] - f.box[0];
                    int bh = f.box[3] - f.box[1];
                    int cx = (f.box[0] + f.box[2]) / 2;
                    int cy = (f.box[1] + f.box[3]) / 2;

                    /* Sample acceptance gates (Section 35) */
                    if (bw < 60 || bh < 60) {
                        snprintf(s_enroll_txn.prompt, sizeof(s_enroll_txn.prompt), "もう少し近づいてください");
                    } else if (cx < 64 || cx > (VISION_PREVIEW_WIDTH - 64) || cy < 48 || cy > (VISION_PREVIEW_HEIGHT - 48)) {
                        snprintf(s_enroll_txn.prompt, sizeof(s_enroll_txn.prompt), "顔を中央に合わせてください");
                    } else if (f.score < 0.70f) {
                        snprintf(s_enroll_txn.prompt, sizeof(s_enroll_txn.prompt), "%s", get_enroll_pose_prompt(s_enroll_txn.accepted_count));
                    } else if (now_ms - s_enroll_txn.last_sample_ms >= 350) {
                        /* Accepted sample! Run feature extraction & enroll */
                        if (!mfn_memory_ready("before first enrollment/MFN load")) {
                            s_enroll_txn.active = false;
                            s_enroll_txn.state = VISION_ENROLL_ERROR;
                            s_enroll_txn.last_error = ESP_ERR_NO_MEM;
                            snprintf(s_enroll_txn.prompt, sizeof(s_enroll_txn.prompt), "認識用メモリが不足しています");
                            continue;
                        }
                        std::list<dl::detect::result_t> single_face;
                        single_face.push_back(f);

                        esp_err_t enr_err = s_face_recognizer->enroll(img, single_face);
                        if (!s_mfn_loaded) {
                            s_mfn_loaded = (enr_err == ESP_OK);
                            log_psram("after first enrollment/MFN load");
                        }
                        if (enr_err == ESP_OK) {
                            /* Preferred B: query top match to retrieve exact assigned feature ID */
                            uint16_t feat_id = 0;
                            auto matches = s_face_recognizer->recognize(img, single_face);
                            if (!matches.empty() && matches.front().id > 0) {
                                feat_id = matches.front().id;
                            } else {
                                feat_id = (uint16_t)s_face_recognizer->get_num_feats();
                            }

                            s_enroll_txn.new_feature_ids[s_enroll_txn.accepted_count] = feat_id;
                            s_enroll_txn.accepted_count++;
                            s_enroll_txn.last_sample_ms = now_ms;
                            s_enroll_txn.state = VISION_ENROLL_SAMPLING;
                            enroll_journal_save(s_enroll_txn.name, s_enroll_txn.target_slot,
                                                s_enroll_txn.accepted_count, s_enroll_txn.new_feature_ids);

                            ESP_LOGI(TAG, "Enroll sample %d/5 accepted (feature_id=%u)",
                                     s_enroll_txn.accepted_count, feat_id);

                            if (s_enroll_txn.accepted_count >= VISION_FACE_SAMPLES_PER_PERSON) {
                                /* All 5 samples collected: Commit metadata (Section 41) */
                                s_enroll_txn.state = VISION_ENROLL_COMMITTING;
                                uint8_t slot = s_enroll_txn.target_slot;

                                if (s_enroll_txn.is_reregister) {
                                    /* Delete old features upon successful re-enrollment */
                                    for (int i = 0; i < VISION_FACE_SAMPLES_PER_PERSON; i++) {
                                        if (s_enroll_txn.old_feature_ids[i] > 0) {
                                            s_face_recognizer->delete_feat(s_enroll_txn.old_feature_ids[i]);
                                        }
                                    }
                                } else {
                                    s_people_file.person_count++;
                                }

                                s_people_file.persons[slot].active = true;
                                s_people_file.persons[slot].slot = slot;
                                snprintf(s_people_file.persons[slot].name, sizeof(s_people_file.persons[slot].name), "%s", s_enroll_txn.name);
                                s_people_file.persons[slot].feature_count = VISION_FACE_SAMPLES_PER_PERSON;
                                for (int i = 0; i < VISION_FACE_SAMPLES_PER_PERSON; i++) {
                                    s_people_file.persons[slot].feature_ids[i] = s_enroll_txn.new_feature_ids[i];
                                }

                                esp_err_t save_err = face_people_save_atomic();
                                if (save_err == ESP_OK) {
                                    enroll_journal_remove();
                                    s_enroll_txn.state = VISION_ENROLL_SUCCESS;
                                    snprintf(s_enroll_txn.prompt, sizeof(s_enroll_txn.prompt), "登録しました");
                                    ESP_LOGI(TAG, "Successfully committed person '%s' (slot=%d)",
                                             s_enroll_txn.name, slot);
                                } else {
                                    /* Rollback on metadata save failure */
                                    for (int i = 0; i < VISION_FACE_SAMPLES_PER_PERSON; i++) {
                                        s_face_recognizer->delete_feat(s_enroll_txn.new_feature_ids[i]);
                                    }
                                    enroll_journal_remove();
                                    if (!s_enroll_txn.is_reregister) {
                                        s_people_file.persons[slot].active = false;
                                        s_people_file.person_count--;
                                    }
                                    s_enroll_txn.state = VISION_ENROLL_ERROR;
                                    snprintf(s_enroll_txn.prompt, sizeof(s_enroll_txn.prompt), "登録データを保存できません");
                                }
                                s_enroll_txn.active = false;
                            } else {
                                snprintf(s_enroll_txn.prompt, sizeof(s_enroll_txn.prompt), "%s (%d/5)",
                                         get_enroll_pose_prompt(s_enroll_txn.accepted_count),
                                         s_enroll_txn.accepted_count);
                            }
                        } else {
                            ESP_LOGW(TAG, "Enroll feature extraction failed");
                        }
                    }
                }
                snprintf(res.enroll_prompt, sizeof(res.enroll_prompt), "%s", s_enroll_txn.prompt);
                res.enroll_state = s_enroll_txn.state;
                res.enroll_sample_count = s_enroll_txn.accepted_count;
            } else {
                /* --- Normal Face Recognition Pipeline (Sections 18 & 19) --- */
                uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
                /* DB-Empty Fast Path (Section 18) */
                if (s_people_file.person_count == 0 ||
                    !s_face_recognizer || s_face_recognizer->get_num_feats() == 0) {
                    for (size_t i = 0; i < faces.size() && i < VISION_MAX_DETECTIONS; i++) {
                        res.boxes[i].match_state = VISION_FACE_MATCH_UNKNOWN;
                        res.boxes[i].person_slot = -1;
                        res.boxes[i].similarity = 0.0f;
                        snprintf(res.boxes[i].label, sizeof(res.boxes[i].label), "未登録");
                    }
                    if (!faces.empty()) {
                        res.primary_match_state = VISION_FACE_MATCH_UNKNOWN;
                        res.primary_person_slot = -1;
                        snprintf(res.primary_name, sizeof(res.primary_name), "未登録の人物");
                    }
                } else if (!faces.empty() && (now_ms - last_recog_time >= VISION_RECOGNITION_MIN_INTERVAL_MS)) {
                    /* Execute per-face multi-face recognition (Section 5 & 19) */
                    int64_t tr0 = esp_timer_get_time();
                    last_recog_time = now_ms;

                    float best_known_sim = 0.0f;
                    int best_known_idx = -1;

                    size_t f_idx = 0;
                    for (auto &f : faces) {
                        if (f_idx >= VISION_MAX_DETECTIONS) break;
                        if (f.keypoint.size() == 10) {
                            if (!mfn_memory_ready("before first recognition/MFN load")) break;
                            std::list<dl::detect::result_t> single_face;
                            single_face.push_back(f);
                            auto matches = s_face_recognizer->recognize(img, single_face);
                            if (!s_mfn_loaded) {
                                s_mfn_loaded = true;
                                log_psram("after first recognition/MFN load");
                            }

                            if (!matches.empty()) {
                                auto &top = matches.front();
                                res.boxes[f_idx].matched_feature_id = top.id;
                                res.boxes[f_idx].similarity = top.similarity;

                                const vision_person_record_t *p = find_person_by_feature_id(top.id);
                                if (p && top.similarity >= s_match_threshold) {
                                    res.boxes[f_idx].match_state = VISION_FACE_MATCH_KNOWN;
                                    res.boxes[f_idx].person_slot = p->slot;
                                    snprintf(res.boxes[f_idx].name, sizeof(res.boxes[f_idx].name), "%s", p->name);
                                    int pct = (int)lroundf(std::clamp(top.similarity, 0.0f, 1.0f) * 100.0f);
                                    snprintf(res.boxes[f_idx].label, sizeof(res.boxes[f_idx].label), "%s %d%%", p->name, pct);

                                    if (top.similarity > best_known_sim) {
                                        best_known_sim = top.similarity;
                                        best_known_idx = (int)f_idx;
                                    }
                                    s_diag.recognition_known++;
                                } else {
                                    res.boxes[f_idx].match_state = VISION_FACE_MATCH_UNKNOWN;
                                    snprintf(res.boxes[f_idx].label, sizeof(res.boxes[f_idx].label), "未登録");
                                    s_diag.recognition_unknown++;
                                }
                            } else {
                                res.boxes[f_idx].match_state = VISION_FACE_MATCH_UNKNOWN;
                                snprintf(res.boxes[f_idx].label, sizeof(res.boxes[f_idx].label), "未登録");
                                s_diag.recognition_unknown++;
                            }
                        }
                        f_idx++;
                    }

                    int64_t tr1 = esp_timer_get_time();
                    res.recognition_ms = (uint32_t)((tr1 - tr0) / 1000);
                    s_diag.face_recognitions++;
                    s_diag.avg_recognition_ms = (s_diag.avg_recognition_ms == 0)
                        ? res.recognition_ms
                        : (s_diag.avg_recognition_ms * 3 + res.recognition_ms) / 4;

                    /* Select Primary Face for Side HUD (Section 20) */
                    if (best_known_idx >= 0) {
                        res.primary_match_state = VISION_FACE_MATCH_KNOWN;
                        res.primary_person_slot = res.boxes[best_known_idx].person_slot;
                        res.primary_feature_id = res.boxes[best_known_idx].matched_feature_id;
                        res.primary_similarity = res.boxes[best_known_idx].similarity;
                        snprintf(res.primary_name, sizeof(res.primary_name), "%s", res.boxes[best_known_idx].name);
                    } else if (!faces.empty()) {
                        res.primary_match_state = VISION_FACE_MATCH_UNKNOWN;
                        res.primary_person_slot = -1;
                        snprintf(res.primary_name, sizeof(res.primary_name), "未登録の人物");
                    }
                }
            }

            s_diag.face_inferences++;
            s_diag.avg_face_ms = (s_diag.avg_face_ms == 0) ? infer_ms : (s_diag.avg_face_ms * 3 + infer_ms) / 4;

            /* Push latest result to result queue (discard oldest if full) */
            if (s_result_queue) {
                if (uxQueueSpacesAvailable(s_result_queue) == 0) {
                    vision_result_t dummy;
                    xQueueReceive(s_result_queue, &dummy, 0);
                }
                xQueueSend(s_result_queue, &res, 0);
            }
        }
        s_infer_progress_ms = (uint32_t)(esp_timer_get_time() / 1000);
        vTaskDelay(1);
    }

    ESP_LOGI(TAG, "Vision inference task exiting");
    xEventGroupSetBits(s_lifecycle_events, VISION_EVT_INFER_EXITED);
    vTaskDelete(NULL);
}
#else
static vision_result_t s_host_result_slot;
static bool s_host_result_valid = false;
static uint8_t s_host_preview_buf[PREVIEW_FRAME_SIZE];

static void release_infer_buffer(int idx)
{
    if (s_infer_idx == idx) s_infer_idx = -1;
}

class InferBufferLease {
public:
    explicit InferBufferLease(int idx) : idx_(idx) {}
    ~InferBufferLease() { release_infer_buffer(idx_); }
private:
    int idx_;
};
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

    s_lifecycle_events = xEventGroupCreate();
    if (!s_lifecycle_events) {
        vSemaphoreDelete(s_infer_sem);
        s_infer_sem = NULL;
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

    s_cmd_queue = xQueueCreate(8, sizeof(vision_cmd_t));
    if (!s_cmd_queue) {
        ESP_LOGE(TAG, "Failed to create command queue");
        vSemaphoreDelete(s_result_queue);
        s_result_queue = NULL;
        vSemaphoreDelete(s_infer_sem);
        s_infer_sem = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_preview_mutex = xSemaphoreCreateMutex();
    if (!s_preview_mutex) {
        ESP_LOGE(TAG, "Failed to create preview mutex");
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
        vSemaphoreDelete(s_result_queue);
        s_result_queue = NULL;
        vSemaphoreDelete(s_infer_sem);
        s_infer_sem = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* Allocate ping-pong preview buffers in PSRAM */
    for (int i = 0; i < 3; i++) {
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

    /* Load persistent people metadata */
    face_people_load();

    s_disp_idx = 0;
    s_ready_idx = 0;
    s_infer_idx = -1;
    s_write_idx = -1;
    s_preview_dirty = false;
    s_state = VISION_STATE_OFF;
    s_mode = VISION_MODE_FACE;
    memset(&s_diag, 0, sizeof(s_diag));
#ifndef HOST_TEST
    s_capture_progress_ms = 0;
    s_infer_progress_ms = 0;
    if (xTaskCreate(vision_health_task, "vis_health", 3072, NULL, 1, &s_health_task_handle) != pdPASS) {
        ESP_LOGW(TAG, "Health telemetry task unavailable");
    }
#endif
    s_inited = true;

    ESP_LOGI(TAG, "Vision service initialized (Preview: %dx%d, People: %d)",
             VISION_PREVIEW_WIDTH, VISION_PREVIEW_HEIGHT, s_people_file.person_count);
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
    if (s_state == VISION_STATE_STOPPING || s_state == VISION_STATE_ERROR
#ifndef HOST_TEST
        || s_capture_task_handle != NULL || s_infer_task_handle != NULL
#endif
    ) {
#ifndef HOST_TEST
        ESP_LOGE(TAG, "Refusing Vision start: state=%d capture=%p infer=%p",
                 s_state, s_capture_task_handle, s_infer_task_handle);
#else
        ESP_LOGE(TAG, "Refusing Vision start: state=%d", s_state);
#endif
#ifndef HOST_TEST
        xSemaphoreGive(s_lock);
#endif
        return ESP_ERR_INVALID_STATE;
    }

    s_state = VISION_STATE_STARTING;
    s_write_idx = -1;
    s_preview_last_publish_ms = 0;
    s_preview_last_consume_ms = 0;
    ESP_LOGI(TAG, "Starting vision service in mode %d", s_mode);

#ifndef HOST_TEST
    xEventGroupClearBits(s_lifecycle_events, VISION_EVT_ALL_EXITED);
    log_psram("before Vision start");
    esp_err_t cam_err = vision_camera_start();
    if (cam_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start camera hardware: %s", esp_err_to_name(cam_err));
        s_state = VISION_STATE_ERROR;
        xSemaphoreGive(s_lock);
        return cam_err;
    }
    log_psram("after Camera start");

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
        12288, /* 12KB stack for model inference */
        NULL,
        4, /* Priority 4: below capture 5, below LVGL 6 */
        &s_infer_task_handle
    );
    if (infer_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create inference task");
        s_infer_running = false;
        s_capture_running = false;
        EventBits_t bits = xEventGroupWaitBits(s_lifecycle_events,
                                                VISION_EVT_CAPTURE_EXITED,
                                                pdFALSE, pdTRUE, pdMS_TO_TICKS(2000));
        if (bits & VISION_EVT_CAPTURE_EXITED) {
            s_capture_task_handle = NULL;
        }
        vision_camera_stop();
        s_state = VISION_STATE_ERROR;
        xSemaphoreGive(s_lock);
        return ESP_FAIL;
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
    if (!s_inited || s_state == VISION_STATE_OFF) {
        return;
    }

    ESP_LOGI(TAG, "Stopping vision service...");

#ifndef HOST_TEST
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) != pdTRUE) return;
    s_state = VISION_STATE_STOPPING;
    s_capture_running = false;
    s_infer_running = false;
    if (s_infer_sem) xSemaphoreGive(s_infer_sem);
    xSemaphoreGive(s_lock);

    EventBits_t need = 0;
    if (s_capture_task_handle) need |= VISION_EVT_CAPTURE_EXITED;
    if (s_infer_task_handle) need |= VISION_EVT_INFER_EXITED;
    EventBits_t bits = xEventGroupWaitBits(s_lifecycle_events, need, pdFALSE,
                                            pdTRUE, pdMS_TO_TICKS(3000));
    if ((bits & need) != need) {
        ESP_LOGE(TAG, "Vision stop timeout: exited=0x%lx need=0x%lx",
                 (unsigned long)bits, (unsigned long)need);
        s_state = VISION_STATE_ERROR;
        return;
    }

    vision_camera_stop();
    s_capture_task_handle = NULL;
    s_infer_task_handle = NULL;
    s_infer_idx = -1;
    s_write_idx = -1;
    s_preview_dirty = false;
    if (s_result_queue) xQueueReset(s_result_queue);
    if (s_cmd_queue) xQueueReset(s_cmd_queue);
    s_state = VISION_STATE_OFF;
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
    s_preview_consume_call_count = s_preview_consume_call_count + 1;
    if (!out_data || !out_w || !out_h || !s_inited || s_state != VISION_STATE_RUNNING) {
        return false;
    }

    if (!s_preview_dirty) {
        s_preview_dirty_miss_count = s_preview_dirty_miss_count + 1;
        return false;
    }

#ifndef HOST_TEST
    if (s_preview_mutex) {
        if (xSemaphoreTake(s_preview_mutex, 0) != pdTRUE) {
            s_preview_consume_lock_busy_count = s_preview_consume_lock_busy_count + 1;
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
    s_preview_consume_ok_count = s_preview_consume_ok_count + 1;
#ifndef HOST_TEST
    s_preview_last_consume_ms = (uint32_t)(esp_timer_get_time() / 1000);
#endif

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

extern "C" esp_err_t vision_service_begin_enrollment(const char *utf8_name)
{
    if (!utf8_name) {
        return ESP_ERR_INVALID_ARG;
    }

    char clean_name[VISION_FACE_NAME_MAX_BYTES + 1];
    snprintf(clean_name, sizeof(clean_name), "%s", utf8_name);
    trim_whitespace(clean_name);

    if (clean_name[0] == '\0') {
        ESP_LOGW(TAG, "Enrollment name empty after trim");
        return ESP_ERR_INVALID_ARG;
    }

    if (find_person_by_name(clean_name) != NULL) {
        ESP_LOGW(TAG, "Duplicate name '%s' already registered", clean_name);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_people_file.person_count >= VISION_MAX_PERSONS) {
        ESP_LOGW(TAG, "Cannot enroll: Maximum 10 persons reached");
        return ESP_ERR_NO_MEM;
    }

#ifndef HOST_TEST
    if (!s_cmd_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    vision_cmd_t cmd = {};
    cmd.type = VISION_CMD_BEGIN_ENROLL;
    snprintf(cmd.name, sizeof(cmd.name), "%s", clean_name);
    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
#else
    s_enroll_txn.active = true;
    s_enroll_txn.target_slot = (uint8_t)find_free_person_slot();
    snprintf(s_enroll_txn.name, sizeof(s_enroll_txn.name), "%s", clean_name);
    s_enroll_txn.state = VISION_ENROLL_WAIT_FACE;
#endif
    return ESP_OK;
}

extern "C" esp_err_t vision_service_cancel_enrollment(void)
{
#ifndef HOST_TEST
    if (!s_cmd_queue) return ESP_ERR_INVALID_STATE;
    vision_cmd_t cmd = {};
    cmd.type = VISION_CMD_CANCEL_ENROLL;
    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
#else
    s_enroll_txn.active = false;
    s_enroll_txn.state = VISION_ENROLL_CANCELLED;
#endif
    return ESP_OK;
}

extern "C" esp_err_t vision_service_delete_person(uint8_t person_slot)
{
    if (person_slot >= VISION_MAX_PERSONS) {
        return ESP_ERR_INVALID_ARG;
    }

#ifndef HOST_TEST
    if (!s_cmd_queue) return ESP_ERR_INVALID_STATE;
    vision_cmd_t cmd = {};
    cmd.type = VISION_CMD_DELETE_PERSON;
    cmd.slot = person_slot;
    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
#else
    if (s_people_file.persons[person_slot].active) {
        s_people_file.persons[person_slot].active = false;
        s_people_file.person_count--;
    }
#endif
    return ESP_OK;
}

extern "C" esp_err_t vision_service_reregister_person(uint8_t person_slot, const char *utf8_name)
{
    if (person_slot >= VISION_MAX_PERSONS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_people_file.persons[person_slot].active) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef HOST_TEST
    if (!s_cmd_queue) return ESP_ERR_INVALID_STATE;
    vision_cmd_t cmd = {};
    cmd.type = VISION_CMD_REREGISTER_PERSON;
    cmd.slot = person_slot;
    if (utf8_name && utf8_name[0] != '\0') {
        snprintf(cmd.name, sizeof(cmd.name), "%s", utf8_name);
        trim_whitespace(cmd.name);
    } else {
        snprintf(cmd.name, sizeof(cmd.name), "%s", s_people_file.persons[person_slot].name);
    }
    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
#else
    s_enroll_txn.active = true;
    s_enroll_txn.is_reregister = true;
    s_enroll_txn.target_slot = person_slot;
    snprintf(s_enroll_txn.name, sizeof(s_enroll_txn.name), "%s",
             (utf8_name && utf8_name[0] != '\0') ? utf8_name : s_people_file.persons[person_slot].name);
    s_enroll_txn.state = VISION_ENROLL_WAIT_FACE;
#endif
    return ESP_OK;
}

extern "C" esp_err_t vision_service_clear_all_people(void)
{
#ifndef HOST_TEST
    if (!s_cmd_queue) return ESP_ERR_INVALID_STATE;
    vision_cmd_t cmd = {};
    cmd.type = VISION_CMD_CLEAR_ALL;
    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
#else
    face_people_init_empty();
#endif
    return ESP_OK;
}

extern "C" int vision_service_get_enrolled_count(void)
{
    return s_people_file.person_count;
}

extern "C" esp_err_t vision_service_get_people_summary(vision_person_summary_t *out, size_t capacity, size_t *out_count)
{
    if (!out || !out_count) return ESP_ERR_INVALID_ARG;
    size_t count = 0;
    for (int i = 0; i < VISION_MAX_PERSONS && count < capacity; i++) {
        if (s_people_file.persons[i].active) {
            out[count].slot = s_people_file.persons[i].slot;
            snprintf(out[count].name, sizeof(out[count].name), "%s", s_people_file.persons[i].name);
            out[count].feature_count = s_people_file.persons[i].feature_count;
            count++;
        }
    }
    *out_count = count;
    return ESP_OK;
}

extern "C" esp_err_t vision_service_enroll_face(void)
{
    char default_name[32];
    snprintf(default_name, sizeof(default_name), "Face %02d", s_people_file.person_count + 1);
    return vision_service_begin_enrollment(default_name);
}

extern "C" esp_err_t vision_service_delete_last_face(void)
{
    for (int i = VISION_MAX_PERSONS - 1; i >= 0; i--) {
        if (s_people_file.persons[i].active) {
            return vision_service_delete_person((uint8_t)i);
        }
    }
    return ESP_OK;
}

extern "C" esp_err_t vision_service_clear_faces(void)
{
    return vision_service_clear_all_people();
}

extern "C" void vision_service_get_diag(vision_diag_t *diag)
{
    if (!diag) return;
    *diag = s_diag;
}
