#include "ui/ui_fireworks.h"
#include "ui/ui_image_loader.h"
#include "ui/ui_theme.h"
#include "fireworks_engine.h"
#include <stdint.h>
#include <stdio.h>

static ui_home_btn_cb_t s_home_cb;
static lv_obj_t *s_art;
static lv_obj_t *s_count;
static lv_obj_t *s_style_btn[3];
static lv_obj_t *s_auto_btn;
static bool s_active;
static uint32_t s_last_tick;
static lv_point_t s_press_pt;

static void set_btn_selected(lv_obj_t *btn, bool selected, bool cyan)
{
    if (!btn) return;
    lv_obj_set_style_bg_color(btn,
        selected ? (cyan ? lv_color_hex(0x0F6377) : lv_color_hex(0x5E481F))
                 : lv_color_hex(0x101B28), 0);
    lv_obj_set_style_bg_opa(btn, selected ? LV_OPA_90 : LV_OPA_70, 0);
    lv_obj_set_style_border_color(btn,
        cyan ? UI_COLOR_CYAN_ACCENT : UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_border_opa(btn, selected ? LV_OPA_COVER : LV_OPA_40, 0);
}

static void sync_buttons(void)
{
    fireworks_stats_t st;
    fireworks_engine_get_stats(&st);
    for (int i = 0; i < 3; ++i)
        set_btn_selected(s_style_btn[i], st.selected_style == i, false);
    set_btn_selected(s_auto_btn, st.auto_mode, true);
}

static void home_cb(lv_event_t *e)
{
    (void)e;
    if (s_home_cb) s_home_cb();
}

static lv_obj_t *make_pill(lv_obj_t *parent, int x, const char *text)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, 422);
    lv_obj_set_size(b, 132, 48);
    lv_obj_set_style_radius(b, 24, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_set_style_border_opa(b, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x101B28), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, 0);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(l, UI_COLOR_TEXT_TITLE, 0);
    lv_obj_center(l);
    return b;
}

static void mode_cb(lv_event_t *e)
{
    intptr_t id = (intptr_t)lv_event_get_user_data(e);
    if (id >= 0 && id <= 2) {
        fireworks_engine_set_style((fireworks_style_t)id);
    } else {
        fireworks_stats_t st;
        fireworks_engine_get_stats(&st);
        fireworks_engine_set_auto(!st.auto_mode);
    }
    sync_buttons();
}

static void touch_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        s_press_pt = p;
    } else if (code == LV_EVENT_RELEASED) {
        int32_t dx = p.x - s_press_pt.x;
        int32_t dy = p.y - s_press_pt.y;
        if (dx < 0) dx = -dx;

        if (dy < -45 && (-dy) > dx) {
            float target = (float)p.y;
            if (target < 90) target = 90;
            if (target > 315) target = 315;
            fireworks_engine_launch((float)s_press_pt.x, 400.0f, target);
        } else {
            float y = (float)p.y;
            if (y < 78) y = 78;
            if (y > 385) y = 385;
            fireworks_engine_tap((float)p.x, y);
        }
        lv_obj_invalidate(s_art);
    }
}

static void draw_particles(lv_layer_t *layer)
{
    size_t count = 0;
    const fw_particle_t *p = fireworks_engine_particles(&count);
    for (size_t i = 0; i < count; ++i) {
        if (!p[i].active) continue;
        float t = p[i].age_s / p[i].life_s;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        lv_opa_t opa = (lv_opa_t)(255.0f * (1.0f - t));

        if (p[i].style == FW_STYLE_KIKU) {
            /* 1. Kiku: Golden Chrysanthemum with sparkling comet trail */
            lv_draw_line_dsc_t ld;
            lv_draw_line_dsc_init(&ld);
            ld.color = lv_color_hex(p[i].rgb888);
            ld.opa = opa;
            ld.width = 2;
            ld.round_start = 0;
            ld.round_end = 0;
            ld.p1.x = (int32_t)p[i].prev_x;
            ld.p1.y = (int32_t)p[i].prev_y;
            ld.p2.x = (int32_t)p[i].x;
            ld.p2.y = (int32_t)p[i].y;
            lv_draw_line(layer, &ld);

            /* Sparkling bright head spark */
            int32_t r = p[i].size;
            lv_area_t a = {
                (int32_t)p[i].x - r, (int32_t)p[i].y - r,
                (int32_t)p[i].x + r, (int32_t)p[i].y + r
            };
            lv_draw_rect_dsc_t rd;
            lv_draw_rect_dsc_init(&rd);
            rd.bg_color = (t > 0.65f && (((int)(p[i].age_s * 30)) & 1))
                          ? lv_color_hex(0xFFFFFF) : lv_color_hex(p[i].rgb888);
            rd.bg_opa = opa;
            rd.radius = (r >= 3) ? 1 : 0;
            lv_draw_rect(layer, &rd, &a);

        } else if (p[i].style == FW_STYLE_BOTAN) {
            /* 2. Botan: Dual-shell Glowing Spherical Peony Petals */
            int32_t r = p[i].size;
            lv_draw_rect_dsc_t cd;
            lv_draw_rect_dsc_init(&cd);
            cd.bg_color = (p[i].sub_type == 1) ? lv_color_hex(0xFFFFFF) : lv_color_hex(p[i].rgb888);
            cd.bg_opa = opa;
            cd.radius = 1;
            lv_area_t ca = {
                (int32_t)p[i].x - r, (int32_t)p[i].y - r,
                (int32_t)p[i].x + r, (int32_t)p[i].y + r
            };
            lv_draw_rect(layer, &cd, &ca);

        } else {
            /* 3. Yanagi: Weeping Willow Cascading Streaks with Silver/Cyan Shimmer */
            lv_draw_line_dsc_t ld;
            lv_draw_line_dsc_init(&ld);
            ld.color = lv_color_hex(p[i].rgb888);
            ld.opa = (lv_opa_t)(opa * 0.85f);
            ld.width = 2;
            ld.round_start = 0;
            ld.round_end = 0;
            ld.p1.x = (int32_t)p[i].x;
            ld.p1.y = (int32_t)p[i].y;
            ld.p2.x = (int32_t)(p[i].prev_x - p[i].vx * 0.04f);
            ld.p2.y = (int32_t)(p[i].prev_y - p[i].vy * 0.04f);
            lv_draw_line(layer, &ld);

            /* Shimmer tip */
            lv_draw_rect_dsc_t rd;
            lv_draw_rect_dsc_init(&rd);
            rd.bg_color = lv_color_hex(0xFFFFFF);
            rd.bg_opa = (lv_opa_t)(opa * 0.7f);
            rd.radius = 0;
            lv_area_t ra = {
                (int32_t)p[i].x - 1, (int32_t)p[i].y - 1,
                (int32_t)p[i].x + 1, (int32_t)p[i].y + 1
            };
            lv_draw_rect(layer, &rd, &ra);
        }
    }

    const fw_rocket_t *r = fireworks_engine_rockets(&count);
    for (size_t i = 0; i < count; ++i) {
        if (!r[i].active) continue;
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = UI_COLOR_GOLD_ACCENT;
        d.opa = LV_OPA_80;
        d.width = 2;
        d.round_start = 0;
        d.round_end = 0;
        d.p1.x = (int32_t)r[i].x;
        d.p1.y = (int32_t)r[i].y;
        d.p2.x = (int32_t)r[i].x;
        d.p2.y = (int32_t)r[i].y + 18;
        lv_draw_line(layer, &d);
    }
}

static void art_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    draw_particles(layer);
}

lv_obj_t *ui_fireworks_screen_create(ui_home_btn_cb_t home_cb_fn)
{
    s_home_cb = home_cb_fn;
    fireworks_engine_init();

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x020610), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_art = lv_obj_create(scr);
    lv_obj_set_pos(s_art, 0, 0);
    lv_obj_set_size(s_art, 800, 480);
    lv_obj_set_style_bg_opa(s_art, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_art, 0, 0);
    lv_obj_remove_flag(s_art, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_art, art_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    lv_obj_t *touch = lv_obj_create(scr);
    lv_obj_set_pos(touch, 0, 64);
    lv_obj_set_size(touch, 800, 344);
    lv_obj_set_style_bg_opa(touch, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch, 0, 0);
    lv_obj_add_flag(touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(touch, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(touch, touch_cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *home = lv_button_create(scr);
    lv_obj_add_style(home, &ui_style_btn_home, 0);
    lv_obj_set_pos(home, 18, 16);
    lv_obj_set_size(home, 112, 44);
    lv_obj_add_event_cb(home, home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hl = lv_label_create(home);
    lv_label_set_text(hl, "< ホーム");
    lv_obj_set_style_text_font(hl, UI_FONT_SMALL, 0);
    lv_obj_center(hl);

    s_count = lv_label_create(scr);
    lv_label_set_text(s_count, "打上数: 0 発");
    lv_obj_set_style_text_font(s_count, UI_FONT_REGULAR, 0);
    lv_obj_set_style_text_color(s_count, UI_COLOR_GOLD_ACCENT, 0);
    lv_obj_align(s_count, LV_ALIGN_TOP_RIGHT, -22, 22);

    /* Bottom pills evenly spaced: width 132 each, centered horizontally */
    s_style_btn[0] = make_pill(scr, 106, "菊");
    s_style_btn[1] = make_pill(scr, 258, "牡丹");
    s_style_btn[2] = make_pill(scr, 410, "柳");
    s_auto_btn = make_pill(scr, 562, "AUTO");

    for (int i = 0; i < 3; ++i)
        lv_obj_add_event_cb(s_style_btn[i], mode_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    lv_obj_add_event_cb(s_auto_btn, mode_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)99);

    sync_buttons();
    return scr;
}

void ui_fireworks_set_active(bool active)
{
    s_active = active;
    if (active) {
        s_last_tick = lv_tick_get();
    } else {
        fireworks_engine_set_auto(false);
        fireworks_engine_reset_visuals();
        sync_buttons();
    }
}

void ui_fireworks_tick(void)
{
    if (!s_active || !s_art) return;

    uint32_t now = lv_tick_get();
    uint32_t dt_ms = now - s_last_tick;
    s_last_tick = now;
    if (dt_ms > 50) dt_ms = 50;

    fireworks_engine_update((float)dt_ms / 1000.0f);

    fireworks_stats_t st;
    fireworks_engine_get_stats(&st);
    static uint32_t s_last_launches = 0xFFFFFFFF;
    if (s_count && st.launches != s_last_launches) {
        s_last_launches = st.launches;
        char buf[40];
        snprintf(buf, sizeof(buf), "打上数: %lu 発",
                 (unsigned long)st.launches);
        lv_label_set_text(s_count, buf);
    }
    lv_obj_invalidate(s_art);
}
