"""Exercise real weather UI functions: hidden updates, detach, reload and failure."""
from pathlib import Path
import subprocess
import tempfile

project = Path(__file__).resolve().parents[1]
source = (project / "main/ui/ui_weather.c").read_text()
def extract(signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]

harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "weather_service.h"
#define LV_OBJ_FLAG_HIDDEN 1
#define LV_STATE_DISABLED 2
#define UI_COLOR_CYAN_ACCENT 0
#define UI_COLOR_GOLD_ACCENT 1
typedef struct { const void *src; bool hidden; char text[128]; } lv_obj_t;
typedef struct { const void *data; } lv_image_dsc_t;
static lv_image_dsc_t ui_img_weather_sunny;
static lv_obj_t bg, badge, time_label;
static lv_obj_t *s_scr_weather = &bg, *s_img_bg = &bg;
static lv_obj_t *s_lottie_snow, *s_lottie_rain, *s_lbl_temp, *s_lbl_cond;
static lv_obj_t *s_lbl_badge = &badge, *s_lbl_time = &time_label, *s_lbl_lore;
static lv_obj_t *s_btn_refresh, *s_lbl_refresh;
static bool s_active_weather, fail_decode;
static int loads, frees;
static weather_info_t snapshot;
static int ui_weather_background_load(weather_cond_t c, bool day) {
    loads++;
    if (fail_decode) return ESP_FAIL;
    ui_img_weather_sunny.data = &snapshot;
    return ESP_OK;
}
static void ui_weather_background_free(void) {
    assert(bg.src == NULL && bg.hidden);
    frees++;
    ui_img_weather_sunny.data = NULL;
}
void weather_service_get_info(weather_info_t *out) { *out = snapshot; }
static void lv_image_set_src(lv_obj_t *o, const void *src) {
    if (src) assert(((const lv_image_dsc_t *)src)->data);
    o->src = src;
}
static void lv_obj_add_flag(lv_obj_t *o, int flag) { o->hidden = true; }
static void lv_obj_remove_flag(lv_obj_t *o, int flag) { o->hidden = false; }
static bool lv_obj_has_flag(lv_obj_t *o, int flag) { return o->hidden; }
static void lv_obj_invalidate(lv_obj_t *o) {}
static void lv_lottie_play(lv_obj_t *o) {}
static void lv_lottie_pause(lv_obj_t *o) {}
static void lv_label_set_text(lv_obj_t *o, const char *s) { snprintf(o->text, sizeof(o->text), "%s", s); }
static void lv_obj_set_style_text_color(lv_obj_t *o, int c, int part) {}
static void lv_obj_add_state(lv_obj_t *o, int state) {}
static void lv_obj_remove_state(lv_obj_t *o, int state) {}
'''
checks = r'''
int main(void) {
    bg.hidden = true;
    snapshot.condition = WEATHER_COND_SUNNY;
    snapshot.is_day = true;
    ui_weather_screen_update(&snapshot);
    assert(loads == 0);
    ui_weather_set_active(true);
    assert(loads == 1 && bg.src && !bg.hidden);
    for (int i = 0; i < 5; ++i) {
        ui_weather_set_active(false);
        assert(bg.src == NULL && bg.hidden && ui_img_weather_sunny.data == NULL);
        int before = loads;
        ui_weather_screen_update(&snapshot);
        assert(loads == before);
        ui_weather_set_active(true);
        assert(bg.src && !bg.hidden);
    }
    ui_weather_set_active(false);
    fail_decode = true;
    ui_weather_set_active(true);
    assert(bg.src == NULL && bg.hidden);
    snapshot.refreshing = true;
    ui_weather_screen_update(&snapshot);
    assert(strstr(badge.text, "更新中"));
    snapshot.refreshing = false;
    snapshot.has_last_success = true;
    snapshot.refresh_failed = true;
    strcpy(snapshot.update_time, "14:30");
    ui_weather_screen_update(&snapshot);
    assert(strstr(badge.text, "更新失敗") && strstr(time_label.text, "14:30"));
    snapshot.refresh_failed = false;
    ui_weather_screen_update(&snapshot);
    assert(strstr(badge.text, "STALE"));
    assert(frees == 6);
    puts("Weather UI lifecycle checks PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "weather_ui.c"
    c.write_text(harness + extract("void ui_weather_screen_update(") + extract("void ui_weather_set_active(") + checks)
    exe = Path(tmp) / "check"
    subprocess.run(["cc", "-std=c11", "-DHOST_TEST", "-I" + str(project / "main"), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
