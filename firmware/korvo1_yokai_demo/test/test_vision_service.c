/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define HOST_TEST 1
#include "../main/vision_service.h"
#include "../main/vision_service.cpp"

int main(void)
{
    printf("[TEST] Testing vision_service lifecycle & state transitions...\n");

    /* 1. Init */
    assert(vision_service_init() == ESP_OK);
    assert(vision_service_get_state() == VISION_STATE_OFF);
    assert(vision_service_get_mode() == VISION_MODE_FACE);

    /* 2. Start */
    assert(vision_service_start() == ESP_OK);
    assert(vision_service_get_state() == VISION_STATE_RUNNING);

    /* 3. Mode Switch */
    assert(vision_service_set_mode(VISION_MODE_OBJECT) == ESP_OK);
    assert(vision_service_get_mode() == VISION_MODE_OBJECT);
    assert(vision_service_set_mode(VISION_MODE_FACE) == ESP_OK);
    assert(vision_service_get_mode() == VISION_MODE_FACE);

    /* 4. Poll when empty */
    vision_result_t res;
    assert(vision_service_poll_result(&res) == false);

    /* 5. Mock publish & poll */
    s_host_result_slot.frame_id = 42;
    s_host_result_slot.mode = VISION_MODE_FACE;
    s_host_result_slot.count = 1;
    s_host_result_slot.face_known = true;
    s_host_result_slot.face_id = 1;
    s_host_result_slot.face_similarity = 0.95f;
    s_host_result_valid = true;

    assert(vision_service_poll_result(&res) == true);
    assert(res.frame_id == 42);
    assert(res.face_known == true);
    assert(res.face_id == 1);
    assert(vision_service_poll_result(&res) == false);

    /* 6. Enroll & DB APIs */
    assert(vision_service_enroll_face() == ESP_OK);
    assert(vision_service_delete_last_face() == ESP_OK);
    assert(vision_service_clear_faces() == ESP_OK);

    /* 7. Stop */
    vision_service_stop();
    assert(vision_service_get_state() == VISION_STATE_OFF);
    assert(vision_service_poll_result(&res) == false);

    /* 8. Diagnostics */
    vision_diag_t diag;
    vision_service_get_diag(&diag);
    assert(diag.frames_captured == 0);

    printf("[TEST] All vision_service tests passed successfully!\n");
    return 0;
}
