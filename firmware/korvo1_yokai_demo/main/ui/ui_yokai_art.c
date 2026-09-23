#include "ui/ui_yokai_art.h"
#include "ui/ui_theme.h"
#include <stdint.h>

static inline int32_t ax(const lv_area_t *a, int32_t x) { return a->x1 + x; }
static inline int32_t ay(const lv_area_t *a, int32_t y) { return a->y1 + y; }

static void draw_rect(lv_layer_t *layer, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, lv_color_t color,
                      lv_opa_t opa, int32_t radius)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = color;
    d.bg_opa = opa;
    d.radius = radius;
    lv_area_t a = {x1, y1, x2, y2};
    lv_draw_rect(layer, &d, &a);
}

static void draw_line(lv_layer_t *layer, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, lv_color_t color,
                      lv_opa_t opa, int32_t width)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = color;
    d.opa = opa;
    d.width = width;
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = x1;
    d.p1.y = y1;
    d.p2.x = x2;
    d.p2.y = y2;
    lv_draw_line(layer, &d);
}

static void draw_triangle(lv_layer_t *layer,
                          int32_t x1, int32_t y1,
                          int32_t x2, int32_t y2,
                          int32_t x3, int32_t y3,
                          lv_color_t color, lv_opa_t opa)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = color;
    d.opa = opa;
    d.p[0].x = x1; d.p[0].y = y1;
    d.p[1].x = x2; d.p[1].y = y2;
    d.p[2].x = x3; d.p[2].y = y3;
    lv_draw_triangle(layer, &d);
}

static void draw_moon(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t r)
{
    draw_rect(layer, cx-r-10, cy-r-10, cx+r+10, cy+r+10,
              lv_color_hex(0xD7E6EF), LV_OPA_10, LV_RADIUS_CIRCLE);
    draw_rect(layer, cx-r-5, cy-r-5, cx+r+5, cy+r+5,
              lv_color_hex(0xE7E0C8), LV_OPA_20, LV_RADIUS_CIRCLE);
    draw_rect(layer, cx-r, cy-r, cx+r, cy+r,
              lv_color_hex(0xF2E2AE), LV_OPA_COVER, LV_RADIUS_CIRCLE);

    /* Cheap crater hints; no bitmap needed. */
    draw_rect(layer, cx-r/3, cy-r/5, cx-r/8, cy+r/12,
              lv_color_hex(0xD4C69D), LV_OPA_35, LV_RADIUS_CIRCLE);
    draw_rect(layer, cx+r/7, cy-r/3, cx+r/3, cy-r/7,
              lv_color_hex(0xC7B98F), LV_OPA_25, LV_RADIUS_CIRCLE);
    draw_rect(layer, cx+r/8, cy+r/7, cx+r/2, cy+r/3,
              lv_color_hex(0xD6CAA5), LV_OPA_25, LV_RADIUS_CIRCLE);
}

static void draw_mountain_band(lv_layer_t *layer, int32_t y_base,
                               lv_color_t back, lv_color_t front)
{
    const int32_t back_peaks[][2] = {
        {0, y_base}, {95, y_base-68}, {185, y_base}, {285, y_base-86},
        {390, y_base}, {500, y_base-72}, {610, y_base}, {700, y_base-92},
        {800, y_base}
    };
    for (int i = 0; i + 2 < (int)(sizeof(back_peaks)/sizeof(back_peaks[0])); i += 2) {
        draw_triangle(layer,
                      back_peaks[i][0], back_peaks[i][1],
                      back_peaks[i+1][0], back_peaks[i+1][1],
                      back_peaks[i+2][0], back_peaks[i+2][1],
                      back, LV_OPA_COVER);
    }

    const int32_t front_peaks[][2] = {
        {0, y_base+22}, {130, y_base-35}, {260, y_base+22},
        {390, y_base-50}, {535, y_base+22}, {675, y_base-42},
        {800, y_base+22}
    };
    for (int i = 0; i + 2 < (int)(sizeof(front_peaks)/sizeof(front_peaks[0])); i += 2) {
        draw_triangle(layer,
                      front_peaks[i][0], front_peaks[i][1],
                      front_peaks[i+1][0], front_peaks[i+1][1],
                      front_peaks[i+2][0], front_peaks[i+2][1],
                      front, LV_OPA_COVER);
    }
}

static void draw_lake(lv_layer_t *layer, int32_t y)
{
    draw_rect(layer, 0, y, 799, 479, lv_color_hex(0x071726), LV_OPA_COVER, 0);
    for (int i = 0; i < 8; ++i) {
        int yy = y + 8 + i * 15;
        int shift = (i & 1) ? 28 : 0;
        draw_line(layer, 34 + shift, yy, 220 + shift, yy,
                  lv_color_hex(0x395E72), LV_OPA_35, 1);
        draw_line(layer, 330 - shift, yy+4, 515 - shift, yy+4,
                  lv_color_hex(0x416D82), LV_OPA_25, 1);
        draw_line(layer, 600 + shift/2, yy-2, 760, yy-2,
                  lv_color_hex(0x3E6578), LV_OPA_30, 1);
    }
}

static void draw_torii(lv_layer_t *layer, int32_t x, int32_t y, int32_t scale)
{
    const lv_color_t vermilion = lv_color_hex(0x8E2B20);
    int w = 86 * scale / 100;
    int h = 92 * scale / 100;
    int post = 8 * scale / 100;
    if (post < 4) post = 4;

    draw_rect(layer, x, y, x+w, y+7, vermilion, LV_OPA_COVER, 2);
    draw_rect(layer, x-8, y+10, x+w+8, y+16, lv_color_hex(0xA23826),
              LV_OPA_COVER, 2);
    draw_rect(layer, x+16, y+16, x+16+post, y+h, vermilion,
              LV_OPA_COVER, 1);
    draw_rect(layer, x+w-16-post, y+16, x+w-16, y+h, vermilion,
              LV_OPA_COVER, 1);
    draw_rect(layer, x+14, y+31, x+w-14, y+36, vermilion,
              LV_OPA_COVER, 1);
}

static void draw_pine_hint(lv_layer_t *layer)
{
    const lv_color_t trunk = lv_color_hex(0x111B1B);
    const lv_color_t needle = lv_color_hex(0x071314);

    draw_line(layer, 18, 0, 72, 88, trunk, LV_OPA_COVER, 8);
    draw_line(layer, 45, 38, 150, 18, trunk, LV_OPA_COVER, 6);
    draw_line(layer, 55, 52, 170, 72, trunk, LV_OPA_COVER, 5);

    for (int i = 0; i < 7; ++i) {
        draw_line(layer, 78+i*12, 23, 61+i*13, 8,
                  needle, LV_OPA_COVER, 4);
        draw_line(layer, 84+i*13, 27, 102+i*12, 10,
                  needle, LV_OPA_COVER, 4);
        draw_line(layer, 88+i*12, 59, 71+i*13, 43,
                  needle, LV_OPA_COVER, 4);
        draw_line(layer, 94+i*12, 61, 111+i*12, 46,
                  needle, LV_OPA_COVER, 4);
    }
}

static void draw_gold_waves(lv_layer_t *layer, int32_t y0)
{
    const lv_color_t gold = lv_color_hex(0x8D6A34);
    for (int row = 0; row < 4; ++row) {
        int y = y0 + row * 14;
        for (int x = 0; x < 800; x += 92) {
            draw_line(layer, x, y+6, x+23, y, gold, LV_OPA_55, 1);
            draw_line(layer, x+23, y, x+46, y+6, gold, LV_OPA_55, 1);
            draw_line(layer, x+46, y+6, x+69, y, gold, LV_OPA_55, 1);
            draw_line(layer, x+69, y, x+92, y+6, gold, LV_OPA_55, 1);
        }
    }
}

static void draw_soroban(lv_layer_t *layer)
{
    /* Modern Soroban Glass: small decorative abacus, not a full bitmap. */
    const lv_color_t frame = lv_color_hex(0xB88B44);
    const lv_color_t rod = lv_color_hex(0x80643B);
    const lv_color_t bead = lv_color_hex(0xC99648);
    const lv_color_t red = lv_color_hex(0x9F2E28);

    draw_rect(layer, 34, 250, 252, 421, lv_color_hex(0x111821), LV_OPA_90, 14);
    /* Frame */
    draw_line(layer, 43, 263, 242, 263, frame, LV_OPA_COVER, 5);
    draw_line(layer, 43, 408, 242, 408, frame, LV_OPA_COVER, 5);
    draw_line(layer, 43, 263, 43, 408, frame, LV_OPA_COVER, 5);
    draw_line(layer, 242, 263, 242, 408, frame, LV_OPA_COVER, 5);
    draw_line(layer, 43, 313, 242, 313, frame, LV_OPA_COVER, 3);

    for (int c = 0; c < 8; ++c) {
        int x = 60 + c * 23;
        draw_line(layer, x, 272, x, 398, rod, LV_OPA_70, 2);

        int top_y = 286 + (c % 3) * 4;
        draw_rect(layer, x-8, top_y-5, x+8, top_y+5,
                  c == 2 ? red : bead, LV_OPA_COVER, 5);

        for (int r = 0; r < 4; ++r) {
            int yy = 335 + r * 16 + ((c + r) & 1 ? 2 : 0);
            draw_rect(layer, x-8, yy-5, x+8, yy+5,
                      (c == 2 && r < 2) ? red : bead,
                      LV_OPA_COVER, 5);
        }
    }
}

static void draw_fireworks_b(lv_layer_t *layer, const lv_area_t *area)
{
    (void)area;
    draw_rect(layer, 0, 0, 799, 479, lv_color_hex(0x041322), LV_OPA_COVER, 0);
    draw_moon(layer, 132, 142, 54);
    draw_pine_hint(layer);
    draw_mountain_band(layer, 337, lv_color_hex(0x142C3D), lv_color_hex(0x0C2131));
    draw_lake(layer, 354);
    draw_torii(layer, 674, 287, 80);

    /* Moon reflection */
    for (int i = 0; i < 6; ++i) {
        int yy = 365 + i * 14;
        int half = 46 - i * 5;
        draw_line(layer, 132-half, yy, 132+half, yy,
                  lv_color_hex(0xDCCB9D), (lv_opa_t)(70 - i*8), 2);
    }
}

static void draw_clock_b(lv_layer_t *layer, const lv_area_t *area)
{
    (void)area;
    draw_rect(layer, 0, 0, 799, 479, lv_color_hex(0x041322), LV_OPA_COVER, 0);
    draw_moon(layer, 116, 168, 72);
    draw_pine_hint(layer);
    draw_mountain_band(layer, 350, lv_color_hex(0x132C3E), lv_color_hex(0x0A202F));
    draw_lake(layer, 366);
    draw_torii(layer, 92, 315, 72);

    /* Dark veil on right for controls */
    draw_rect(layer, 330, 58, 799, 479, lv_color_hex(0x06111C), LV_OPA_75, 0);
    draw_line(layer, 471, 128, 471, 432, lv_color_hex(0x477186), LV_OPA_45, 1);
}

static void draw_calculator_a(lv_layer_t *layer, const lv_area_t *area)
{
    (void)area;
    draw_rect(layer, 0, 0, 799, 479, lv_color_hex(0x07111A), LV_OPA_COVER, 0);
    draw_moon(layer, 122, 165, 82);
    draw_pine_hint(layer);
    draw_soroban(layer);
    draw_gold_waves(layer, 414);

    /* Functional area gets a subtle glass-black wash. */
    draw_rect(layer, 276, 46, 796, 476, lv_color_hex(0x09131E), LV_OPA_85, 18);
    draw_line(layer, 276, 46, 796, 46, lv_color_hex(0x8C6A3C), LV_OPA_55, 1);
}

void ui_yokai_art_draw(lv_layer_t *layer, const lv_area_t *area,
                       ui_yokai_art_scene_t scene)
{
    if (!layer || !area) return;

    /* This art is authored for the physical 800x480 panel. */
    switch (scene) {
    case UI_YOKAI_ART_FIREWORKS_B:
        draw_fireworks_b(layer, area);
        break;
    case UI_YOKAI_ART_CLOCK_B:
        draw_clock_b(layer, area);
        break;
    case UI_YOKAI_ART_CALCULATOR_A:
        draw_calculator_a(layer, area);
        break;
    default:
        break;
    }
}

static void art_draw_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;

    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    ui_yokai_art_scene_t scene =
        (ui_yokai_art_scene_t)(uintptr_t)lv_event_get_user_data(e);

    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    ui_yokai_art_draw(layer, &a, scene);
}

void ui_yokai_art_attach(lv_obj_t *obj, ui_yokai_art_scene_t scene)
{
    if (!obj) return;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, art_draw_cb, LV_EVENT_DRAW_MAIN,
                        (void *)(uintptr_t)scene);
}
