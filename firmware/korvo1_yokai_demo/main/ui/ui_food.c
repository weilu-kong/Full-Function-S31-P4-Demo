#include "ui/ui_apps.h"
#include "ui/ui_food.h"
#include "ui/ui_theme.h"
#include "food_service.h"
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define FOOD_PAGE_SIZE 6
static lv_obj_t *s_screen, *s_content, *s_list, *s_clock, *s_status, *s_page_label;
static lv_obj_t *s_modal, *s_name, *s_expiry, *s_keyboard, *s_error, *s_delete_label;
static size_t s_page, s_edit_index;
static bool s_active, s_synced, s_delete_armed;
static int s_today;
static food_result_t s_load_result;
static ui_home_btn_cb_t s_home_cb;
static void home_cb(lv_event_t *event)
{
    (void)event;
    if (s_home_cb) s_home_cb();
}
/* Native LVGL keyboard map: date digits and separator are available together. */
static const char *const s_date_keys[] = {"1", "2", "3", "-", "\n", "4", "5", "6", LV_SYMBOL_BACKSPACE, "\n",
    "7", "8", "9", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "\n", LV_SYMBOL_CLOSE, "0", LV_SYMBOL_OK, ""};
static const lv_buttonmatrix_ctrl_t s_date_controls[] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
static void refresh_list(void);
static void open_editor(size_t index);

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(obj, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_set_pos(obj, x, y);
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int width,
                        lv_event_cb_t cb, void *data)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_add_style(obj, &ui_style_glass_card, 0);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, width, 40);
    lv_obj_t *caption = label(obj, text, 0, 0); lv_obj_center(caption);
    if (cb) { ui_add_click_sfx(obj); lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, data); }
    return obj;
}

static void close_editor(void)
{
    if (s_modal) lv_obj_delete(s_modal);
    s_modal = s_name = s_expiry = s_keyboard = s_error = s_delete_label = NULL;
    s_delete_armed = false;
}
static void cancel_cb(lv_event_t *event) { (void)event; close_editor(); }
static void save_cb(lv_event_t *event)
{
    (void)event;
    food_result_t result = s_edit_index == SIZE_MAX ?
        food_service_add(lv_textarea_get_text(s_name), lv_textarea_get_text(s_expiry)) :
        food_service_edit(s_edit_index, lv_textarea_get_text(s_name), lv_textarea_get_text(s_expiry));
    if (result != FOOD_OK) { lv_label_set_text(s_error, food_service_error(result)); return; }
    close_editor(); refresh_list();
}
static void delete_cb(lv_event_t *event)
{
    (void)event;
    if (!s_delete_armed) {
        s_delete_armed = true;
        lv_label_set_text(s_error, "この食品を削除します。「削除を確定」で保存データから削除");
        lv_label_set_text(s_delete_label, "削除を確定");
        return;
    }
    food_result_t result = food_service_delete(s_edit_index);
    if (result != FOOD_OK) { lv_label_set_text(s_error, food_service_error(result)); return; }
    close_editor(); refresh_list();
}
static void focus_cb(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target(event);
    lv_keyboard_set_textarea(s_keyboard, target);
    lv_keyboard_set_mode(s_keyboard, target == s_expiry ? LV_KEYBOARD_MODE_USER_1 : LV_KEYBOARD_MODE_TEXT_LOWER);
    if (s_delete_armed) { s_delete_armed = false; lv_label_set_text(s_delete_label, "削除"); lv_label_set_text(s_error, ""); }
}
static void row_cb(lv_event_t *event) { open_editor((size_t)(uintptr_t)lv_event_get_user_data(event)); }
static void add_cb(lv_event_t *event) { (void)event; open_editor(SIZE_MAX); }
static void page_cb(lv_event_t *event)
{
    if ((uintptr_t)lv_event_get_user_data(event)) {
        if ((s_page + 1) * FOOD_PAGE_SIZE < food_service_count()) ++s_page;
    } else if (s_page) --s_page;
    refresh_list();
}

static void open_editor(size_t index)
{
    if (s_load_result != FOOD_OK) return;
    close_editor(); s_edit_index = index;
    s_modal = lv_obj_create(s_screen);
    lv_obj_set_pos(s_modal, 0, 0); lv_obj_set_size(s_modal, 800, 480);
    lv_obj_add_style(s_modal, &ui_style_glass_card, 0);
    lv_obj_set_style_pad_all(s_modal, 0, 0);
    lv_obj_set_scrollable(s_modal, false);
    label(s_modal, index == SIZE_MAX ? "食品を追加" : "食品を編集", 20, 12);
    label(s_modal, "名前 (最大48バイト)", 20, 50);
    label(s_modal, "期限 YYYY-MM-DD", 20, 105);
    const food_record_t *record = food_service_get(index);
    s_name = lv_textarea_create(s_modal);
    lv_obj_set_pos(s_name, 220, 40); lv_obj_set_size(s_name, 550, 46);
    lv_textarea_set_one_line(s_name, true);
    lv_textarea_set_max_length(s_name, FOOD_NAME_BYTES);
    lv_obj_set_style_text_font(s_name, UI_FONT_REGULAR, 0);
    lv_textarea_set_text(s_name, record ? record->name : "");
    s_expiry = lv_textarea_create(s_modal);
    lv_obj_set_pos(s_expiry, 220, 95); lv_obj_set_size(s_expiry, 550, 46);
    lv_textarea_set_one_line(s_expiry, true); lv_textarea_set_max_length(s_expiry, 10);
    lv_textarea_set_accepted_chars(s_expiry, "0123456789-");
    lv_textarea_set_placeholder_text(s_expiry, "YYYY-MM-DD");
    lv_textarea_set_text(s_expiry, record ? record->expiry : "");
    s_error = label(s_modal, "", 20, 148); lv_obj_set_width(s_error, 750);
    lv_obj_set_style_text_color(s_error, UI_COLOR_RED_ACCENT, 0);
    button(s_modal, "保存", 20, 192, 120, save_cb, NULL);
    button(s_modal, "キャンセル", 150, 192, 150, cancel_cb, NULL);
    if (record) {
        lv_obj_t *del = button(s_modal, "削除", 610, 192, 160, delete_cb, NULL);
        s_delete_label = lv_obj_get_child(del, 0);
    }
    s_keyboard = lv_keyboard_create(s_modal);
    lv_obj_set_pos(s_keyboard, 20, 245); lv_obj_set_size(s_keyboard, 750, 220);
    lv_keyboard_set_map(s_keyboard, LV_KEYBOARD_MODE_USER_1, s_date_keys, s_date_controls);
    lv_keyboard_set_textarea(s_keyboard, s_name);
    lv_obj_add_event_cb(s_name, focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_expiry, focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_keyboard, cancel_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(s_keyboard, save_cb, LV_EVENT_READY, NULL);
}

static void refresh_list(void)
{
    if (!s_list) return;
    size_t count = food_service_count();
    if (s_page * FOOD_PAGE_SIZE >= count) s_page = count ? (count - 1) / FOOD_PAGE_SIZE : 0;
    lv_obj_clean(s_list);
    if (!count) {
        lv_obj_t *empty = label(s_list, "食品は未登録です。「追加」から名前と期限を登録してください", 0, 0);
        lv_obj_set_style_text_font(empty, UI_FONT_SMALL, 0);
    }
    for (size_t i = s_page * FOOD_PAGE_SIZE; i < count && i < (s_page + 1) * FOOD_PAGE_SIZE; ++i) {
        const food_record_t *record = food_service_get(i);
        char text[150];
        int expiry;
        if (s_synced && food_date_ordinal(record->expiry, &expiry)) {
            int days = expiry - s_today;
            if (days < 0) snprintf(text, sizeof(text), "%s    %s    %d日前に期限切れ", record->name, record->expiry, -days);
            else if (!days) snprintf(text, sizeof(text), "%s    %s    本日期限", record->name, record->expiry);
            else snprintf(text, sizeof(text), "%s    %s    残り%d日", record->name, record->expiry, days);
        } else snprintf(text, sizeof(text), "%s    %s    日付未同期", record->name, record->expiry);
        lv_obj_t *row = lv_button_create(s_list);
        lv_obj_add_style(row, &ui_style_glass_card, 0);
        lv_obj_set_size(row, 740, 47);
        lv_obj_t *caption = label(row, text, 0, 0);
        lv_obj_set_width(caption, 715);
        lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_center(caption);
        ui_add_click_sfx(row);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        if (s_load_result != FOOD_OK) lv_obj_add_state(row, LV_STATE_DISABLED);
    }
    lv_label_set_text_fmt(s_page_label, "%u / %u ページ    %u / 32 件", (unsigned)s_page + 1,
                          (unsigned)(count ? (count + FOOD_PAGE_SIZE - 1) / FOOD_PAGE_SIZE : 1), (unsigned)count);
}

lv_obj_t *ui_food_screen_create(ui_home_btn_cb_t home_callback)
{
    s_home_cb = home_callback;
    s_screen = ui_screen_create();
    lv_obj_set_size(s_screen, 800, 480);
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG_DARK, 0);
    lv_obj_set_scrollable(s_screen, false);
    button(s_screen, "ホーム", 16, 12, 106, home_cb, NULL);
    label(s_screen, "百鬼の台所 (Food Freshness Tracker)", 134, 20);
    return s_screen;
}

void ui_food_set_active(bool active)
{
    if (!s_screen || s_active == active) return;
    s_active = active;
    if (!active) {
        close_editor();
        if (s_content) lv_obj_delete(s_content);
        s_content = s_list = s_clock = s_status = s_page_label = NULL;
        return;
    }
    s_load_result = food_service_init();
    s_content = lv_obj_create(s_screen);
    lv_obj_add_style(s_content, &ui_style_glass_card, 0);
    lv_obj_set_pos(s_content, 16, 62); lv_obj_set_size(s_content, 768, 406);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_set_scrollable(s_content, false);
    s_clock = label(s_content, "日付未同期 — Wi-Fiで時刻を同期してください", 12, 10);
    s_status = label(s_content, food_service_error(s_load_result), 12, 36);
    lv_obj_set_width(s_status, 585); lv_obj_set_style_text_color(s_status, UI_COLOR_RED_ACCENT, 0);
    lv_obj_t *add = button(s_content, "追加", 638, 12, 110, add_cb, NULL);
    if (s_load_result != FOOD_OK) lv_obj_add_state(add, LV_STATE_DISABLED);
    s_list = lv_obj_create(s_content);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 0, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_pos(s_list, 10, 72); lv_obj_set_size(s_list, 748, 285);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    s_page_label = label(s_content, "", 170, 375);
    button(s_content, "前へ", 10, 360, 120, page_cb, NULL);
    button(s_content, "次へ", 628, 360, 120, page_cb, (void *)(uintptr_t)1);
    s_page = 0; s_today = -1; s_synced = false;
    ui_food_tick(); refresh_list();
}

void ui_food_tick(void)
{
    if (!s_active || !s_clock) return;
    time_t now = time(NULL); struct tm local;
    char date[11]; int today = -1;
    bool synced = localtime_r(&now, &local) && strftime(date, sizeof(date), "%Y-%m-%d", &local) == 10 && food_date_ordinal(date, &today);
    if (synced == s_synced && today == s_today) return;
    s_synced = synced; s_today = today;
    if (synced) lv_label_set_text_fmt(s_clock, "今日 %s    期限は登録した日付で管理", date);
    else lv_label_set_text(s_clock, "日付未同期 — Wi-Fiで時刻を同期してください");
    refresh_list();
}
