#include "ui/ui_yokai_art.h"
#include "ui/ui_theme.h"
#include <stdint.h>

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

static void draw_sky_atmosphere(lv_layer_t *layer)
{
    /* Deep vertical night sky progression */
    draw_rect(layer, 0, 0, 799, 80, lv_color_hex(0x020610), LV_OPA_COVER, 0);
    draw_rect(layer, 0, 80, 799, 160, lv_color_hex(0x040D1B), LV_OPA_COVER, 0);
    draw_rect(layer, 0, 160, 799, 235, lv_color_hex(0x071728), LV_OPA_COVER, 0);
    draw_rect(layer, 0, 235, 799, 305, lv_color_hex(0x0C2237), LV_OPA_COVER, 0);
    draw_rect(layer, 0, 305, 799, 360, lv_color_hex(0x132F4A), LV_OPA_COVER, 0);
}

static void draw_starfield(lv_layer_t *layer)
{
    static const struct { int16_t x, y; uint8_t opa; uint8_t size; } stars[] = {
        {35, 28, 140, 1}, {88, 74, 90, 1}, {145, 42, 170, 2}, {210, 95, 110, 1},
        {260, 35, 80, 1}, {310, 70, 150, 1}, {375, 25, 200, 2}, {430, 85, 90, 1},
        {485, 40, 130, 1}, {530, 110, 75, 1}, {580, 32, 180, 2}, {625, 75, 100, 1},
        {670, 28, 160, 1}, {715, 90, 85, 1}, {760, 48, 140, 2}, {785, 20, 110, 1},
        {60, 145, 95, 1}, {180, 165, 120, 1}, {240, 180, 85, 1}, {460, 160, 110, 1},
        {540, 175, 90, 1}, {695, 150, 130, 1}, {740, 185, 70, 1}, {110, 215, 80, 1},
        {290, 225, 100, 1}, {515, 210, 85, 1}, {660, 220, 90, 1}, {770, 230, 100, 1}
    };

    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(0xF0F6FF);

    for (size_t i = 0; i < sizeof(stars)/sizeof(stars[0]); ++i) {
        d.bg_opa = (lv_opa_t)stars[i].opa;
        int32_t s = stars[i].size;
        lv_area_t a = {stars[i].x, stars[i].y, stars[i].x + s, stars[i].y + s};
        lv_draw_rect(layer, &d, &a);
    }
}

static void draw_moon(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t r)
{
    /* Layered diffuse glowing corona */
    draw_rect(layer, cx-r-36, cy-r-36, cx+r+36, cy+r+36,
              lv_color_hex(0xB2CDE2), LV_OPA_10, LV_RADIUS_CIRCLE);
    draw_rect(layer, cx-r-22, cy-r-22, cx+r+22, cy+r+22,
              lv_color_hex(0xDCE4CE), LV_OPA_20, LV_RADIUS_CIRCLE);
    draw_rect(layer, cx-r-10, cy-r-10, cx+r+10, cy+r+10,
              lv_color_hex(0xF4ECCB), LV_OPA_30, LV_RADIUS_CIRCLE);

    /* Moon disc - warm harvest moon pearl tone */
    draw_rect(layer, cx-r, cy-r, cx+r, cy+r,
              lv_color_hex(0xFFF6D9), LV_OPA_COVER, LV_RADIUS_CIRCLE);

    /* Lunar maria / crater shading */
    draw_rect(layer, cx - r*35/100, cy - r*22/100,
              cx - r*10/100, cy + r*15/100,
              lv_color_hex(0xD4C499), LV_OPA_30, LV_RADIUS_CIRCLE);
    draw_rect(layer, cx + r*12/100, cy - r*38/100,
              cx + r*42/100, cy - r*10/100,
              lv_color_hex(0xC7B68A), LV_OPA_30, LV_RADIUS_CIRCLE);
    draw_rect(layer, cx + r*15/100, cy + r*10/100,
              cx + r*55/100, cy + r*40/100,
              lv_color_hex(0xCEBF94), LV_OPA_30, LV_RADIUS_CIRCLE);
    draw_rect(layer, cx - r*15/100, cy + r*25/100,
              cx + r*18/100, cy + r*48/100,
              lv_color_hex(0xD8CA9E), LV_OPA_20, LV_RADIUS_CIRCLE);

    /* Delicate wisp of night cloud drifting across lower moon edge */
    draw_rect(layer, cx - r*70/100, cy + r*42/100,
              cx + r*85/100, cy + r*58/100,
              lv_color_hex(0x0C1F32), LV_OPA_30, 6);
}

static void draw_mountains_rich(lv_layer_t *layer, int32_t y_base)
{
    /* Layer 1: Distant misty mountain range */
    const lv_color_t mist_mountain = lv_color_hex(0x152E43);
    const int32_t back_peaks[][2] = {
        {0, y_base}, {80, y_base-82}, {170, y_base-15}, {265, y_base-102},
        {370, y_base-25}, {480, y_base-88}, {590, y_base-18}, {690, y_base-108},
        {800, y_base}
    };
    for (int i = 0; i + 2 < (int)(sizeof(back_peaks)/sizeof(back_peaks[0])); i += 2) {
        draw_triangle(layer,
                      back_peaks[i][0], back_peaks[i][1],
                      back_peaks[i+1][0], back_peaks[i+1][1],
                      back_peaks[i+2][0], back_peaks[i+2][1],
                      mist_mountain, LV_OPA_80);
    }

    /* Layer 2: Midground ridge */
    const lv_color_t mid_mountain = lv_color_hex(0x0C2031);
    const int32_t mid_peaks[][2] = {
        {0, y_base+14}, {120, y_base-48}, {240, y_base+8},
        {360, y_base-62}, {500, y_base+6}, {640, y_base-52},
        {800, y_base+14}
    };
    for (int i = 0; i + 2 < (int)(sizeof(mid_peaks)/sizeof(mid_peaks[0])); i += 2) {
        draw_triangle(layer,
                      mid_peaks[i][0], mid_peaks[i][1],
                      mid_peaks[i+1][0], mid_peaks[i+1][1],
                      mid_peaks[i+2][0], mid_peaks[i+2][1],
                      mid_mountain, LV_OPA_COVER);
    }

    /* Distant lakeside village / shrine lights along horizon */
    static const int16_t lights[][2] = {
        {185, 348}, {215, 350}, {340, 351}, {365, 349},
        {520, 350}, {550, 352}, {610, 348}, {630, 351}
    };
    for (size_t i = 0; i < sizeof(lights)/sizeof(lights[0]); ++i) {
        draw_rect(layer, lights[i][0]-1, lights[i][1]-1,
                  lights[i][0]+2, lights[i][1]+2,
                  lv_color_hex(0xFFA726), LV_OPA_80, 1);
        /* Subtle water reflection dot */
        draw_line(layer, lights[i][0]-2, lights[i][1]+4,
                  lights[i][0]+2, lights[i][1]+4,
                  lv_color_hex(0xFFB74D), LV_OPA_40, 1);
    }
}

static void draw_lake_rich(lv_layer_t *layer, int32_t y, int32_t moon_cx)
{
    /* Lake deep reflective base */
    draw_rect(layer, 0, y, 799, 479, lv_color_hex(0x040F1A), LV_OPA_COVER, 0);

    /* Soft ambient water surface sheen */
    for (int i = 0; i < 9; ++i) {
        int yy = y + 6 + i * 13;
        int shift = (i & 1) ? 35 : 0;
        draw_line(layer, 20 + shift, yy, 240 + shift, yy,
                  lv_color_hex(0x23445A), LV_OPA_20, 1);
        draw_line(layer, 310 - shift, yy+3, 530 - shift, yy+3,
                  lv_color_hex(0x2B4E66), LV_OPA_20, 1);
        draw_line(layer, 590 + shift/2, yy-2, 775, yy-2,
                  lv_color_hex(0x25475E), LV_OPA_20, 1);
    }

    /* Moonlight reflection pillar on the water surface */
    if (moon_cx > 0) {
        for (int i = 0; i < 9; ++i) {
            int yy = y + 8 + i * 12;
            int half = 48 - i * 4;
            if (half < 14) half = 14;
            lv_opa_t opa = (lv_opa_t)(75 - i * 7);
            draw_line(layer, moon_cx - half, yy, moon_cx + half, yy,
                      lv_color_hex(0xEEDEB2), opa, 2);
            /* Soft side ripples */
            draw_line(layer, moon_cx - half - 16, yy+2, moon_cx - half - 4, yy+2,
                      lv_color_hex(0xDBC99B), (lv_opa_t)(opa / 2), 1);
            draw_line(layer, moon_cx + half + 4, yy+2, moon_cx + half + 16, yy+2,
                      lv_color_hex(0xDBC99B), (lv_opa_t)(opa / 2), 1);
        }
    }
}

static void draw_torii_rich(lv_layer_t *layer, int32_t x, int32_t y, int32_t scale)
{
    const lv_color_t vermilion = lv_color_hex(0xB33124);
    const lv_color_t vermilion_hi = lv_color_hex(0xC93C2C);
    const lv_color_t black_wood = lv_color_hex(0x181515);

    int w = 88 * scale / 100;
    int h = 96 * scale / 100;
    int post_w = 9 * scale / 100;
    if (post_w < 5) post_w = 5;

    /* Kasagi (curved upper roof beam) with black end caps */
    draw_rect(layer, x - 10, y, x + w + 10, y + 8, vermilion_hi, LV_OPA_COVER, 3);
    draw_rect(layer, x - 12, y + 1, x - 7, y + 7, black_wood, LV_OPA_COVER, 1);
    draw_rect(layer, x + w + 7, y + 1, x + w + 12, y + 7, black_wood, LV_OPA_COVER, 1);

    /* Shimaki (secondary beam right under Kasagi) */
    draw_rect(layer, x - 4, y + 8, x + w + 4, y + 15, vermilion, LV_OPA_COVER, 2);

    /* Nuki (horizontal tie beam) */
    draw_rect(layer, x + 8, y + 33, x + w - 8, y + 40, vermilion, LV_OPA_COVER, 1);

    /* Left & Right Hashira (vertical pillars) */
    int left_x = x + 18;
    int right_x = x + w - 18 - post_w;
    draw_rect(layer, left_x, y + 15, left_x + post_w, y + h, vermilion, LV_OPA_COVER, 2);
    draw_rect(layer, right_x, y + 15, right_x + post_w, y + h, vermilion, LV_OPA_COVER, 2);

    /* Kamebara / Daiishi (black water foundation pedestals) */
    draw_rect(layer, left_x - 3, y + h - 10, left_x + post_w + 3, y + h, black_wood, LV_OPA_COVER, 2);
    draw_rect(layer, right_x - 3, y + h - 10, right_x + post_w + 3, y + h, black_wood, LV_OPA_COVER, 2);

    /* Torii warm reflection in the lake water */
    draw_line(layer, left_x - 4, y + h + 4, left_x + post_w + 4, y + h + 4,
              lv_color_hex(0x9E3025), LV_OPA_40, 2);
    draw_line(layer, right_x - 4, y + h + 4, right_x + post_w + 4, y + h + 4,
              lv_color_hex(0x9E3025), LV_OPA_40, 2);
}

static void draw_stone_lantern(lv_layer_t *layer, int32_t x, int32_t y)
{
    const lv_color_t stone = lv_color_hex(0x13212C);
    const lv_color_t amber = lv_color_hex(0xFFAB2E);

    /* Roof cap (Kasa) */
    draw_triangle(layer, x - 14, y + 8, x, y, x + 14, y + 8, stone, LV_OPA_COVER);
    draw_rect(layer, x - 12, y + 7, x + 12, y + 11, stone, LV_OPA_COVER, 2);

    /* Light chamber (Hibukuro) glowing amber */
    draw_rect(layer, x - 16, y + 9, x + 16, y + 23, amber, LV_OPA_20, LV_RADIUS_CIRCLE);
    draw_rect(layer, x - 8, y + 11, x + 8, y + 21, amber, LV_OPA_COVER, 2);

    /* Base & pedestal */
    draw_rect(layer, x - 6, y + 21, x + 6, y + 42, stone, LV_OPA_COVER, 1);
    draw_rect(layer, x - 12, y + 42, x + 12, y + 50, stone, LV_OPA_COVER, 2);

    /* Amber water reflection */
    draw_line(layer, x - 8, y + 54, x + 8, y + 54, amber, LV_OPA_50, 2);
    draw_line(layer, x - 5, y + 60, x + 5, y + 60, amber, LV_OPA_30, 1);
}

static void draw_pine_silhouette(lv_layer_t *layer)
{
    const lv_color_t trunk = lv_color_hex(0x0B1417);
    const lv_color_t needle = lv_color_hex(0x060E10);

    /* Gracefully curving Japanese pine branch from top-left */
    draw_line(layer, 0, 10, 85, 92, trunk, LV_OPA_COVER, 10);
    draw_line(layer, 65, 75, 168, 48, trunk, LV_OPA_COVER, 7);
    draw_line(layer, 85, 92, 195, 115, trunk, LV_OPA_COVER, 6);

    /* Needle fan clusters */
    for (int i = 0; i < 9; ++i) {
        int bx = 70 + i * 14;
        draw_line(layer, bx, 55, bx - 14, 38, needle, LV_OPA_COVER, 4);
        draw_line(layer, bx + 4, 58, bx + 16, 42, needle, LV_OPA_COVER, 4);
        draw_line(layer, bx - 2, 98, bx - 16, 80, needle, LV_OPA_COVER, 4);
        draw_line(layer, bx + 6, 102, bx + 18, 85, needle, LV_OPA_COVER, 4);
    }
}

static void draw_gold_waves(lv_layer_t *layer, int32_t y0)
{
    const lv_color_t gold = lv_color_hex(0x9E783B);
    for (int row = 0; row < 4; ++row) {
        int y = y0 + row * 13;
        for (int x = 20; x < 270; x += 80) {
            draw_line(layer, x, y+5, x+20, y, gold, LV_OPA_40, 1);
            draw_line(layer, x+20, y, x+40, y+5, gold, LV_OPA_40, 1);
            draw_line(layer, x+40, y+5, x+60, y, gold, LV_OPA_40, 1);
            draw_line(layer, x+60, y, x+80, y+5, gold, LV_OPA_40, 1);
        }
    }
}

static void draw_soroban(lv_layer_t *layer)
{
    /* Refined Japanese Lacquer Soroban */
    const lv_color_t frame_wood = lv_color_hex(0x23170E);
    const lv_color_t gold_trim = lv_color_hex(0xC49A45);
    const lv_color_t rod = lv_color_hex(0x9E8148);
    const lv_color_t bead_amber = lv_color_hex(0xD49E48);
    const lv_color_t bead_hi = lv_color_hex(0xF4CE7E);
    const lv_color_t bead_red = lv_color_hex(0xB7282E);

    /* Abacus Outer Lacquer Base */
    draw_rect(layer, 28, 230, 258, 435, frame_wood, LV_OPA_90, 12);
    /* Gold trim outline */
    draw_line(layer, 36, 238, 250, 238, gold_trim, LV_OPA_80, 3);
    draw_line(layer, 36, 427, 250, 427, gold_trim, LV_OPA_80, 3);
    draw_line(layer, 36, 238, 36, 427, gold_trim, LV_OPA_80, 3);
    draw_line(layer, 250, 238, 250, 427, gold_trim, LV_OPA_80, 3);

    /* Middle beam (Hari) dividing upper and lower deck */
    draw_line(layer, 36, 295, 250, 295, gold_trim, LV_OPA_COVER, 4);

    /* Unit dot on beam */
    draw_rect(layer, 114, 293, 118, 297, lv_color_hex(0xFFFFFF), LV_OPA_COVER, 1);

    /* 8 Columns of Brass Rods & Beads */
    for (int c = 0; c < 8; ++c) {
        int x = 52 + c * 25;
        draw_line(layer, x, 244, x, 420, rod, LV_OPA_70, 2);

        /* Upper deck: 1 bead (value 5) */
        int top_y = (c % 2 == 0) ? 260 : 278;
        lv_color_t color = (c == 2) ? bead_red : bead_amber;
        draw_rect(layer, x - 9, top_y - 6, x + 9, top_y + 6, color, LV_OPA_COVER, 4);
        draw_line(layer, x - 7, top_y - 3, x + 7, top_y - 3, bead_hi, LV_OPA_60, 1);

        /* Lower deck: 4 beads (value 1 each) */
        for (int r = 0; r < 4; ++r) {
            int yy = 318 + r * 24 + ((c + r) & 1 ? 4 : 0);
            draw_rect(layer, x - 9, yy - 6, x + 9, yy + 6, color, LV_OPA_COVER, 4);
            draw_line(layer, x - 7, yy - 3, x + 7, yy - 3, bead_hi, LV_OPA_60, 1);
        }
    }
}

static void draw_fireworks_b(lv_layer_t *layer, const lv_area_t *area)
{
    (void)area;
    draw_sky_atmosphere(layer);
    draw_starfield(layer);
    draw_moon(layer, 130, 140, 56);
    draw_pine_silhouette(layer);
    draw_mountains_rich(layer, 342);
    draw_lake_rich(layer, 356, 130);
    draw_stone_lantern(layer, 55, 340);
    draw_torii_rich(layer, 668, 276, 88);
}

static void draw_clock_b(lv_layer_t *layer, const lv_area_t *area)
{
    (void)area;
    draw_sky_atmosphere(layer);
    draw_starfield(layer);
    draw_moon(layer, 112, 155, 68);
    draw_pine_silhouette(layer);
    draw_mountains_rich(layer, 348);
    draw_lake_rich(layer, 360, 112);
    draw_torii_rich(layer, 85, 298, 76);

    /* Right glass card container framing clock & stopwatch controls */
    draw_rect(layer, 328, 58, 792, 474, lv_color_hex(0x06111C), LV_OPA_80, 16);
    /* Subtle glass edge highlight */
    draw_line(layer, 328, 74, 328, 458, lv_color_hex(0x2B4F66), LV_OPA_40, 1);
}

static void draw_calculator_a(lv_layer_t *layer, const lv_area_t *area)
{
    (void)area;
    draw_sky_atmosphere(layer);
    draw_starfield(layer);
    draw_moon(layer, 120, 150, 78);
    draw_pine_silhouette(layer);
    draw_soroban(layer);
    draw_gold_waves(layer, 432);

    /* Functional keypad & display glass card container starting at y=58 (below header title) */
    draw_rect(layer, 280, 58, 792, 474, lv_color_hex(0x08131E), LV_OPA_80, 16);
    draw_line(layer, 280, 74, 280, 458, lv_color_hex(0x8C6A3C), LV_OPA_30, 1);
}

void ui_yokai_art_draw(lv_layer_t *layer, const lv_area_t *area,
                       ui_yokai_art_scene_t scene)
{
    if (!layer || !area) return;

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
