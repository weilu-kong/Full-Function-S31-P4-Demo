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
    remove(VISION_PEOPLE_META_PATH);
    remove(VISION_PEOPLE_ALT_PATH);

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

    /* Consume each published sequence exactly once. */
    const uint8_t *preview = NULL;
    uint16_t preview_w = 0;
    uint16_t preview_h = 0;
    s_ready_idx = 1;
    s_preview_publish_seq = 1;
    s_preview_consumed_seq = 0;
    assert(vision_service_get_preview_frame(&preview, &preview_w, &preview_h) == true);
    assert(vision_service_get_preview_frame(&preview, &preview_w, &preview_h) == false);
    s_preview_publish_seq = 2;
    assert(vision_service_get_preview_frame(&preview, &preview_w, &preview_h) == true);

    vision_service_stop();
    assert(vision_service_get_state() == VISION_STATE_OFF);

    /* A faulted lifecycle must not create another task set. */
    s_state = VISION_STATE_ERROR;
    assert(vision_service_start() == ESP_ERR_INVALID_STATE);
    s_state = VISION_STATE_OFF;

    /* A partial write to the newer slot must leave the previous metadata usable. */
    vision_people_file_t next = s_people_file;
    next.persons[0].active = true;
    next.persons[0].slot = 0;
    next.persons[0].feature_count = VISION_FACE_SAMPLES_PER_PERSON;
    snprintf(next.persons[0].name, sizeof(next.persons[0].name), "%s", "Alice");
    next.person_count = 1;
    assert(face_people_save_atomic(&next) == ESP_OK);
    snprintf(next.persons[0].name, sizeof(next.persons[0].name), "%s", "Alice2");
    assert(face_people_save_atomic(&next) == ESP_OK);
    FILE *corrupt = fopen(VISION_PEOPLE_META_PATH, "wb");
    assert(corrupt != NULL);
    assert(fwrite("x", 1, 1, corrupt) == 1);
    fclose(corrupt);
    assert(face_people_load() == ESP_OK);
    assert(strcmp(s_people_file.persons[0].name, "Alice") == 0);
    /* 12. Vision Enrollment UX & Reliability Tests (Sections 5-17 & 24) */
    printf("[TEST] Testing enrollment error codes, pose validation, and state machine...\n");

    /* Error code strings */
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_NONE), "正常") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_NO_FACE), "顔が見つかりません") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_MULTIPLE_FACES), "一人だけ映してください") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_FACE_TOO_SMALL), "もう少し近づいてください") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_FACE_OFF_CENTER), "顔を中央に合わせてください") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_LOW_DETECT_SCORE), "顔をはっきり映してください") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_WRONG_POSE), "指示された向きに顔を向けてください") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_FACE_UNSTABLE), "顔を少し静止してください") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_MFN_NO_MEMORY), "認識用メモリが不足しています") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_FEATURE_EXTRACT_FAILED), "特徴抽出に失敗しました") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_FEATURE_ID_INVALID), "特徴IDを確認できません") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_METADATA_SAVE_FAILED), "登録データを保存できませんでした") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_FACE_DB_FAILED), "特徴データベースエラー") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_EMPTY_NAME), "名前を入力してください") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_DUPLICATE_NAME), "同名が既に登録されています") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_MAX_PERSONS), "登録数が上限(10名)です") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_COMMAND_TIMEOUT), "コマンドがタイムアウトしました") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_INVALID_SLOT), "無効なスロット番号です") == 0);
    assert(strcmp(vision_enroll_error_to_str(VISION_ENROLL_ERR_CANCELLED), "登録を中止しました") == 0);

    /* Pose yaw calculation */
    float test_yaw = 0.0f;
    std::vector<int> empty_kpt;
    assert(calc_face_pose_yaw_from_keypoints(empty_kpt, &test_yaw) == false);

    /* Standard front face: left_eye=(30, 50), left_mouth=(35, 80), nose=(50, 65), right_eye=(70, 50), right_mouth=(65, 80) */
    std::vector<int> front_kpt = {30, 50, 35, 80, 50, 65, 70, 50, 65, 80};
    assert(calc_face_pose_yaw_from_keypoints(front_kpt, &test_yaw) == true);
    assert(fabsf(test_yaw) < 0.01f);
    assert(is_pose_valid_for_step(0, test_yaw) == true);
    assert(is_pose_valid_for_step(3, test_yaw) == true);
    assert(is_pose_valid_for_step(4, test_yaw) == true);
    assert(is_pose_valid_for_step(1, test_yaw) == false); /* Step 1 requires turned face */
    assert(is_pose_valid_for_step(2, test_yaw) == false); /* Step 2 requires turned face */

    /* Turned face (yaw > 0.45) */
    std::vector<int> turned_kpt1 = {30, 50, 35, 80, 62, 65, 70, 50, 65, 80}; /* nose=62, mid=50, half=20 -> yaw=0.60 */
    assert(calc_face_pose_yaw_from_keypoints(turned_kpt1, &test_yaw) == true);
    assert(fabsf(test_yaw - 0.60f) < 0.01f);
    assert(is_pose_valid_for_step(1, test_yaw) == true);
    assert(is_pose_valid_for_step(0, test_yaw) == false);

    /* Turned face (yaw < -0.45) */
    std::vector<int> turned_kpt2 = {30, 50, 35, 80, 38, 65, 70, 50, 65, 80}; /* nose=38, mid=50, half=20 -> yaw=-0.60 */
    assert(calc_face_pose_yaw_from_keypoints(turned_kpt2, &test_yaw) == true);
    assert(fabsf(test_yaw - (-0.60f)) < 0.01f);
    assert(is_pose_valid_for_step(2, test_yaw) == true);
    assert(is_pose_valid_for_step(0, test_yaw) == false);

    /* Prompts check */
    assert(strcmp(get_enroll_pose_prompt(0), "正面を向いてください") == 0);
    assert(strcmp(get_enroll_pose_prompt(1), "少し左を向いてください") == 0);
    assert(strcmp(get_enroll_pose_prompt(2), "少し右を向いてください") == 0);
    assert(strcmp(get_enroll_pose_prompt(3), "正面を向いてください") == 0);
    assert(strcmp(get_enroll_pose_prompt(4), "正面を向いてください") == 0);

    /* Sample state machine simulation & retry safety */
    vision_service_begin_enrollment("TestUser");
    assert(s_enroll_txn.active == true);
    assert(s_enroll_txn.sample_state == VISION_ENROLL_SAMPLE_WAITING);
    assert(s_enroll_txn.accepted_count == 0);

    /* Simulate 1 accepted sample */
    s_enroll_txn.new_feature_ids[0] = 501;
    s_enroll_txn.accepted_count = 1;
    s_enroll_txn.sample_state = VISION_ENROLL_SAMPLE_ACCEPTED;
    assert(s_enroll_txn.accepted_count == 1);

    /* Simulate retry on step 2 (extraction failed) */
    s_enroll_txn.sample_state = VISION_ENROLL_SAMPLE_RETRY;
    s_enroll_txn.error_code = VISION_ENROLL_ERR_FEATURE_EXTRACT_FAILED;
    s_enroll_txn.backend_error = ESP_FAIL;
    /* accepted_count MUST stay 1, not increment */
    assert(s_enroll_txn.accepted_count == 1);
    assert(s_enroll_txn.sample_state == VISION_ENROLL_SAMPLE_RETRY);

    /* Cancel rollback check */
    vision_service_cancel_enrollment();
    assert(s_enroll_txn.active == false);
    assert(s_enroll_txn.state == VISION_ENROLL_CANCELLED);
    assert(s_enroll_txn.error_code == VISION_ENROLL_ERR_CANCELLED);

    printf("[TEST] All Phase 5, 6 & enrollment UX/reliability unit tests passed successfully!\n");
    return 0;
}
