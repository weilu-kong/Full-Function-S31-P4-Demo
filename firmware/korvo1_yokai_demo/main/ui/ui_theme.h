#pragma once

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Japanese Yokai / Mythological Aesthetics Color Palette */
#define UI_COLOR_BG_DARK        lv_color_hex(0x0C0F17) /* 漆黒 (Jet Black) */
#define UI_COLOR_GLASS_CARD     lv_color_hex(0x161B26) /* 濃藍 (Deep Indigo Glass) */
#define UI_COLOR_GOLD_ACCENT    lv_color_hex(0xD4A373) /* 金箔 (Gold Leaf) */
#define UI_COLOR_RED_ACCENT     lv_color_hex(0xE63946) /* 茜色 (Crimson Vermilion) */
#define UI_COLOR_CYAN_ACCENT    lv_color_hex(0x48CAE4) /* 浅葱色 (Pale Cyan) */
#define UI_COLOR_TEXT_TITLE     lv_color_hex(0xF8F9FA) /* 純白 (Pure White) */
#define UI_COLOR_TEXT_SUB       lv_color_hex(0x8D99AE) /* 錫色 (Pewter Gray) */
#define UI_COLOR_KEY_WHITE      lv_color_hex(0x232B3B) /* 白鍵 / Lacquer Slate */
#define UI_COLOR_KEY_BLACK      lv_color_hex(0x11151E) /* 黒鍵 / Dark Ebony */

/* Global UI Styles */
extern lv_style_t ui_style_glass_card;
extern lv_style_t ui_style_glass_card_pressed;
extern lv_style_t ui_style_accent_border;
extern lv_style_t ui_style_pill_badge;
extern lv_style_t ui_style_btn_home;

/* Japanese Yokai CJK Font Declarations */
LV_FONT_DECLARE(ui_font_cjk_14);
LV_FONT_DECLARE(ui_font_cjk_16);
LV_FONT_DECLARE(ui_font_cjk_20);
LV_FONT_DECLARE(ui_font_cjk_32);

#define UI_FONT_TINY    (&ui_font_cjk_14)
#define UI_FONT_SMALL   (&ui_font_cjk_16)
#define UI_FONT_REGULAR (&ui_font_cjk_20)
#define UI_FONT_TITLE   (&ui_font_cjk_20)
#define UI_FONT_LARGE   (&ui_font_cjk_32)

/** Initialize the Japanese Yokai theme styles and tokens. */
void ui_theme_init(void);

#ifdef __cplusplus
}
#endif
