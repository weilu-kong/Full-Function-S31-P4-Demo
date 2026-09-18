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
static lv_obj_t *s_lbl_vision_det = NULL;
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
static int s_vision_step = 0;

static void vision_btn_cb(lv_event_t *e)
{
    (void)e;
    s_vision_step = (s_vision_step + 1) % 3;
    const char *dets[] = {
        "[物体検出] 雪女の気配を検知 (信頼度 98%)",
        "[物体検出] 狸の置物を識別 (信頼度 94%)",
        "[物体検出] 障子に目目連が現れました (信頼度 99%)"
    };
    if (s_lbl_vision_det) {
        lv_label_set_text(s_lbl_vision_det, dets[s_vision_step]);
    }
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

    /* Viewfinder Area */
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
    lv_obj_set_pos(lbl_info, 560, 24);

    s_lbl_vision_det = lv_label_create(card);
    lv_label_set_text(s_lbl_vision_det, "[物体検出] 妖怪探索中…");
    lv_obj_set_style_text_color(s_lbl_vision_det, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_vision_det, UI_FONT_REGULAR, 0);
    lv_obj_set_width(s_lbl_vision_det, 180);
    lv_label_set_long_mode(s_lbl_vision_det, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_lbl_vision_det, 560, 70);

    lv_obj_t *btn_scan = lv_button_create(card);
    lv_obj_add_style(btn_scan, &ui_style_pill_badge, 0);
    lv_obj_set_size(btn_scan, 170, 48);
    lv_obj_set_pos(btn_scan, 560, 270);
    lv_obj_set_style_bg_color(btn_scan, UI_COLOR_CYAN_ACCENT, 0);
    lv_obj_add_event_cb(btn_scan, vision_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_scan = lv_label_create(btn_scan);
    lv_label_set_text(lbl_scan, "霊視スキャン");
    lv_obj_set_style_text_font(lbl_scan, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(lbl_scan, lv_color_hex(0x0C0F17), 0);
    lv_obj_center(lbl_scan);

    return scr;
}

static bool s_vision_active = false;

void ui_vision_set_active(bool active)
{
    s_vision_active = active;
    if (s_lbl_vision_det) {
        if (active) {
            lv_label_set_text(s_lbl_vision_det, "カメラ準備中…");
        } else {
            lv_label_set_text(s_lbl_vision_det, "[顔検知] 停止中");
            if (s_vf_img) {
                lv_obj_add_flag(s_vf_img, LV_OBJ_FLAG_HIDDEN);
            }
            if (s_lbl_vf_target) {
                lv_obj_remove_flag(s_lbl_vf_target, LV_OBJ_FLAG_HIDDEN);
            }
            for (int i = 0; i < VISION_MAX_DETECTIONS; i++) {
                if (s_face_boxes[i]) {
                    lv_obj_add_flag(s_face_boxes[i], LV_OBJ_FLAG_HIDDEN);
                }
                if (s_face_labels[i]) {
                    lv_obj_add_flag(s_face_labels[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }
}

void ui_vision_screen_update(void)
{
    if (!s_vision_active) {
        return;
    }

    /* 1. Consume fresh camera preview frame */
    const uint8_t *frame_data = NULL;
    uint16_t fw = 0, fh = 0;
    if (vision_service_get_preview_frame(&frame_data, &fw, &fh)) {
        if (s_vf_img && frame_data) {
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
        if (!s_lbl_vision_det) break;
        if (res.mode == VISION_MODE_FACE) {
            if (res.count == 0) {
                lv_label_set_text(s_lbl_vision_det, "[顔検知] 探索中…");
                for (int i = 0; i < VISION_MAX_DETECTIONS; i++) {
                    if (s_face_boxes[i]) lv_obj_add_flag(s_face_boxes[i], LV_OBJ_FLAG_HIDDEN);
                    if (s_face_labels[i]) lv_obj_add_flag(s_face_labels[i], LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "[顔検知] %d人検知 (%lums)",
                         res.count, (unsigned long)res.inference_ms);
                lv_label_set_text(s_lbl_vision_det, buf);

                for (int i = 0; i < VISION_MAX_DETECTIONS; i++) {
                    if (!s_face_boxes[i] || !s_face_labels[i]) continue;
                    if (i < res.count) {
                        /* Scale detector coordinates (320x240) to viewfinder (520x310) */
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

                        /* Place label pill badge above box or inside top if near top boundary */
                        int lbl_y = by - 24;
                        if (lbl_y < 2) {
                            lbl_y = by + 4;
                        }
                        int lbl_x = bx;
                        if (lbl_x > 400) lbl_x = 400; /* Prevent badge overflow on far right */
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
