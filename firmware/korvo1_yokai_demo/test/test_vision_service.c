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
    printf("[TEST] Testing vision_service Phase 5 recognition & Phase 6 metadata...\n");

    /* 1. Init & Lifecycle */
    assert(vision_service_init() == ESP_OK);
    assert(vision_service_get_state() == VISION_STATE_OFF);
    assert(vision_service_get_mode() == VISION_MODE_FACE);

    /* 2. Metadata empty state */
    assert(vision_service_get_enrolled_count() == 0);
    assert(s_people_file.magic == VISION_PEOPLE_META_MAGIC);
    assert(s_people_file.version == VISION_PEOPLE_META_VERSION);
    assert(face_people_validate(&s_people_file) == true);

    /* 3. CRC32 Check */
    uint32_t orig_crc = s_people_file.crc32;
    s_people_file.crc32 = 0x12345678;
    assert(face_people_validate(&s_people_file) == false);
    s_people_file.crc32 = orig_crc;
    assert(face_people_validate(&s_people_file) == true);

    /* 4. Name validation rules (Section 32 & 55) */
    assert(vision_service_begin_enrollment(NULL) == ESP_ERR_INVALID_ARG);
    assert(vision_service_begin_enrollment("") == ESP_ERR_INVALID_ARG);
    assert(vision_service_begin_enrollment("   ") == ESP_ERR_INVALID_ARG);

    /* Valid enrollment begin */
    assert(vision_service_begin_enrollment("  Alice  ") == ESP_OK);
    assert(strcmp(s_enroll_txn.name, "Alice") == 0);
    assert(s_enroll_txn.active == true);
    assert(s_enroll_txn.state == VISION_ENROLL_WAIT_FACE);

    /* Cancel enrollment */
    assert(vision_service_cancel_enrollment() == ESP_OK);
    assert(s_enroll_txn.active == false);
    assert(s_enroll_txn.state == VISION_ENROLL_CANCELLED);

    /* 5. Simulate 1 committed person with 5 feature IDs */
    s_people_file.persons[0].active = true;
    s_people_file.persons[0].slot = 0;
    snprintf(s_people_file.persons[0].name, sizeof(s_people_file.persons[0].name), "%s", "Alice");
    s_people_file.persons[0].feature_count = 5;
    s_people_file.persons[0].feature_ids[0] = 101;
    s_people_file.persons[0].feature_ids[1] = 102;
    s_people_file.persons[0].feature_ids[2] = 103;
    s_people_file.persons[0].feature_ids[3] = 104;
    s_people_file.persons[0].feature_ids[4] = 105;
    s_people_file.person_count = 1;
    s_people_file.crc32 = calc_crc32((const uint8_t *)&s_people_file,
                                     offsetof(vision_people_file_t, crc32));
    assert(face_people_validate(&s_people_file) == true);

    /* Duplicate name check */
    assert(vision_service_begin_enrollment("Alice") == ESP_ERR_INVALID_STATE);

    /* 6. Feature ID to Person lookup (Section 43) */
    const vision_person_record_t *p1 = find_person_by_feature_id(103);
    assert(p1 != NULL);
    assert(p1->slot == 0);
    assert(strcmp(p1->name, "Alice") == 0);

    /* Unknown feature ID */
    const vision_person_record_t *p_unk = find_person_by_feature_id(999);
    assert(p_unk == NULL);

    /* 7. People summary API (Section 29) */
    vision_person_summary_t summaries[VISION_MAX_PERSONS];
    size_t count = 0;
    assert(vision_service_get_people_summary(summaries, VISION_MAX_PERSONS, &count) == ESP_OK);
    assert(count == 1);
    assert(summaries[0].slot == 0);
    assert(strcmp(summaries[0].name, "Alice") == 0);
    assert(summaries[0].feature_count == 5);

    /* 8. Delete & Re-register person (Section 45) */
    assert(vision_service_reregister_person(0, "Alice2") == ESP_OK);
    assert(s_enroll_txn.active == true);
    assert(s_enroll_txn.is_reregister == true);
    assert(s_enroll_txn.target_slot == 0);
    assert(strcmp(s_enroll_txn.name, "Alice2") == 0);
    s_enroll_txn.active = false;

    assert(vision_service_delete_person(0) == ESP_OK);
    assert(s_people_file.person_count == 0);
    assert(s_people_file.persons[0].active == false);
    assert(find_person_by_feature_id(103) == NULL);

    /* 9. Max 10 persons cap check (Section 46) */
    for (int i = 0; i < 10; i++) {
        s_people_file.persons[i].active = true;
        s_people_file.persons[i].slot = i;
        snprintf(s_people_file.persons[i].name, sizeof(s_people_file.persons[i].name), "User%02d", i);
        s_people_file.persons[i].feature_count = 5;
    }
    s_people_file.person_count = 10;
    assert(vision_service_begin_enrollment("User11") == ESP_ERR_NO_MEM);

    /* 10. Journal Save / Remove */
    uint16_t sample_feats[5] = {1, 2, 3, 0, 0};
    enroll_journal_save("Bob", 2, 3, sample_feats);
    enroll_journal_remove();

    /* Reset for clean state */
    face_people_init_empty();

    /* 11. Start and Stop */
    assert(vision_service_start() == ESP_OK);
    assert(vision_service_get_state() == VISION_STATE_RUNNING);

    /* Inference ownership is released even on an early scope exit. */
    s_infer_idx = 1;
    {
        InferBufferLease lease(1);
    }
    assert(s_infer_idx == -1);

    /* A reserved writer is excluded until publication releases it. */
    s_disp_idx = 0;
    s_infer_idx = 1;
    s_write_idx = 2;
    assert(find_free_preview_buffer() == -1);
    s_write_idx = -1;
    assert(find_free_preview_buffer() == 2);

    vision_service_stop();
    assert(vision_service_get_state() == VISION_STATE_OFF);

    /* A faulted lifecycle must not create another task set. */
    s_state = VISION_STATE_ERROR;
    assert(vision_service_start() == ESP_ERR_INVALID_STATE);
    s_state = VISION_STATE_OFF;

    printf("[TEST] All Phase 5 & 6 unit tests passed successfully!\n");
    return 0;
}
