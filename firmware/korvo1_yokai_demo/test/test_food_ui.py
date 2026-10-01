"""Execute real Food UI code with small LVGL ownership/event stubs; no display needed."""
from pathlib import Path
import subprocess
import tempfile
import re

project = Path(__file__).resolve().parents[1]
source = (project / "main/ui/ui_food.c").read_text()
source = re.sub(r'^#include "[^\n]+"\n', '', source, flags=re.M)
harness = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "food_service.h"
typedef struct lv_obj lv_obj_t;
typedef struct { lv_obj_t *target; void *data; } lv_event_t;
typedef void (*lv_event_cb_t)(lv_event_t *);
typedef void (*ui_home_btn_cb_t)(void);
typedef int lv_buttonmatrix_ctrl_t;
void ui_food_tick(void);
struct lv_obj { lv_obj_t *child, *next; lv_event_cb_t cb; void *data; char text[160]; };
static unsigned allocations, frees, home_calls;
static int ui_style_glass_card;
#define LV_EVENT_CLICKED 1
#define LV_EVENT_FOCUSED 2
#define LV_EVENT_CANCEL 3
#define LV_EVENT_READY 4
#define LV_STATE_DISABLED 0
#define LV_FLEX_FLOW_COLUMN 0
#define LV_LABEL_LONG_MODE_DOTS 0
#define LV_KEYBOARD_MODE_USER_1 0
#define LV_KEYBOARD_MODE_TEXT_LOWER 1
#define UI_FONT_SMALL 0
#define UI_FONT_REGULAR 0
#define UI_COLOR_TEXT_TITLE 0
#define UI_COLOR_BG_DARK 0
#define UI_COLOR_RED_ACCENT 0
#define LV_SYMBOL_BACKSPACE "back"
#define LV_SYMBOL_LEFT "left"
#define LV_SYMBOL_RIGHT "right"
#define LV_SYMBOL_CLOSE "cancel"
#define LV_SYMBOL_OK "ok"
static lv_obj_t *lv_obj_create(lv_obj_t *p) {
    lv_obj_t *o = calloc(1, sizeof(*o)); assert(o); allocations++;
    if (p) { o->next = p->child; p->child = o; } return o;
}
#define lv_label_create lv_obj_create
#define lv_button_create lv_obj_create
#define lv_textarea_create lv_obj_create
#define lv_keyboard_create lv_obj_create
static void lv_obj_delete(lv_obj_t *o) {
    while(o->child) { lv_obj_t *c = o->child; o->child = c->next; lv_obj_delete(c); }
    free(o); frees++;
}
/* Detach root-owned subtrees before destroying them, just as LVGL does. */
static void detach_delete(lv_obj_t *o);
static void lv_obj_clean(lv_obj_t *o) {
    while(o->child) { lv_obj_t *c = o->child; o->child = c->next; lv_obj_delete(c); }
}
static void lv_label_set_text(lv_obj_t *o, const char *s) { snprintf(o->text, sizeof(o->text), "%s", s); }
static void lv_label_set_text_fmt(lv_obj_t *o, const char *s, ...) {
    va_list ap; va_start(ap,s); vsnprintf(o->text,sizeof(o->text),s,ap); va_end(ap);
}
#define lv_textarea_set_text lv_label_set_text
static const char *lv_textarea_get_text(lv_obj_t *o) { return o->text; }
static void lv_obj_add_event_cb(lv_obj_t *o, lv_event_cb_t cb, int event, void *data) {
    if(event == LV_EVENT_CLICKED) { o->cb = cb; o->data = data; }
}
static void *lv_event_get_user_data(lv_event_t *e) { return e->data; }
static lv_obj_t *lv_event_get_target(lv_event_t *e) { return e->target; }
static lv_obj_t *lv_obj_get_child(lv_obj_t *o, int n) { assert(n == 0); return o->child; }
static unsigned children(lv_obj_t *o) { unsigned n=0; for(o=o->child;o;o=o->next)n++; return n; }
static unsigned records = 32;
static food_record_t record = {"Milk", "2026-10-01"};
food_result_t food_service_init(void) { return FOOD_OK; }
size_t food_service_count(void) { return records; }
const food_record_t *food_service_get(size_t i) { return i<records ? &record : NULL; }
food_result_t food_service_add(const char *n,const char *d) { (void)n;(void)d;return FOOD_OK; }
food_result_t food_service_edit(size_t i,const char *n,const char *d) { (void)i;(void)n;(void)d;return FOOD_OK; }
food_result_t food_service_delete(size_t i) { (void)i;return FOOD_OK; }
bool food_date_ordinal(const char *d,int *n) { (void)d; *n=1;return true; }
const char *food_service_error(food_result_t r) { (void)r;return ""; }
static void home(void) { home_calls++; }
'''
# Presentation setters do not participate in ownership or event behavior.
noop = [name for name in set(re.findall(r'\b(lv_[a-z_]+|ui_add_click_sfx)\(', source))
        if name.startswith(('lv_obj_set_', 'lv_obj_add_style', 'lv_obj_add_state', 'lv_keyboard_set_',
                            'lv_textarea_set_', 'lv_label_set_long_mode')) or name in ('lv_obj_center', 'ui_add_click_sfx')]
noop.remove('lv_textarea_set_text')
harness += ''.join(f'#define {name}(...) ((void)0)\n' for name in noop)
# Keep an internal raw deleter and wrap source deletion to emulate parent detachment.
source = source.replace('lv_obj_delete(', 'detach_delete(')
checks = r'''
static void detach_delete(lv_obj_t *o) {
    lv_obj_t **link = &s_screen->child;
    while (*link && *link != o) link = &(*link)->next;
    if (*link == o) *link = o->next;
    lv_obj_delete(o);
}
int main(void) {
    ui_food_screen_create(home);
    unsigned idle = allocations - frees;
    assert(!s_content && !s_list && !s_modal && !s_keyboard);
    lv_obj_t *home_button = s_screen->child->next;
    assert(home_button && home_button->cb);
    lv_event_t e = {home_button, home_button->data};
    home_button->cb(&e); assert(home_calls == 1);
    s_home_cb = NULL; home_button->cb(&e); assert(home_calls == 1); s_home_cb = home;
    for (unsigned i=0;i<20;i++) {
        ui_food_set_active(true);
        assert(s_content && s_list && children(s_list) == 6);
        assert(!s_modal && !s_keyboard);
        lv_event_t next = {NULL, (void *)(uintptr_t)1};
        for (unsigned p=0;p<5;p++) page_cb(&next);
        assert(s_page == 5 && children(s_list) == 2);
        unsigned before = allocations;
        ui_food_tick(); assert(allocations == before);
        open_editor(0); assert(s_modal && s_keyboard);
        ui_food_set_active(false);
        assert(!s_content && !s_list && !s_modal && !s_keyboard);
        assert(allocations - frees == idle);
        before = allocations;
        ui_food_tick(); ui_food_set_active(false);
        assert(allocations == before);
    }
    lv_obj_delete(s_screen); assert(allocations == frees);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / 'food_ui.c'
    path.write_text(harness + source + checks)
    exe = Path(tmp) / 'food_ui'
    subprocess.run(['cc', '-std=c11', '-Werror=cast-function-type-strict', '-fsanitize=address,undefined',
                    '-I', str(project / 'main'), str(path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Food Home callback and 20 UI lifecycle cycles passed')
