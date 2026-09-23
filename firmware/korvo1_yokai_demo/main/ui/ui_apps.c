#include "ui/ui_apps.h"
#include "ui/ui_theme.h"
#include "ui/ui_drawer.h"
#include "synth_service.h"
#include "vision_service.h"
#include "esp_log.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


/* -------------------------------------------------------------
 * Common Helper: Header Bar
 * ------------------------------------------------------------- */
static lv_obj_t *create_screen_header(lv_obj_t *parent, const char *title, ui_home_btn_cb_t home_cb)
{
    lv_obj_t *btn_home = lv_button_create(parent);
    lv_obj_add_style(btn_home, &ui_style_btn_home, 0);
    lv_obj_set_size(btn_home, 106, 36);
    lv_obj_set_pos(btn_home, 16, 12);
    if (home_cb) {
        lv_obj_add_event_cb(btn_home, (lv_event_cb_t)home_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_label_set_text(lbl_home, "ホーム");
    lv_obj_set_style_text_font(lbl_home, UI_FONT_REGULAR, 0);
    lv_obj_center(lbl_home);

    lv_obj_t *lbl_title = lv_label_create(parent);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_title, 134, 15);

    return btn_home;
}

/* -------------------------------------------------------------
 * 1. Voice Shrine Screen (言霊の神社)
 * ------------------------------------------------------------- */
static lv_obj_t *s_lbl_voice_state = NULL;
static lv_obj_t *s_lbl_voice_result = NULL;
static lv_obj_t *s_lbl_voice_detail = NULL;
static lv_timer_t *s_voice_reset_timer = NULL;

static void voice_reset_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_lbl_voice_state && s_lbl_voice_result && s_lbl_voice_detail) {
        lv_label_set_text(s_lbl_voice_state, "常時認識中");
        lv_label_set_text(s_lbl_voice_result, "英語または日本語の命令を待っています");
        lv_label_set_text(s_lbl_voice_detail, "WakeNet: Hi ESP / 言霊の社内はウェイクワード不要です");
        lv_obj_set_style_text_color(s_lbl_voice_state, UI_COLOR_CYAN_ACCENT, 0);
    }
    if (s_voice_reset_timer) {
        lv_timer_pause(s_voice_reset_timer);
    }
}

lv_obj_t *ui_voice_screen_create(ui_home_btn_cb_t home_cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_screen_header(scr, "言霊の社 (Voice Shrine)", home_cb);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_add_style(card, &ui_style_glass_card, 0);
    lv_obj_set_size(card, 768, 412);
    lv_obj_set_pos(card, 16, 56);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *status = lv_obj_create(card);
    lv_obj_add_style(status, &ui_style_glass_card, 0);
    lv_obj_set_size(status, 744, 76);
    lv_obj_set_pos(status, 12, 10);
    lv_obj_set_style_pad_all(status, 0, 0);
    lv_obj_remove_flag(status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(status, LV_OBJ_FLAG_CLICKABLE);

    s_lbl_voice_state = lv_label_create(status);
    lv_label_set_text(s_lbl_voice_state, "常時認識中");
    lv_obj_set_style_text_color(s_lbl_voice_state, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_voice_state, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(s_lbl_voice_state, 16, 10);

    s_lbl_voice_result = lv_label_create(status);
    lv_label_set_text(s_lbl_voice_result, "英語または日本語の命令を待っています");
    lv_obj_set_style_text_color(s_lbl_voice_result, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(s_lbl_voice_result, UI_FONT_SMALL, 0);
    lv_obj_set_width(s_lbl_voice_result, 530);
    lv_obj_set_pos(s_lbl_voice_result, 160, 12);

    s_lbl_voice_detail = lv_label_create(status);
    lv_label_set_text(s_lbl_voice_detail, "WakeNet: Hi ESP / 言霊の社内はウェイクワード不要です");
    lv_obj_set_style_text_color(s_lbl_voice_detail, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_voice_detail, UI_FONT_SMALL, 0);
    lv_obj_set_width(s_lbl_voice_detail, 710);
    lv_obj_set_pos(s_lbl_voice_detail, 16, 44);

    lv_obj_t *list = lv_obj_create(card);
    lv_obj_set_size(list, 744, 308);
    lv_obj_set_pos(list, 12, 94);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    for (voice_command_t command = VOICE_COMMAND_SYNTH;
         command < VOICE_COMMAND_COUNT; ++command) {
        const voice_command_info_t *info = voice_service_command_info(command);
        if (!info) continue;
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_set_size(row, 720, 46);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x111A28), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_hor(row, 12, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *feature = lv_label_create(row);
        lv_label_set_text(feature, info->feature);
        lv_obj_set_width(feature, 130);
        lv_obj_set_style_text_font(feature, UI_FONT_SMALL, 0);
        lv_obj_set_style_text_color(feature, UI_COLOR_GOLD_ACCENT, 0);
        lv_obj_align(feature, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *english = lv_label_create(row);
        lv_label_set_text(english, info->english);
        lv_obj_set_width(english, 390);
        lv_obj_set_style_text_font(english, UI_FONT_SMALL, 0);
        lv_obj_set_style_text_color(english, UI_COLOR_TEXT_TITLE, 0);
        lv_obj_align(english, LV_ALIGN_LEFT_MID, 140, 0);

        lv_obj_t *japanese = lv_label_create(row);
        lv_label_set_text(japanese, info->japanese);
        lv_obj_set_width(japanese, 150);
        lv_obj_set_style_text_font(japanese, UI_FONT_SMALL, 0);
        lv_obj_set_style_text_color(japanese, UI_COLOR_CYAN_ACCENT, 0);
        lv_obj_set_style_text_align(japanese, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(japanese, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    if (!s_voice_reset_timer) {
        s_voice_reset_timer = lv_timer_create(voice_reset_timer_cb, 2000, NULL);
        lv_timer_pause(s_voice_reset_timer);
    }

    ui_voice_screen_update(NULL, synth_service_get_master_volume(),
                           voice_service_is_ready(), voice_service_error());

    return scr;
}

void ui_voice_screen_update(const voice_result_t *result, int volume,
                            bool service_ready, const char *error_text)
{
    if (!s_lbl_voice_state || !s_lbl_voice_result || !s_lbl_voice_detail) return;
    if (!service_ready) {
        lv_label_set_text(s_lbl_voice_state, "音声認識を利用できません");
        lv_label_set_text(s_lbl_voice_result, "ESP-SR 初期化エラー");
        lv_label_set_text(s_lbl_voice_detail, error_text ? error_text : "Unknown error");
        lv_obj_set_style_text_color(s_lbl_voice_state, UI_COLOR_RED_ACCENT, 0);
        if (s_voice_reset_timer) lv_timer_pause(s_voice_reset_timer);
        return;
    }

    lv_obj_set_style_text_color(s_lbl_voice_state, UI_COLOR_CYAN_ACCENT, 0);
    if (!result) {
        lv_label_set_text(s_lbl_voice_state, "常時認識中");
        lv_label_set_text(s_lbl_voice_result, "英語または日本語の命令を待っています");
        return;
    }

    const voice_command_info_t *info = voice_service_command_info(result->command);
    char detail[96];
    switch (result->event) {
    case VOICE_EVENT_WAKE:
        lv_label_set_text(s_lbl_voice_state, "御用でしょうか");
        lv_label_set_text(s_lbl_voice_result, "WakeNet 起動");
        lv_label_set_text(s_lbl_voice_detail, "5 秒以内に命令を話してください");
        break;
    case VOICE_EVENT_LISTENING:
        lv_label_set_text(s_lbl_voice_state, "常時認識中");
        lv_label_set_text(s_lbl_voice_result, "英語または日本語の命令を待っています");
        lv_label_set_text(s_lbl_voice_detail, "WakeNet: Hi ESP / 言霊の社内はウェイクワード不要です");
        break;
    case VOICE_EVENT_COMMAND:
        if (!info) break;
        lv_label_set_text(s_lbl_voice_state, info->feature);
        lv_label_set_text(s_lbl_voice_result, result->language == VOICE_LANGUAGE_JAPANESE
                          ? info->japanese : info->english);
        int confidence = (int)(result->confidence * 100.0f + 0.5f);
        if (confidence < 0) confidence = 0;
        if (confidence > 100) confidence = 100;
        if (info->target == VOICE_TARGET_VOLUME) {
            snprintf(detail, sizeof(detail), "%s / 信頼度 %d%% / 音量 %d%%",
                     result->language == VOICE_LANGUAGE_JAPANESE ? "日本語" : "English",
                     confidence, volume);
        } else {
            snprintf(detail, sizeof(detail), "%s / 信頼度 %d%%",
                     result->language == VOICE_LANGUAGE_JAPANESE ? "日本語" : "English",
                     confidence);
        }
        lv_label_set_text(s_lbl_voice_detail, detail);
        if (s_voice_reset_timer) {
            lv_timer_reset(s_voice_reset_timer);
            lv_timer_resume(s_voice_reset_timer);
        }
        break;
    case VOICE_EVENT_RETRY:
        lv_label_set_text(s_lbl_voice_state, "もう一度");
        lv_label_set_text(s_lbl_voice_result, "命令を確認できませんでした");
        lv_label_set_text(s_lbl_voice_detail, "もう一度話してください");
        if (s_voice_reset_timer) {
            lv_timer_reset(s_voice_reset_timer);
            lv_timer_resume(s_voice_reset_timer);
        }
        break;
    case VOICE_EVENT_ERROR:
        lv_label_set_text(s_lbl_voice_state, "音声認識を利用できません");
        lv_label_set_text(s_lbl_voice_result, "ESP-SR 実行エラー");
        lv_label_set_text(s_lbl_voice_detail, error_text ? error_text : "Unknown error");
        lv_obj_set_style_text_color(s_lbl_voice_state, UI_COLOR_RED_ACCENT, 0);
        break;
    default:
        lv_label_set_text(s_lbl_voice_state, "常時認識中");
        break;
    }
}

/* -------------------------------------------------------------
 * 2. Vision AI Screen (目目連の眼)
 * ------------------------------------------------------------- */
static lv_obj_t *s_vf_img = NULL;
static lv_obj_t *s_lbl_vf_target = NULL;
static lv_obj_t *s_face_boxes[VISION_MAX_DETECTIONS] = {NULL};
static lv_obj_t *s_face_labels[VISION_MAX_DETECTIONS] = {NULL};
static lv_image_dsc_t s_preview_img_dsc = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = VISION_PREVIEW_WIDTH,
        .h = VISION_PREVIEW_HEIGHT,
        .stride = VISION_PREVIEW_WIDTH * 2,
    },
    .data_size = VISION_PREVIEW_WIDTH * VISION_PREVIEW_HEIGHT * 2,
    .data = NULL,
};

/* Right Side HUD */
static lv_obj_t *s_lbl_vision_status = NULL;
static lv_obj_t *s_lbl_vision_target = NULL;
static lv_obj_t *s_lbl_vision_perf = NULL;

/* In-progress Enrollment HUD */
static lv_obj_t *s_box_enroll_hud = NULL;
static lv_obj_t *s_lbl_enroll_step = NULL;
static lv_obj_t *s_lbl_enroll_feedback = NULL;
static lv_obj_t *s_lbl_enroll_prompt = NULL;
static lv_obj_t *s_btn_enroll_cancel = NULL;

/* Action Buttons */
static lv_obj_t *s_btn_enroll_start = NULL;
static lv_obj_t *s_btn_manage_open = NULL;
static lv_obj_t *s_btn_scan = NULL;

/* Enrollment Modal */
static lv_obj_t *s_enroll_modal = NULL;
static lv_obj_t *s_lbl_enroll_modal_err = NULL;
static lv_obj_t *s_ta_enroll_name = NULL;
static lv_obj_t *s_enroll_kb = NULL;
static int s_enroll_target_slot = -1;

/* Management Modal */
static lv_obj_t *s_manage_modal = NULL;
static lv_obj_t *s_lbl_manage_title = NULL;
static lv_obj_t *s_manage_list = NULL;

static bool s_vision_active = false;
static int s_vision_step = 0;

static void refresh_manage_list(void);

static void vision_btn_cb(lv_event_t *e)
{
    (void)e;
    s_vision_step = (s_vision_step + 1) % 3;
    const char *dets[] = {
        "[物体検出] 雪女の気配を検知 (信頼度 98%)",
        "[物体検出] 狸の置物を識別 (信頼度 94%)",
        "[物体検出] 障子に目目連が現れました (信頼度 99%)"
    };
    if (s_lbl_vision_target) {
        lv_label_set_text(s_lbl_vision_target, dets[s_vision_step]);
    }
}

static void enroll_open_btn_cb(lv_event_t *e)
{
    (void)e;
    if (vision_service_get_enrolled_count() >= VISION_MAX_PERSONS) {
        if (s_lbl_vision_status) {
            lv_label_set_text(s_lbl_vision_status, "登録数が上限(10名)です");
            lv_obj_set_style_text_color(s_lbl_vision_status, UI_COLOR_RED_ACCENT, 0);
        }
        return;
    }
    s_enroll_target_slot = -1;
    if (s_ta_enroll_name) lv_textarea_set_text(s_ta_enroll_name, "");
    if (s_lbl_enroll_modal_err) lv_label_set_text(s_lbl_enroll_modal_err, "");
    if (s_enroll_modal) lv_obj_remove_flag(s_enroll_modal, LV_OBJ_FLAG_HIDDEN);
}

static void enroll_submit_action(void)
{
    if (!s_ta_enroll_name) return;
    const char *text = lv_textarea_get_text(s_ta_enroll_name);
    if (!text || text[0] == '\0') {
        if (s_lbl_enroll_modal_err) {
            lv_label_set_text(s_lbl_enroll_modal_err, "名前を入力してください");
        }
        return;
    }

    esp_err_t err = ESP_OK;
    if (s_enroll_target_slot >= 0) {
        err = vision_service_reregister_person((uint8_t)s_enroll_target_slot, text);
    } else {
        err = vision_service_begin_enrollment(text);
    }

    if (err != ESP_OK) {
        if (s_lbl_enroll_modal_err) {
            if (err == ESP_ERR_INVALID_ARG) {
                lv_label_set_text(s_lbl_enroll_modal_err, "名前を入力してください");
            } else if (err == ESP_ERR_INVALID_STATE) {
                lv_label_set_text(s_lbl_enroll_modal_err, "同名の人物が既に登録されています");
            } else if (err == ESP_ERR_NO_MEM) {
                lv_label_set_text(s_lbl_enroll_modal_err, "登録数が上限(10名)です");
            } else if (err == ESP_ERR_TIMEOUT) {
                lv_label_set_text(s_lbl_enroll_modal_err, "コマンドがタイムアウトしました");
            } else {
                lv_label_set_text(s_lbl_enroll_modal_err, "登録を開始できませんでした");
            }
        }
        return;
    }

    if (s_lbl_enroll_modal_err) lv_label_set_text(s_lbl_enroll_modal_err, "");
    if (s_enroll_modal) lv_obj_add_flag(s_enroll_modal, LV_OBJ_FLAG_HIDDEN);
}

static void enroll_start_btn_cb(lv_event_t *e)
{
    (void)e;
    enroll_submit_action();
}

static void enroll_cancel_modal_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_enroll_modal) lv_obj_add_flag(s_enroll_modal, LV_OBJ_FLAG_HIDDEN);
}

static void enroll_stop_active_btn_cb(lv_event_t *e)
{
    (void)e;
    vision_service_cancel_enrollment();
}

static void enroll_kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        enroll_submit_action();
    } else if (code == LV_EVENT_CANCEL) {
        if (s_enroll_modal) lv_obj_add_flag(s_enroll_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

static void delete_person_btn_cb(lv_event_t *e)
{
    uint8_t slot = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    vision_service_delete_person(slot);
    refresh_manage_list();
}

static void reregister_person_btn_cb(lv_event_t *e)
{
    uint8_t slot = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    if (s_manage_modal) lv_obj_add_flag(s_manage_modal, LV_OBJ_FLAG_HIDDEN);
    s_enroll_target_slot = (int)slot;
    if (s_ta_enroll_name) {
        vision_person_summary_t summaries[VISION_MAX_PERSONS];
        size_t count = 0;
        vision_service_get_people_summary(summaries, VISION_MAX_PERSONS, &count);
        for (size_t i = 0; i < count; i++) {
            if (summaries[i].slot == slot) {
                lv_textarea_set_text(s_ta_enroll_name, summaries[i].name);
                break;
            }
        }
    }
    if (s_lbl_enroll_modal_err) lv_label_set_text(s_lbl_enroll_modal_err, "");
    if (s_enroll_modal) lv_obj_remove_flag(s_enroll_modal, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_manage_list(void)
{
    if (!s_manage_list) return;
    lv_obj_clean(s_manage_list);

    vision_person_summary_t list[VISION_MAX_PERSONS];
    size_t count = 0;
    vision_service_get_people_summary(list, VISION_MAX_PERSONS, &count);

    if (s_lbl_manage_title) {
        char title_buf[48];
        snprintf(title_buf, sizeof(title_buf), "登録者管理 (%d/10名)", (int)count);
        lv_label_set_text(s_lbl_manage_title, title_buf);
    }

    if (count == 0) {
        lv_obj_t *empty_lbl = lv_label_create(s_manage_list);
        lv_label_set_text(empty_lbl, "登録された人物はいません\n「人物を登録」から追加してください");
        lv_obj_set_style_text_color(empty_lbl, UI_COLOR_TEXT_SUB, 0);
        lv_obj_set_style_text_font(empty_lbl, UI_FONT_REGULAR, 0);
        lv_obj_set_style_text_align(empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(empty_lbl);
        return;
    }

    for (size_t i = 0; i < count; i++) {
        lv_obj_t *row = lv_obj_create(s_manage_list);
        lv_obj_set_size(row, 620, 52);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x131926), 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x232D42), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_name = lv_label_create(row);
        char name_buf[64];
        snprintf(name_buf, sizeof(name_buf), "%d. %s", list[i].slot + 1, list[i].name);
        lv_label_set_text(lbl_name, name_buf);
        lv_obj_set_style_text_font(lbl_name, UI_FONT_REGULAR, 0);
        lv_obj_set_style_text_color(lbl_name, UI_COLOR_TEXT_TITLE, 0);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 10, 0);

        lv_obj_t *lbl_info = lv_label_create(row);
        lv_label_set_text(lbl_info, "5 件保存済");
        lv_obj_set_style_text_font(lbl_info, UI_FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl_info, UI_COLOR_TEXT_SUB, 0);
        lv_obj_align(lbl_info, LV_ALIGN_LEFT_MID, 250, 0);

        lv_obj_t *btn_re = lv_button_create(row);
        lv_obj_add_style(btn_re, &ui_style_pill_badge, 0);
        lv_obj_set_size(btn_re, 90, 36);
        lv_obj_align(btn_re, LV_ALIGN_RIGHT_MID, -105, 0);
        lv_obj_set_style_bg_color(btn_re, UI_COLOR_CYAN_ACCENT, 0);
        lv_obj_add_event_cb(btn_re, reregister_person_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)list[i].slot);

        lv_obj_t *lbl_re = lv_label_create(btn_re);
        lv_label_set_text(lbl_re, "再登録");
        lv_obj_set_style_text_font(lbl_re, UI_FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl_re, lv_color_hex(0x0C0F17), 0);
        lv_obj_center(lbl_re);

        lv_obj_t *btn_del = lv_button_create(row);
        lv_obj_add_style(btn_del, &ui_style_pill_badge, 0);
        lv_obj_set_size(btn_del, 90, 36);
        lv_obj_align(btn_del, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_obj_set_style_bg_color(btn_del, UI_COLOR_RED_ACCENT, 0);
        lv_obj_add_event_cb(btn_del, delete_person_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)list[i].slot);

        lv_obj_t *lbl_del = lv_label_create(btn_del);
        lv_label_set_text(lbl_del, "削除");
        lv_obj_set_style_text_font(lbl_del, UI_FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl_del, UI_COLOR_TEXT_TITLE, 0);
        lv_obj_center(lbl_del);
    }
}

static void manage_open_btn_cb(lv_event_t *e)
{
    (void)e;
    refresh_manage_list();
    if (s_manage_modal) lv_obj_remove_flag(s_manage_modal, LV_OBJ_FLAG_HIDDEN);
}

static void manage_close_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_manage_modal) lv_obj_add_flag(s_manage_modal, LV_OBJ_FLAG_HIDDEN);
}

static void clear_all_btn_cb(lv_event_t *e)
{
    (void)e;
    vision_service_clear_all_people();
    refresh_manage_list();
}

lv_obj_t *ui_vision_screen_create(ui_home_btn_cb_t home_cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_screen_header(scr, "目目連の眼 (Vision AI)", home_cb);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_add_style(card, &ui_style_glass_card, 0);
    lv_obj_set_size(card, 768, 412);
    lv_obj_set_pos(card, 16, 56);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Viewfinder Area (520x310) */
    lv_obj_t *vf = lv_obj_create(card);
    lv_obj_set_size(vf, 520, 310);
    lv_obj_set_pos(vf, 20, 20);
    lv_obj_set_style_bg_color(vf, lv_color_hex(0x06090E), 0);
    lv_obj_set_style_border_color(vf, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_border_width(vf, 2, 0);
    lv_obj_set_style_radius(vf, 10, 0);
    lv_obj_set_style_pad_all(vf, 0, 0);
    lv_obj_set_style_clip_corner(vf, true, 0);
    lv_obj_remove_flag(vf, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_vf_target = lv_label_create(vf);
    lv_label_set_text(s_lbl_vf_target, "[ 霊視ビューファインダー / ESP-DL ]");
    lv_obj_set_style_text_color(s_lbl_vf_target, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_vf_target, UI_FONT_REGULAR, 0);
    lv_obj_center(s_lbl_vf_target);

    /* Real-time camera preview image (fills 520x310 viewfinder frame) */
    s_vf_img = lv_image_create(vf);
    lv_obj_set_pos(s_vf_img, 0, 0);
    lv_obj_set_size(s_vf_img, 520, 310);
    lv_image_set_inner_align(s_vf_img, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_add_flag(s_vf_img, LV_OBJ_FLAG_HIDDEN);

    /* Real-time Face Detection Bounding Boxes & Tracking Labels */
    for (int i = 0; i < VISION_MAX_DETECTIONS; i++) {
        s_face_boxes[i] = lv_obj_create(vf);
        lv_obj_remove_flag(s_face_boxes[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_face_boxes[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(s_face_boxes[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(s_face_boxes[i], UI_COLOR_CYAN_ACCENT, 0);
        lv_obj_set_style_border_width(s_face_boxes[i], 2, 0);
        lv_obj_set_style_radius(s_face_boxes[i], 4, 0);
        lv_obj_set_style_pad_all(s_face_boxes[i], 0, 0);
        lv_obj_add_flag(s_face_boxes[i], LV_OBJ_FLAG_HIDDEN);

        s_face_labels[i] = lv_label_create(vf);
        lv_obj_remove_flag(s_face_labels[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_face_labels[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(s_face_labels[i], lv_color_hex(0x0C1524), 0);
        lv_obj_set_style_bg_opa(s_face_labels[i], LV_OPA_80, 0);
        lv_obj_set_style_border_color(s_face_labels[i], UI_COLOR_CYAN_ACCENT, 0);
        lv_obj_set_style_border_width(s_face_labels[i], 1, 0);
        lv_obj_set_style_radius(s_face_labels[i], 4, 0);
        lv_obj_set_style_pad_hor(s_face_labels[i], 6, 0);
        lv_obj_set_style_pad_ver(s_face_labels[i], 2, 0);
        lv_obj_set_style_text_color(s_face_labels[i], UI_COLOR_CYAN_ACCENT, 0);
        lv_obj_set_style_text_font(s_face_labels[i], UI_FONT_SMALL, 0);
        lv_obj_add_flag(s_face_labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Right Control Area */
    lv_obj_t *lbl_info = lv_label_create(card);
    lv_label_set_text(lbl_info, "目目連・画像認識");
    lv_obj_set_style_text_color(lbl_info, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_info, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_info, 555, 16);

    s_lbl_vision_status = lv_label_create(card);
    lv_label_set_text(s_lbl_vision_status, "常時顔認識中");
    lv_obj_set_style_text_color(s_lbl_vision_status, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_vision_status, UI_FONT_REGULAR, 0);
    lv_obj_set_width(s_lbl_vision_status, 185);
    lv_obj_set_pos(s_lbl_vision_status, 550, 48);

    s_lbl_vision_target = lv_label_create(card);
    lv_label_set_text(s_lbl_vision_target, "探索中…");
    lv_obj_set_style_text_color(s_lbl_vision_target, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(s_lbl_vision_target, UI_FONT_TITLE, 0);
    lv_obj_set_width(s_lbl_vision_target, 185);
    lv_obj_set_pos(s_lbl_vision_target, 550, 78);

    s_lbl_vision_perf = lv_label_create(card);
    lv_label_set_text(s_lbl_vision_perf, "カメラ準備中");
    lv_obj_set_style_text_color(s_lbl_vision_perf, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(s_lbl_vision_perf, UI_FONT_SMALL, 0);
    lv_obj_set_width(s_lbl_vision_perf, 185);
    lv_obj_set_pos(s_lbl_vision_perf, 550, 115);

    /* In-Progress Enrollment HUD Box */
    s_box_enroll_hud = lv_obj_create(card);
    lv_obj_add_style(s_box_enroll_hud, &ui_style_glass_card, 0);
    lv_obj_set_size(s_box_enroll_hud, 185, 165);
    lv_obj_set_pos(s_box_enroll_hud, 550, 155);
    lv_obj_set_style_border_color(s_box_enroll_hud, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_border_width(s_box_enroll_hud, 1, 0);
    lv_obj_set_style_pad_all(s_box_enroll_hud, 6, 0);
    lv_obj_remove_flag(s_box_enroll_hud, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_box_enroll_hud, LV_OBJ_FLAG_HIDDEN);

    s_lbl_enroll_step = lv_label_create(s_box_enroll_hud);
    lv_label_set_text(s_lbl_enroll_step, "顔登録 (0/5)");
    lv_obj_set_style_text_font(s_lbl_enroll_step, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(s_lbl_enroll_step, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_align(s_lbl_enroll_step, LV_ALIGN_TOP_MID, 0, 2);

    s_lbl_enroll_feedback = lv_label_create(s_box_enroll_hud);
    lv_label_set_text(s_lbl_enroll_feedback, "");
    lv_obj_set_style_text_font(s_lbl_enroll_feedback, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_lbl_enroll_feedback, UI_COLOR_GREEN_ACCENT, 0);
    lv_obj_set_style_text_align(s_lbl_enroll_feedback, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_enroll_feedback, 175);
    lv_obj_align(s_lbl_enroll_feedback, LV_ALIGN_TOP_MID, 0, 24);

    s_lbl_enroll_prompt = lv_label_create(s_box_enroll_hud);
    lv_label_set_text(s_lbl_enroll_prompt, "正面を向いてください");
    lv_obj_set_style_text_font(s_lbl_enroll_prompt, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_lbl_enroll_prompt, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_align(s_lbl_enroll_prompt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_enroll_prompt, 175);
    lv_obj_align(s_lbl_enroll_prompt, LV_ALIGN_TOP_MID, 0, 48);

    s_btn_enroll_cancel = lv_button_create(s_box_enroll_hud);
    lv_obj_add_style(s_btn_enroll_cancel, &ui_style_pill_badge, 0);
    lv_obj_set_size(s_btn_enroll_cancel, 160, 38);
    lv_obj_align(s_btn_enroll_cancel, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(s_btn_enroll_cancel, UI_COLOR_RED_ACCENT, 0);
    lv_obj_add_event_cb(s_btn_enroll_cancel, enroll_stop_active_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_stop = lv_label_create(s_btn_enroll_cancel);
    lv_label_set_text(lbl_stop, "登録中止");
    lv_obj_set_style_text_font(lbl_stop, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(lbl_stop, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_center(lbl_stop);

    /* Action Buttons (Visible when not enrolling) */
    s_btn_enroll_start = lv_button_create(card);
    lv_obj_add_style(s_btn_enroll_start, &ui_style_pill_badge, 0);
    lv_obj_set_size(s_btn_enroll_start, 180, 44);
    lv_obj_set_pos(s_btn_enroll_start, 550, 175);
    lv_obj_set_style_bg_color(s_btn_enroll_start, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_add_event_cb(s_btn_enroll_start, enroll_open_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_enr = lv_label_create(s_btn_enroll_start);
    lv_label_set_text(lbl_enr, "人物を登録");
    lv_obj_set_style_text_font(lbl_enr, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(lbl_enr, lv_color_hex(0x0C0F17), 0);
    lv_obj_center(lbl_enr);

    s_btn_manage_open = lv_button_create(card);
    lv_obj_add_style(s_btn_manage_open, &ui_style_glass_card, 0);
    lv_obj_set_size(s_btn_manage_open, 180, 44);
    lv_obj_set_pos(s_btn_manage_open, 550, 235);
    lv_obj_set_style_border_color(s_btn_manage_open, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_border_width(s_btn_manage_open, 1, 0);
    lv_obj_add_event_cb(s_btn_manage_open, manage_open_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_mgr = lv_label_create(s_btn_manage_open);
    lv_label_set_text(lbl_mgr, "登録者管理");
    lv_obj_set_style_text_font(lbl_mgr, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(lbl_mgr, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_center(lbl_mgr);

    s_btn_scan = lv_button_create(card);
    lv_obj_add_style(s_btn_scan, &ui_style_glass_card, 0);
    lv_obj_set_size(s_btn_scan, 180, 40);
    lv_obj_set_pos(s_btn_scan, 550, 295);
    lv_obj_set_style_border_color(s_btn_scan, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_border_width(s_btn_scan, 1, 0);
    lv_obj_add_event_cb(s_btn_scan, vision_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_scan = lv_label_create(s_btn_scan);
    lv_label_set_text(lbl_scan, "霊視スキャン");
    lv_obj_set_style_text_font(lbl_scan, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_scan, UI_COLOR_TEXT_SUB, 0);
    lv_obj_center(lbl_scan);

    /* --- 1. Full-Screen Name Enrollment Modal --- */
    s_enroll_modal = lv_obj_create(scr);
    lv_obj_set_size(s_enroll_modal, 800, 480);
    lv_obj_set_pos(s_enroll_modal, 0, 0);
    lv_obj_set_style_bg_color(s_enroll_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_enroll_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_enroll_modal, 0, 0);
    lv_obj_remove_flag(s_enroll_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *enroll_panel = lv_obj_create(s_enroll_modal);
    lv_obj_add_style(enroll_panel, &ui_style_glass_card, 0);
    lv_obj_set_size(enroll_panel, 750, 430);
    lv_obj_center(enroll_panel);
    lv_obj_remove_flag(enroll_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_modal_title = lv_label_create(enroll_panel);
    lv_label_set_text(lbl_modal_title, "人物登録 (顔認識)");
    lv_obj_set_style_text_color(lbl_modal_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_modal_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_modal_title, 20, 14);

    s_lbl_enroll_modal_err = lv_label_create(enroll_panel);
    lv_label_set_text(s_lbl_enroll_modal_err, "");
    lv_obj_set_style_text_color(s_lbl_enroll_modal_err, UI_COLOR_RED_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_enroll_modal_err, UI_FONT_SMALL, 0);
    lv_obj_set_pos(s_lbl_enroll_modal_err, 250, 18);

    s_ta_enroll_name = lv_textarea_create(enroll_panel);
    lv_textarea_set_one_line(s_ta_enroll_name, true);
    lv_textarea_set_placeholder_text(s_ta_enroll_name, "名前を入力 (Alice)");
    lv_textarea_set_max_length(s_ta_enroll_name, 24);
    lv_obj_set_size(s_ta_enroll_name, 430, 44);
    lv_obj_set_pos(s_ta_enroll_name, 20, 55);
    lv_obj_set_style_bg_color(s_ta_enroll_name, UI_COLOR_KEY_WHITE, 0);
    lv_obj_set_style_text_color(s_ta_enroll_name, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(s_ta_enroll_name, UI_FONT_REGULAR, 0);
    lv_obj_set_style_pad_ver(s_ta_enroll_name, 1, 0);

    lv_obj_t *btn_start = lv_button_create(enroll_panel);
    lv_obj_add_style(btn_start, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_start, 115, 44);
    lv_obj_set_pos(btn_start, 470, 55);
    lv_obj_set_style_bg_color(btn_start, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_add_event_cb(btn_start, enroll_start_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_st = lv_label_create(btn_start);
    lv_label_set_text(lbl_st, "開始");
    lv_obj_set_style_text_font(lbl_st, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(lbl_st, lv_color_hex(0x0C0F17), 0);
    lv_obj_center(lbl_st);

    lv_obj_t *btn_canc = lv_button_create(enroll_panel);
    lv_obj_add_style(btn_canc, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_canc, 120, 44);
    lv_obj_set_pos(btn_canc, 600, 55);
    lv_obj_add_event_cb(btn_canc, enroll_cancel_modal_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_cn = lv_label_create(btn_canc);
    lv_label_set_text(lbl_cn, "キャンセル");
    lv_obj_set_style_text_font(lbl_cn, UI_FONT_SMALL, 0);
    lv_obj_center(lbl_cn);

    s_enroll_kb = lv_keyboard_create(enroll_panel);
    lv_obj_set_size(s_enroll_kb, 710, 305);
    lv_obj_set_pos(s_enroll_kb, 15, 110);
    lv_keyboard_set_textarea(s_enroll_kb, s_ta_enroll_name);
    lv_obj_add_event_cb(s_enroll_kb, enroll_kb_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_add_flag(s_enroll_modal, LV_OBJ_FLAG_HIDDEN);

    /* --- 2. Full-Screen Management Modal --- */
    s_manage_modal = lv_obj_create(scr);
    lv_obj_set_size(s_manage_modal, 800, 480);
    lv_obj_set_pos(s_manage_modal, 0, 0);
    lv_obj_set_style_bg_color(s_manage_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_manage_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_manage_modal, 0, 0);
    lv_obj_remove_flag(s_manage_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *manage_panel = lv_obj_create(s_manage_modal);
    lv_obj_add_style(manage_panel, &ui_style_glass_card, 0);
    lv_obj_set_size(manage_panel, 680, 420);
    lv_obj_center(manage_panel);
    lv_obj_remove_flag(manage_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_manage_title = lv_label_create(manage_panel);
    lv_label_set_text(s_lbl_manage_title, "登録者管理 (0/10名)");
    lv_obj_set_style_text_color(s_lbl_manage_title, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_manage_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(s_lbl_manage_title, 20, 14);

    s_manage_list = lv_obj_create(manage_panel);
    lv_obj_set_size(s_manage_list, 640, 275);
    lv_obj_set_pos(s_manage_list, 20, 55);
    lv_obj_set_style_bg_opa(s_manage_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_manage_list, 0, 0);
    lv_obj_set_style_pad_all(s_manage_list, 4, 0);
    lv_obj_set_flex_flow(s_manage_list, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *btn_clear = lv_button_create(manage_panel);
    lv_obj_add_style(btn_clear, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_clear, 130, 42);
    lv_obj_set_pos(btn_clear, 20, 350);
    lv_obj_set_style_bg_color(btn_clear, UI_COLOR_RED_ACCENT, 0);
    lv_obj_add_event_cb(btn_clear, clear_all_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_clr = lv_label_create(btn_clear);
    lv_label_set_text(lbl_clr, "全消去");
    lv_obj_set_style_text_font(lbl_clr, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(lbl_clr, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_center(lbl_clr);

    lv_obj_t *btn_close = lv_button_create(manage_panel);
    lv_obj_add_style(btn_close, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_close, 130, 42);
    lv_obj_set_pos(btn_close, 530, 350);
    lv_obj_set_style_bg_color(btn_close, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_add_event_cb(btn_close, manage_close_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_cls = lv_label_create(btn_close);
    lv_label_set_text(lbl_cls, "閉じる");
    lv_obj_set_style_text_font(lbl_cls, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(lbl_cls, lv_color_hex(0x0C0F17), 0);
    lv_obj_center(lbl_cls);

    lv_obj_add_flag(s_manage_modal, LV_OBJ_FLAG_HIDDEN);

    return scr;
}

void ui_vision_set_active(bool active)
{
    s_vision_active = active;
    if (active) {
        if (s_lbl_vision_status) {
            lv_label_set_text(s_lbl_vision_status, "常時顔認識中");
            lv_obj_set_style_text_color(s_lbl_vision_status, UI_COLOR_CYAN_ACCENT, 0);
        }
        if (s_lbl_vision_target) {
            lv_label_set_text(s_lbl_vision_target, "カメラ準備中…");
        }
    } else {
        if (s_lbl_vision_status) {
            lv_label_set_text(s_lbl_vision_status, "[顔検知] 停止中");
        }
        if (s_vf_img) {
            lv_obj_add_flag(s_vf_img, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_lbl_vf_target) {
            lv_obj_remove_flag(s_lbl_vf_target, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_enroll_modal) {
            lv_obj_add_flag(s_enroll_modal, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_manage_modal) {
            lv_obj_add_flag(s_manage_modal, LV_OBJ_FLAG_HIDDEN);
        }
        for (int i = 0; i < VISION_MAX_DETECTIONS; i++) {
            if (s_face_boxes[i]) lv_obj_add_flag(s_face_boxes[i], LV_OBJ_FLAG_HIDDEN);
            if (s_face_labels[i]) lv_obj_add_flag(s_face_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_vision_screen_update(void)
{
    if (!s_vision_active) {
        return;
    }

    bool modal_visible = (s_enroll_modal && !lv_obj_has_flag(s_enroll_modal, LV_OBJ_FLAG_HIDDEN)) ||
                         (s_manage_modal && !lv_obj_has_flag(s_manage_modal, LV_OBJ_FLAG_HIDDEN));
    if (modal_visible && s_vf_img && !lv_obj_has_flag(s_vf_img, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(s_vf_img, LV_OBJ_FLAG_HIDDEN);
    }

    /* 1. Consume fresh camera preview frame */
    const uint8_t *frame_data = NULL;
    uint16_t fw = 0, fh = 0;
    if (vision_service_get_preview_frame(&frame_data, &fw, &fh)) {
        if (!modal_visible && s_vf_img && frame_data) {
            s_preview_img_dsc.data = frame_data;
            lv_image_set_src(s_vf_img, &s_preview_img_dsc);
            lv_obj_remove_flag(s_vf_img, LV_OBJ_FLAG_HIDDEN);
            if (s_lbl_vf_target) {
                lv_obj_add_flag(s_lbl_vf_target, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_invalidate(s_vf_img);
        }
    }

    /* 2. Poll inference detection results */
    vision_result_t res;
    while (vision_service_poll_result(&res)) {
        if (modal_visible) continue;
        if (res.mode == VISION_MODE_FACE) {
            /* Check if enrollment session is active */
            if (res.enroll_state != VISION_ENROLL_IDLE) {
                if (s_box_enroll_hud) lv_obj_remove_flag(s_box_enroll_hud, LV_OBJ_FLAG_HIDDEN);
                if (s_btn_enroll_start) lv_obj_add_flag(s_btn_enroll_start, LV_OBJ_FLAG_HIDDEN);
                if (s_btn_manage_open) lv_obj_add_flag(s_btn_manage_open, LV_OBJ_FLAG_HIDDEN);
                if (s_btn_scan) lv_obj_add_flag(s_btn_scan, LV_OBJ_FLAG_HIDDEN);

                if (res.enroll_state == VISION_ENROLL_SUCCESS) {
                    if (s_box_enroll_hud) lv_obj_set_style_border_color(s_box_enroll_hud, UI_COLOR_GREEN_ACCENT, 0);
                    if (s_lbl_enroll_step) {
                        lv_label_set_text(s_lbl_enroll_step, "顔登録 5/5");
                        lv_obj_set_style_text_color(s_lbl_enroll_step, UI_COLOR_GREEN_ACCENT, 0);
                    }
                    if (s_lbl_enroll_feedback) {
                        lv_label_set_text(s_lbl_enroll_feedback, "✓ 顔登録が完了しました");
                        lv_obj_set_style_text_color(s_lbl_enroll_feedback, UI_COLOR_GREEN_ACCENT, 0);
                    }
                    if (s_lbl_enroll_prompt) {
                        lv_label_set_text(s_lbl_enroll_prompt, res.enroll_name);
                        lv_obj_set_style_text_color(s_lbl_enroll_prompt, UI_COLOR_TEXT_TITLE, 0);
                    }
                    if (s_lbl_vision_status) {
                        lv_label_set_text(s_lbl_vision_status, "登録完了");
                        lv_obj_set_style_text_color(s_lbl_vision_status, UI_COLOR_GREEN_ACCENT, 0);
                    }
                    if (s_lbl_vision_target) {
                        lv_label_set_text(s_lbl_vision_target, res.enroll_name);
                    }
                } else if (res.enroll_state == VISION_ENROLL_ERROR || res.enroll_state == VISION_ENROLL_CANCELLED) {
                    if (s_box_enroll_hud) lv_obj_set_style_border_color(s_box_enroll_hud, UI_COLOR_RED_ACCENT, 0);
                    if (s_lbl_enroll_step) {
                        lv_label_set_text(s_lbl_enroll_step, res.enroll_state == VISION_ENROLL_CANCELLED ? "登録中止" : "登録エラー");
                        lv_obj_set_style_text_color(s_lbl_enroll_step, UI_COLOR_RED_ACCENT, 0);
                    }
                    if (s_lbl_enroll_feedback) {
                        if (res.enroll_error_code != VISION_ENROLL_ERR_NONE) {
                            char err_buf[32];
                            snprintf(err_buf, sizeof(err_buf), "✕ E%d エラー", res.enroll_error_code);
                            lv_label_set_text(s_lbl_enroll_feedback, err_buf);
                        } else {
                            lv_label_set_text(s_lbl_enroll_feedback, res.enroll_state == VISION_ENROLL_CANCELLED ? "✕ 中止されました" : "✕ 失敗しました");
                        }
                        lv_obj_set_style_text_color(s_lbl_enroll_feedback, UI_COLOR_RED_ACCENT, 0);
                    }
                    if (s_lbl_enroll_prompt) {
                        lv_label_set_text(s_lbl_enroll_prompt, res.enroll_prompt);
                        lv_obj_set_style_text_color(s_lbl_enroll_prompt, UI_COLOR_RED_ACCENT, 0);
                    }
                    if (s_lbl_vision_status) {
                        lv_label_set_text(s_lbl_vision_status, res.enroll_state == VISION_ENROLL_CANCELLED ? "登録中止" : "エラー");
                        lv_obj_set_style_text_color(s_lbl_vision_status, UI_COLOR_RED_ACCENT, 0);
                    }
                } else {
                    /* SAMPLING / COMMITTING / WAIT_FACE */
                    char step_buf[32];
                    uint8_t curr_step = (res.enroll_sample_state == VISION_ENROLL_SAMPLE_ACCEPTED)
                                        ? res.enroll_sample_count
                                        : (res.enroll_sample_count + 1);
                    if (curr_step > 5) curr_step = 5;
                    snprintf(step_buf, sizeof(step_buf), "顔登録 %d / 5", curr_step);
                    if (s_lbl_enroll_step) lv_label_set_text(s_lbl_enroll_step, step_buf);

                    if (res.enroll_sample_state == VISION_ENROLL_SAMPLE_ACCEPTED) {
                        if (s_box_enroll_hud) lv_obj_set_style_border_color(s_box_enroll_hud, UI_COLOR_GREEN_ACCENT, 0);
                        if (s_lbl_enroll_step) lv_obj_set_style_text_color(s_lbl_enroll_step, UI_COLOR_GREEN_ACCENT, 0);
                        if (s_lbl_enroll_feedback) {
                            char fb_buf[48];
                            snprintf(fb_buf, sizeof(fb_buf), "✓ 第%dステップ完了！", res.enroll_sample_count);
                            lv_label_set_text(s_lbl_enroll_feedback, fb_buf);
                            lv_obj_set_style_text_color(s_lbl_enroll_feedback, UI_COLOR_GREEN_ACCENT, 0);
                        }
                        if (s_lbl_vision_status) {
                            char st_buf[32];
                            snprintf(st_buf, sizeof(st_buf), "第%dステップ完了", res.enroll_sample_count);
                            lv_label_set_text(s_lbl_vision_status, st_buf);
                            lv_obj_set_style_text_color(s_lbl_vision_status, UI_COLOR_GREEN_ACCENT, 0);
                        }
                    } else if (res.enroll_sample_state == VISION_ENROLL_SAMPLE_RETRY) {
                        if (s_box_enroll_hud) lv_obj_set_style_border_color(s_box_enroll_hud, UI_COLOR_RED_ACCENT, 0);
                        if (s_lbl_enroll_step) lv_obj_set_style_text_color(s_lbl_enroll_step, UI_COLOR_RED_ACCENT, 0);
                        if (s_lbl_enroll_feedback) {
                            char fb_buf[48];
                            snprintf(fb_buf, sizeof(fb_buf), "✕ E%d 再試行", res.enroll_error_code);
                            lv_label_set_text(s_lbl_enroll_feedback, fb_buf);
                            lv_obj_set_style_text_color(s_lbl_enroll_feedback, UI_COLOR_RED_ACCENT, 0);
                        }
                        if (s_lbl_vision_status) {
                            lv_label_set_text(s_lbl_vision_status, "再試行してください");
                            lv_obj_set_style_text_color(s_lbl_vision_status, UI_COLOR_RED_ACCENT, 0);
                        }
                    } else if (res.enroll_sample_state == VISION_ENROLL_SAMPLE_CAPTURING) {
                        if (s_box_enroll_hud) lv_obj_set_style_border_color(s_box_enroll_hud, UI_COLOR_GOLD_ACCENT, 0);
                        if (s_lbl_enroll_step) lv_obj_set_style_text_color(s_lbl_enroll_step, UI_COLOR_GOLD_ACCENT, 0);
                        if (s_lbl_enroll_feedback) {
                            lv_label_set_text(s_lbl_enroll_feedback, "撮影中…");
                            lv_obj_set_style_text_color(s_lbl_enroll_feedback, UI_COLOR_GOLD_ACCENT, 0);
                        }
                        if (s_lbl_vision_status) {
                            lv_label_set_text(s_lbl_vision_status, "特徴抽出中…");
                            lv_obj_set_style_text_color(s_lbl_vision_status, UI_COLOR_GOLD_ACCENT, 0);
                        }
                    } else {
                        /* WAITING or STABILIZING */
                        lv_color_t color = (res.enroll_sample_state == VISION_ENROLL_SAMPLE_STABILIZING)
                                           ? UI_COLOR_GOLD_ACCENT : UI_COLOR_CYAN_ACCENT;
                        if (s_box_enroll_hud) lv_obj_set_style_border_color(s_box_enroll_hud, color, 0);
                        if (s_lbl_enroll_step) lv_obj_set_style_text_color(s_lbl_enroll_step, UI_COLOR_GOLD_ACCENT, 0);
                        if (s_lbl_enroll_feedback) {
                            if (res.enroll_error_code != VISION_ENROLL_ERR_NONE) {
                                char fb_buf[32];
                                snprintf(fb_buf, sizeof(fb_buf), "E%d", res.enroll_error_code);
                                lv_label_set_text(s_lbl_enroll_feedback, fb_buf);
                                lv_obj_set_style_text_color(s_lbl_enroll_feedback, UI_COLOR_RED_ACCENT, 0);
                            } else {
                                lv_label_set_text(s_lbl_enroll_feedback, "顔を合わせてください");
                                lv_obj_set_style_text_color(s_lbl_enroll_feedback, color, 0);
                            }
                        }
                        if (s_lbl_vision_status) {
                            lv_label_set_text(s_lbl_vision_status, "顔登録実行中");
                            lv_obj_set_style_text_color(s_lbl_vision_status, UI_COLOR_GOLD_ACCENT, 0);
                        }
                    }
                    if (s_lbl_enroll_prompt) {
                        lv_label_set_text(s_lbl_enroll_prompt, res.enroll_prompt);
                        lv_obj_set_style_text_color(s_lbl_enroll_prompt, UI_COLOR_TEXT_TITLE, 0);
                    }
                    if (s_lbl_vision_target) {
                        lv_label_set_text(s_lbl_vision_target, res.enroll_name);
                    }
                }
            } else {
                if (s_box_enroll_hud) lv_obj_add_flag(s_box_enroll_hud, LV_OBJ_FLAG_HIDDEN);
                if (s_btn_enroll_start) lv_obj_remove_flag(s_btn_enroll_start, LV_OBJ_FLAG_HIDDEN);
                if (s_btn_manage_open) lv_obj_remove_flag(s_btn_manage_open, LV_OBJ_FLAG_HIDDEN);
                if (s_btn_scan) lv_obj_remove_flag(s_btn_scan, LV_OBJ_FLAG_HIDDEN);

                if (res.count == 0) {
                    if (s_lbl_vision_status) {
                        lv_label_set_text(s_lbl_vision_status, "顔が見つかりません");
                        lv_obj_set_style_text_color(s_lbl_vision_status, UI_COLOR_TEXT_SUB, 0);
                    }
                    if (s_lbl_vision_target) lv_label_set_text(s_lbl_vision_target, "探索中…");
                    if (s_lbl_vision_perf) lv_label_set_text(s_lbl_vision_perf, "カメラプレビュー中");
                } else {
                    if (res.primary_match_state == VISION_FACE_MATCH_KNOWN) {
                        if (s_lbl_vision_status) {
                            lv_label_set_text(s_lbl_vision_status, "認識しました");
                            lv_obj_set_style_text_color(s_lbl_vision_status, UI_COLOR_GREEN_ACCENT, 0);
                        }
                        if (s_lbl_vision_target) {
                            char target_buf[64];
                            int pct = (int)(res.primary_similarity * 100.0f);
                            snprintf(target_buf, sizeof(target_buf), "%s (%d%%)", res.primary_name, pct);
                            lv_label_set_text(s_lbl_vision_target, target_buf);
                        }
                    } else {
                        if (s_lbl_vision_status) {
                            lv_label_set_text(s_lbl_vision_status, "未登録の人物です");
                            lv_obj_set_style_text_color(s_lbl_vision_status, UI_COLOR_GOLD_ACCENT, 0);
                        }
                        if (s_lbl_vision_target) lv_label_set_text(s_lbl_vision_target, "未登録");
                    }

                    if (s_lbl_vision_perf) {
                        char perf_buf[64];
                        snprintf(perf_buf, sizeof(perf_buf), "%d人 / 検出 %lums / 認識 %lums",
                                 res.count, (unsigned long)res.inference_ms, (unsigned long)res.recognition_ms);
                        lv_label_set_text(s_lbl_vision_perf, perf_buf);
                    }
                }
            }

            /* Update bounding boxes and tracking labels in Viewfinder */
            for (int i = 0; i < VISION_MAX_DETECTIONS; i++) {
                if (!s_face_boxes[i] || !s_face_labels[i]) continue;
                if (i < res.count) {
                    int bx = (res.boxes[i].x * 520) / VISION_PREVIEW_WIDTH;
                    int by = (res.boxes[i].y * 310) / VISION_PREVIEW_HEIGHT;
                    int bw = (res.boxes[i].w * 520) / VISION_PREVIEW_WIDTH;
                    int bh = (res.boxes[i].h * 310) / VISION_PREVIEW_HEIGHT;
                    if (bw < 10) bw = 10;
                    if (bh < 10) bh = 10;
                    if (bx < 0) bx = 0;
                    if (by < 0) by = 0;
                    if (bx + bw > 520) bw = 520 - bx;
                    if (by + bh > 310) bh = 310 - by;

                    lv_obj_set_pos(s_face_boxes[i], bx, by);
                    lv_obj_set_size(s_face_boxes[i], bw, bh);

                    if (res.boxes[i].match_state == VISION_FACE_MATCH_KNOWN) {
                        lv_obj_set_style_border_color(s_face_boxes[i], UI_COLOR_GREEN_ACCENT, 0);
                        lv_obj_set_style_border_color(s_face_labels[i], UI_COLOR_GREEN_ACCENT, 0);
                        lv_obj_set_style_text_color(s_face_labels[i], UI_COLOR_GREEN_ACCENT, 0);
                    } else {
                        lv_obj_set_style_border_color(s_face_boxes[i], UI_COLOR_CYAN_ACCENT, 0);
                        lv_obj_set_style_border_color(s_face_labels[i], UI_COLOR_CYAN_ACCENT, 0);
                        lv_obj_set_style_text_color(s_face_labels[i], UI_COLOR_CYAN_ACCENT, 0);
                    }

                    int lbl_y = by - 24;
                    if (lbl_y < 2) lbl_y = by + 4;
                    int lbl_x = bx;
                    if (lbl_x > 400) lbl_x = 400;
                    lv_obj_set_pos(s_face_labels[i], lbl_x, lbl_y);
                    lv_label_set_text(s_face_labels[i], res.boxes[i].label);

                    lv_obj_remove_flag(s_face_boxes[i], LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(s_face_labels[i], LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(s_face_boxes[i], LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(s_face_labels[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }
}

/* -------------------------------------------------------------
 * 3. Fireworks Screen (夜空の花火)
 * ------------------------------------------------------------- */
static lv_obj_t *s_fireworks_area = NULL;
static lv_obj_t *s_lbl_fw_count = NULL;
static int s_fw_count = 0;

static void fw_anim_del_cb(lv_anim_t *a)
{
    lv_obj_t *obj = (lv_obj_t *)a->var;
    if (obj) {
        lv_obj_delete(obj);
    }
}

static void fw_anim_opa_cb(lv_anim_t *a, int32_t val)
{
    lv_obj_t *obj = (lv_obj_t *)a->var;
    if (obj) {
        lv_obj_set_style_bg_opa(obj, val, 0);
    }
}

static void fw_anim_size_cb(lv_anim_t *a, int32_t val)
{
    lv_obj_t *obj = (lv_obj_t *)a->var;
    if (obj) {
        int32_t cx = (int32_t)(intptr_t)lv_obj_get_user_data(obj) >> 16;
        int32_t cy = (int32_t)(int16_t)(intptr_t)lv_obj_get_user_data(obj);
        lv_obj_set_size(obj, val, val);
        lv_obj_set_pos(obj, cx - val / 2, cy - val / 2);
    }
}

static void fireworks_touch_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev || !s_fireworks_area) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    s_fw_count++;
    if (s_lbl_fw_count) {
        char buf[32];
        snprintf(buf, sizeof(buf), "打上数: %d 発", s_fw_count);
        lv_label_set_text(s_lbl_fw_count, buf);
    }

    int32_t rel_x = pt.x - 16;
    int32_t rel_y = pt.y - 56;

    lv_obj_t *p = lv_obj_create(s_fireworks_area);
    lv_obj_set_size(p, 10, 10);
    lv_obj_set_pos(p, rel_x - 5, rel_y - 5);
    lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
    lv_color_t colors[3] = {UI_COLOR_RED_ACCENT, UI_COLOR_GOLD_ACCENT, UI_COLOR_CYAN_ACCENT};
    lv_obj_set_style_bg_color(p, colors[s_fw_count % 3], 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);

    intptr_t coords = ((rel_x & 0xFFFF) << 16) | (rel_y & 0xFFFF);
    lv_obj_set_user_data(p, (void *)coords);

    /* Animate expansion */
    lv_anim_t a_size;
    lv_anim_init(&a_size);
    lv_anim_set_var(&a_size, p);
    lv_anim_set_values(&a_size, 10, 70);
    lv_anim_set_duration(&a_size, 550);
    lv_anim_set_path_cb(&a_size, lv_anim_path_ease_out);
    lv_anim_set_custom_exec_cb(&a_size, fw_anim_size_cb);
    lv_anim_start(&a_size);

    /* Animate fade out and auto delete */
    lv_anim_t a_opa;
    lv_anim_init(&a_opa);
    lv_anim_set_var(&a_opa, p);
    lv_anim_set_values(&a_opa, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a_opa, 550);
    lv_anim_set_path_cb(&a_opa, lv_anim_path_ease_out);
    lv_anim_set_custom_exec_cb(&a_opa, fw_anim_opa_cb);
    lv_anim_set_completed_cb(&a_opa, fw_anim_del_cb);
    lv_anim_start(&a_opa);
}

lv_obj_t *ui_fireworks_screen_create(ui_home_btn_cb_t home_cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_screen_header(scr, "夜空の花火 (Night Sky Fireworks)", home_cb);

    s_lbl_fw_count = lv_label_create(scr);
    lv_label_set_text(s_lbl_fw_count, "打上数: 0 発");
    lv_obj_set_style_text_color(s_lbl_fw_count, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_fw_count, UI_FONT_REGULAR, 0);
    lv_obj_set_pos(s_lbl_fw_count, 640, 16);

    s_fireworks_area = lv_obj_create(scr);
    lv_obj_add_style(s_fireworks_area, &ui_style_glass_card, 0);
    lv_obj_set_size(s_fireworks_area, 768, 412);
    lv_obj_set_pos(s_fireworks_area, 16, 56);
    lv_obj_add_flag(s_fireworks_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_fireworks_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_fireworks_area, fireworks_touch_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_tip = lv_label_create(s_fireworks_area);
    lv_label_set_text(lbl_tip, "夜空をタップして花火を打ち上げてください ★");
    lv_obj_set_style_text_color(lbl_tip, UI_COLOR_TEXT_SUB, 0);
    lv_obj_set_style_text_font(lbl_tip, UI_FONT_REGULAR, 0);
    lv_obj_align(lbl_tip, LV_ALIGN_BOTTOM_MID, 0, -16);

    return scr;
}

/* -------------------------------------------------------------
 * 4. Clock & Timer Screen (狸屋の時計)
 * ------------------------------------------------------------- */
static lv_obj_t *s_lbl_clock_big = NULL;
static lv_obj_t *s_lbl_timer = NULL;
static int s_timer_seconds = 180; /* 3 minutes */
static bool s_timer_running = false;

static void timer_start_cb(lv_event_t *e)
{
    (void)e;
    s_timer_running = !s_timer_running;
}

static void timer_reset_cb(lv_event_t *e)
{
    (void)e;
    s_timer_running = false;
    s_timer_seconds = 180;
    if (s_lbl_timer) {
        lv_label_set_text(s_lbl_timer, "03:00");
    }
}

lv_obj_t *ui_clock_screen_create(ui_home_btn_cb_t home_cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_screen_header(scr, "狸屋の時計 (Tanuki Clock & Timer)", home_cb);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_add_style(card, &ui_style_glass_card, 0);
    lv_obj_set_size(card, 768, 412);
    lv_obj_set_pos(card, 16, 56);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Big Clock */
    s_lbl_clock_big = lv_label_create(card);
    lv_label_set_text(s_lbl_clock_big, "12:00:00");
    lv_obj_set_style_text_color(s_lbl_clock_big, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_clock_big, UI_FONT_LARGE, 0);
    lv_obj_set_pos(s_lbl_clock_big, 260, 40);

    /* Timer Section */
    lv_obj_t *lbl_t_title = lv_label_create(card);
    lv_label_set_text(lbl_t_title, "茶道・妖怪タイマー (Timer)");
    lv_obj_set_style_text_color(lbl_t_title, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_style_text_font(lbl_t_title, UI_FONT_TITLE, 0);
    lv_obj_set_pos(lbl_t_title, 250, 160);

    s_lbl_timer = lv_label_create(card);
    lv_label_set_text(s_lbl_timer, "03:00");
    lv_obj_set_style_text_color(s_lbl_timer, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_timer, UI_FONT_LARGE, 0);
    lv_obj_set_pos(s_lbl_timer, 310, 210);

    lv_obj_t *btn_start = lv_button_create(card);
    lv_obj_add_style(btn_start, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_start, 120, 44);
    lv_obj_set_pos(btn_start, 230, 290);
    lv_obj_set_style_bg_color(btn_start, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_add_event_cb(btn_start, timer_start_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_st = lv_label_create(btn_start);
    lv_label_set_text(lbl_st, "開始 / 停止");
    lv_obj_set_style_text_font(lbl_st, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(lbl_st, lv_color_hex(0x0C0F17), 0);
    lv_obj_center(lbl_st);

    lv_obj_t *btn_rst = lv_button_create(card);
    lv_obj_add_style(btn_rst, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_rst, 120, 44);
    lv_obj_set_pos(btn_rst, 380, 290);
    lv_obj_add_event_cb(btn_rst, timer_reset_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_rt = lv_label_create(btn_rst);
    lv_label_set_text(lbl_rt, "リセット");
    lv_obj_set_style_text_font(lbl_rt, UI_FONT_REGULAR, 0);
    lv_obj_center(lbl_rt);

    return scr;
}

/* -------------------------------------------------------------
 * 5. Japanese Soroban Calculator Screen (和風の算盤)
 * ------------------------------------------------------------- */
static lv_obj_t *s_lbl_calc_screen = NULL;
static char s_calc_buf[64] = "0";
static double s_calc_val = 0.0;
static char s_calc_op = '\0';
static bool s_calc_new_num = true;

static void calc_btn_cb(lv_event_t *e)
{
    const char *key = (const char *)lv_event_get_user_data(e);
    if (!key) return;

    if (strcmp(key, "AC") == 0) {
        strcpy(s_calc_buf, "0");
        s_calc_val = 0.0;
        s_calc_op = '\0';
        s_calc_new_num = true;
    } else if (key[0] >= '0' && key[0] <= '9') {
        if (s_calc_new_num || strcmp(s_calc_buf, "0") == 0) {
            snprintf(s_calc_buf, sizeof(s_calc_buf), "%s", key);
            s_calc_new_num = false;
        } else {
            if (strlen(s_calc_buf) < 14) {
                strcat(s_calc_buf, key);
            }
        }
    } else if (strcmp(key, ".") == 0) {
        if (s_calc_new_num) {
            strcpy(s_calc_buf, "0.");
            s_calc_new_num = false;
        } else if (!strchr(s_calc_buf, '.') && strlen(s_calc_buf) < 13) {
            strcat(s_calc_buf, ".");
        }
    } else if (key[0] == '+' || key[0] == '-' || key[0] == '*' || key[0] == '/') {
        s_calc_val = atof(s_calc_buf);
        s_calc_op = key[0];
        s_calc_new_num = true;
    } else if (strcmp(key, "=") == 0) {
        double cur = atof(s_calc_buf);
        double res = cur;
        if (s_calc_op == '+') res = s_calc_val + cur;
        else if (s_calc_op == '-') res = s_calc_val - cur;
        else if (s_calc_op == '*') res = s_calc_val * cur;
        else if (s_calc_op == '/') {
            if (fabs(cur) < 1e-9) {
                strcpy(s_calc_buf, "0");
                s_calc_val = 0.0;
                s_calc_op = '\0';
                s_calc_new_num = true;
                if (s_lbl_calc_screen) lv_label_set_text(s_lbl_calc_screen, "エラー");
                return;
            }
            res = s_calc_val / cur;
        }
        snprintf(s_calc_buf, sizeof(s_calc_buf), "%.4f", res);
        char *p = strchr(s_calc_buf, '.');
        if (p) {
            char *end = s_calc_buf + strlen(s_calc_buf) - 1;
            while (end > p && *end == '0') {
                *end-- = '\0';
            }
            if (end == p) *p = '\0';
        }
        s_calc_new_num = true;
        s_calc_op = '\0';
    }

    if (s_lbl_calc_screen) {
        lv_label_set_text(s_lbl_calc_screen, s_calc_buf);
    }
}

lv_obj_t *ui_calculator_screen_create(ui_home_btn_cb_t home_cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_screen_header(scr, "和風の算盤 (Japanese Abacus Calculator)", home_cb);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_add_style(card, &ui_style_glass_card, 0);
    lv_obj_set_size(card, 520, 412);
    lv_obj_set_pos(card, 140, 56);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Calculator LCD Display */
    lv_obj_t *screen_box = lv_obj_create(card);
    lv_obj_set_size(screen_box, 360, 60);
    lv_obj_set_pos(screen_box, 10, 10);
    lv_obj_set_style_bg_color(screen_box, lv_color_hex(0x06090E), 0);
    lv_obj_set_style_border_color(screen_box, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_border_width(screen_box, 1, 0);
    lv_obj_set_style_radius(screen_box, 8, 0);

    s_lbl_calc_screen = lv_label_create(screen_box);
    lv_label_set_text(s_lbl_calc_screen, "0");
    lv_obj_set_style_text_color(s_lbl_calc_screen, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_calc_screen, UI_FONT_LARGE, 0);
    lv_obj_align(s_lbl_calc_screen, LV_ALIGN_RIGHT_MID, -12, 0);

    /* Dedicated AC Button next to LCD */
    lv_obj_t *btn_ac = lv_button_create(card);
    lv_obj_set_size(btn_ac, 110, 60);
    lv_obj_set_pos(btn_ac, 382, 10);
    lv_obj_set_style_radius(btn_ac, 8, 0);
    lv_obj_set_style_bg_color(btn_ac, UI_COLOR_RED_ACCENT, 0);
    lv_obj_set_style_border_width(btn_ac, 0, 0);
    lv_obj_add_event_cb(btn_ac, calc_btn_cb, LV_EVENT_CLICKED, (void *)"AC");

    lv_obj_t *lbl_ac = lv_label_create(btn_ac);
    lv_label_set_text(lbl_ac, "AC");
    lv_obj_set_style_text_font(lbl_ac, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(lbl_ac, lv_color_hex(0x0C0F17), 0);
    lv_obj_center(lbl_ac);

    /* 4x4 Keypad */
    const char *keys[16] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "0", ".", "=", "+"
    };

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int idx = r * 4 + c;
            lv_obj_t *btn = lv_button_create(card);
            lv_obj_set_size(btn, 110, 60);
            lv_obj_set_pos(btn, 10 + c * 124, 85 + r * 72);
            lv_obj_set_style_radius(btn, 8, 0);
            if (keys[idx][0] == '=') {
                lv_obj_set_style_bg_color(btn, UI_COLOR_GOLD_ACCENT, 0);
            } else if (keys[idx][0] == '+' || keys[idx][0] == '-' ||
                       keys[idx][0] == '*' || keys[idx][0] == '/') {
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x1F2937), 0);
            } else {
                lv_obj_set_style_bg_color(btn, UI_COLOR_KEY_WHITE, 0);
            }
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x2D3748), 0);
            lv_obj_add_event_cb(btn, calc_btn_cb, LV_EVENT_CLICKED, (void *)keys[idx]);

            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, keys[idx]);
            lv_obj_set_style_text_font(lbl, UI_FONT_LARGE, 0);
            if (keys[idx][0] == '=') {
                lv_obj_set_style_text_color(lbl, lv_color_hex(0x0C0F17), 0);
            } else {
                lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_TITLE, 0);
            }
            lv_obj_center(lbl);
        }
    }

    return scr;
}

/* -------------------------------------------------------------
 * 6. Food Expiration Tracker Screen (百鬼の台所)
 * ------------------------------------------------------------- */
lv_obj_t *ui_food_screen_create(ui_home_btn_cb_t home_cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DARK, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_screen_header(scr, "百鬼の台所 (Food Freshness Tracker)", home_cb);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_add_style(card, &ui_style_glass_card, 0);
    lv_obj_set_size(card, 768, 412);
    lv_obj_set_pos(card, 16, 56);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 8, 0);

    const struct {
        const char *name;
        const char *days;
        lv_color_t color;
    } foods[5] = {
        {"妖怪豆腐 (Yokai Tofu)", "残り 2 日 [要注意]", UI_COLOR_RED_ACCENT},
        {"清流山女魚 (Mountain Trout)", "残り 4 日 [新鮮]", UI_COLOR_CYAN_ACCENT},
        {"笹団子 (Sasa Dango)", "残り 7 日 [良好]", UI_COLOR_GOLD_ACCENT},
        {"天狗の生姜 (Tengu Ginger)", "残り 12 日 [新鮮]", UI_COLOR_CYAN_ACCENT},
        {"秘伝鬼味噌 (Demon Miso)", "残り 30 日 [熟成]", UI_COLOR_GOLD_ACCENT},
    };

    for (int i = 0; i < 5; i++) {
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, 740, 52);
        lv_obj_set_style_bg_color(row, UI_COLOR_KEY_WHITE, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x2D3748), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_name = lv_label_create(row);
        lv_label_set_text(lbl_name, foods[i].name);
        lv_obj_set_style_text_color(lbl_name, UI_COLOR_TEXT_TITLE, 0);
        lv_obj_set_style_text_font(lbl_name, UI_FONT_REGULAR, 0);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 12, 0);

        lv_obj_t *lbl_days = lv_label_create(row);
        lv_label_set_text(lbl_days, foods[i].days);
        lv_obj_set_style_text_color(lbl_days, foods[i].color, 0);
        lv_obj_set_style_text_font(lbl_days, UI_FONT_REGULAR, 0);
        lv_obj_align(lbl_days, LV_ALIGN_RIGHT_MID, -12, 0);
    }

    return scr;
}

void ui_apps_tick_periodic(void)
{
    /* Update big clock */
    if (s_lbl_clock_big) {
        time_t now = time(NULL);
        struct tm local = {0};
        char time_buf[32] = "12:00:00";
        if (localtime_r(&now, &local) != NULL && local.tm_year >= 120) {
            snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
                     local.tm_hour, local.tm_min, local.tm_sec);
        } else {
            uint32_t sec = lv_tick_get() / 1000;
            uint32_t s = sec % 60;
            uint32_t m = (sec / 60) % 60;
            uint32_t h = (12 + sec / 3600) % 24;
            snprintf(time_buf, sizeof(time_buf), "%02u:%02u:%02u",
                     (unsigned)h, (unsigned)m, (unsigned)s);
        }
        lv_label_set_text(s_lbl_clock_big, time_buf);
    }

    /* Update timer */
    if (s_timer_running && s_timer_seconds > 0) {
        static uint32_t s_last_timer_tick = 0;
        uint32_t now_tick = lv_tick_get();
        if (now_tick - s_last_timer_tick >= 1000) {
            s_last_timer_tick = now_tick;
            s_timer_seconds--;
            if (s_lbl_timer) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%02d:%02d", s_timer_seconds / 60, s_timer_seconds % 60);
                lv_label_set_text(s_lbl_timer, buf);
            }
        }
    }
}
