"""Run actual Fireworks input/draw callbacks at S31 and P4 viewport origins."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/ui/ui_fireworks.c').read_text()
def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include "fireworks_engine.h"
typedef struct {int32_t x,y;} lv_point_t;
typedef struct {int32_t x1,y1,x2,y2;} lv_area_t;
typedef int lv_obj_t,lv_layer_t,lv_indev_t,lv_event_t,lv_event_code_t;
typedef uint8_t lv_opa_t;
typedef struct {int color,opa,width,round_start,round_end;lv_point_t p1,p2;} lv_draw_line_dsc_t;
typedef struct {int bg_color,bg_opa,radius;} lv_draw_rect_dsc_t;
#define LV_EVENT_PRESSED 1
#define LV_EVENT_RELEASED 2
#define UI_COLOR_GOLD_ACCENT 0
#define LV_OPA_80 204
#define lv_color_hex(x) (x)
#define lv_obj_invalidate(o) ((void)(o))
static lv_obj_t art,*s_art=&art;
static lv_point_t point,s_press_pt;
static int ox,oy,calls;
static float tap_x,tap_y,launch_x,launch_y,target_y;
static fw_particle_t particles[3];
static fw_rocket_t rocket;
static const int expected[6][4]={{10,20,30,40},{27,37,33,43},{27,37,33,43},
                               {30,40,10,20},{29,39,31,41},{100,200,100,218}};
static void __attribute__((unused)) lv_obj_get_coords(lv_obj_t *o,lv_area_t *a) {(void)o;*a=(lv_area_t){ox,oy,ox+799,oy+479};}
static lv_indev_t *lv_indev_active(void) {static lv_indev_t d;return &d;}
static int lv_event_get_code(lv_event_t *e) {return *e;}
static void lv_indev_get_point(lv_indev_t *d,lv_point_t *p) {(void)d;*p=point;}
void fireworks_engine_tap(float x,float y) {tap_x=x;tap_y=y;}
void fireworks_engine_launch(float x,float y,float t) {launch_x=x;launch_y=y;target_y=t;}
const fw_particle_t *fireworks_engine_particles(size_t *n) {*n=3;return particles;}
const fw_rocket_t *fireworks_engine_rockets(size_t *n) {*n=1;return &rocket;}
static void record(int x1,int y1,int x2,int y2) {assert(calls<6);const int *v=expected[calls++];assert(x1==v[0]+ox && y1==v[1]+oy && x2==v[2]+ox && y2==v[3]+oy);}
static void lv_draw_line_dsc_init(lv_draw_line_dsc_t *d) {*d=(lv_draw_line_dsc_t){0};}
static void lv_draw_rect_dsc_init(lv_draw_rect_dsc_t *d) {*d=(lv_draw_rect_dsc_t){0};}
static void lv_draw_line(lv_layer_t *l,lv_draw_line_dsc_t *d) {(void)l;record(d->p1.x,d->p1.y,d->p2.x,d->p2.y);}
static void lv_draw_rect(lv_layer_t *l,lv_draw_rect_dsc_t *d,lv_area_t *a) {(void)l;(void)d;record(a->x1,a->y1,a->x2,a->y2);}
'''
code += function('static void touch_cb(') + function('static void draw_particles(')
code += r'''
int main(void) {
    for(int i=0;i<3;i++) particles[i]=(fw_particle_t){.active=true,.style=i,.prev_x=10,.prev_y=20,.x=30,.y=40,.size=3,.life_s=1};
    rocket=(fw_rocket_t){.active=true,.x=100,.y=200};
    for(int p4=0;p4<2;p4++) {
        ox=p4?112:0;oy=p4?60:0;calls=0;
        lv_event_t press=LV_EVENT_PRESSED,release=LV_EVENT_RELEASED;
        point=(lv_point_t){ox+400,oy+200};touch_cb(&press);touch_cb(&release);
        assert(tap_x==400 && tap_y==200);
        point=(lv_point_t){ox+500,oy+400};touch_cb(&press);
        point.y=oy+150;touch_cb(&release);
        assert(launch_x==500 && launch_y==400 && target_y==150);
        draw_particles(NULL);assert(calls==6);
    }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path,binary=Path(tmp)/'check.c',Path(tmp)/'check'
    path.write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',root/'main',path,'-o',binary],check=True)
    subprocess.run([binary],check=True)
print('Fireworks touch and drawing at S31/P4 viewport origins passed')
