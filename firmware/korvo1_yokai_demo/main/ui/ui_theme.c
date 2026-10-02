#include "ui/ui_theme.h"
#include "synth_service.h"

lv_style_t ui_style_glass_card;
lv_style_t ui_style_glass_card_pressed;
lv_style_t ui_style_accent_border;
lv_style_t ui_style_pill_badge;
lv_style_t ui_style_btn_home;

void ui_theme_init(void)
{
    /* Dark Japanese Sleek Card Style - Optimized for 60 FPS Swiping */
    lv_style_init(&ui_style_glass_card);
    lv_style_set_bg_color(&ui_style_glass_card, lv_color_hex(0x131926));
    lv_style_set_bg_opa(&ui_style_glass_card, LV_OPA_COVER);
    lv_style_set_radius(&ui_style_glass_card, 14);
    lv_style_set_border_width(&ui_style_glass_card, 1);
    lv_style_set_border_color(&ui_style_glass_card, UI_COLOR_GOLD_ACCENT);
    lv_style_set_border_opa(&ui_style_glass_card, LV_OPA_30);
    lv_style_set_shadow_width(&ui_style_glass_card, 0);

    /* Pressed Feedback */
    lv_style_init(&ui_style_glass_card_pressed);
    lv_style_set_bg_color(&ui_style_glass_card_pressed, lv_color_hex(0x1F283C));
    lv_style_set_bg_opa(&ui_style_glass_card_pressed, LV_OPA_COVER);
    lv_style_set_border_color(&ui_style_glass_card_pressed, UI_COLOR_GOLD_ACCENT);
    lv_style_set_border_opa(&ui_style_glass_card_pressed, LV_OPA_90);
    lv_style_set_shadow_width(&ui_style_glass_card_pressed, 0);
    lv_style_set_transform_scale(&ui_style_glass_card_pressed, 250); /* Subtle click depth */

    /* Accent Border */
    lv_style_init(&ui_style_accent_border);
    lv_style_set_border_width(&ui_style_accent_border, 2);
    lv_style_set_border_color(&ui_style_accent_border, UI_COLOR_GOLD_ACCENT);
    lv_style_set_border_opa(&ui_style_accent_border, LV_OPA_60);

    /* Pill Badge */
    lv_style_init(&ui_style_pill_badge);
    lv_style_set_bg_color(&ui_style_pill_badge, lv_color_hex(0x1F2430));
    lv_style_set_bg_opa(&ui_style_pill_badge, LV_OPA_70);
    lv_style_set_radius(&ui_style_pill_badge, 12);
    lv_style_set_border_width(&ui_style_pill_badge, 1);
    lv_style_set_border_color(&ui_style_pill_badge, UI_COLOR_GOLD_ACCENT);
    lv_style_set_border_opa(&ui_style_pill_badge, LV_OPA_40);
    lv_style_set_text_color(&ui_style_pill_badge, UI_COLOR_GOLD_ACCENT);
    lv_style_set_text_font(&ui_style_pill_badge, UI_FONT_SMALL);
    lv_style_set_pad_hor(&ui_style_pill_badge, 10);
    lv_style_set_pad_ver(&ui_style_pill_badge, 4);

    /* Home Button */
    lv_style_init(&ui_style_btn_home);
    lv_style_set_bg_color(&ui_style_btn_home, lv_color_hex(0x1A1F2C));
    lv_style_set_bg_opa(&ui_style_btn_home, LV_OPA_70);
    lv_style_set_radius(&ui_style_btn_home, 14);
    lv_style_set_border_width(&ui_style_btn_home, 1);
    lv_style_set_border_color(&ui_style_btn_home, UI_COLOR_GOLD_ACCENT);
    lv_style_set_border_opa(&ui_style_btn_home, LV_OPA_50);
    lv_style_set_text_color(&ui_style_btn_home, UI_COLOR_TEXT_TITLE);
    lv_style_set_text_font(&ui_style_btn_home, UI_FONT_SMALL);
    lv_style_set_pad_hor(&ui_style_btn_home, 12);
    lv_style_set_pad_ver(&ui_style_btn_home, 6);
}

static void click_sfx_cb(lv_event_t *event)
{
    /* Bubbled events must not create a second sound on the parent. */
    if (lv_event_get_target(event) == lv_event_get_current_target(event))
        synth_service_play_sfx(SYNTH_SFX_CLICK);
}

void ui_add_click_sfx(lv_obj_t *control)
{
    lv_obj_add_event_cb(control, click_sfx_cb, LV_EVENT_CLICKED, NULL);
}

/* Retain the existing 800x480 layout inside a larger native display. */
lv_obj_t *ui_content_create(lv_obj_t *parent)
{
    lv_display_t *display = lv_display_get_default();
    if (lv_display_get_horizontal_resolution(display) == 800 &&
        lv_display_get_vertical_resolution(display) == 480) return parent;
    lv_obj_t *content = lv_obj_create(parent);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 800, 480);
    lv_obj_center(content);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_CLICKABLE);
    return content;
}

lv_obj_t *ui_screen_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, UI_COLOR_BG_DARK, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return ui_content_create(screen);
}
