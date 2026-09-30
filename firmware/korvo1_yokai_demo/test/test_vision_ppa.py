#!/usr/bin/env python3
"""Compile the actual hardware-scaling fragment; check clipping and framebuffer ownership."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
fragment = ROOT / "tools/patches/lvgl_vision_ppa.c"
stub = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct { int x1,y1,x2,y2; } lv_area_t;
typedef struct { int w,h,stride; } header_t;
typedef struct { header_t header; unsigned char *data; unsigned data_size; } lv_draw_buf_t;
typedef lv_draw_buf_t lv_image_dsc_t;
typedef struct { lv_draw_buf_t *draw_buf; lv_area_t buf_area; } lv_layer_t;
typedef struct { const void *src; } lv_draw_image_dsc_t;
typedef struct { bool img_sw_fallback; void *srm_client; } lv_draw_ppa_unit_t;
typedef struct { lv_layer_t *target_layer; void *draw_unit; lv_area_t area,clip_area; } lv_draw_task_t;
typedef struct { void *buffer; unsigned buffer_size; int pic_w,pic_h,block_w,block_h,block_offset_x,block_offset_y,srm_cm; } pic_t;
typedef struct { pic_t in,out; float scale_x,scale_y; int rotation_angle,mode; } ppa_srm_oper_config_t;
#define PPA_SRM_COLOR_MODE_RGB565 1
#define PPA_TRANS_MODE_BLOCKING 1
#define ESP_OK 0
#define LV_LOG_WARN(...) ((void)0)
#define LV_LOG_USER(...) ((void)0)
static int calls, fallback, result;
static ppa_srm_oper_config_t seen;
static int ppa_do_scale_rotate_mirror(void *client,const ppa_srm_oper_config_t *cfg) { assert(client); calls++; seen=*cfg; return result; }
static void lv_draw_sw_image(lv_draw_task_t *t,const lv_draw_image_dsc_t *d,const lv_area_t *a) { (void)t;(void)d;(void)a;fallback++; }
'''
checks = r'''
int main(void) {
  static unsigned char source[153600], framebuffer[768000];
  lv_image_dsc_t image={{320,240,640},source,sizeof(source)};
  lv_draw_buf_t buffer={{800,480,1600},framebuffer,sizeof(framebuffer)};
  lv_layer_t layer={&buffer,{10,20,809,499}};
  lv_draw_ppa_unit_t unit={false,(void *)1};
  lv_draw_task_t task={&layer,&unit,{110,70,429,309},{110,70,509,369}};
  lv_draw_image_dsc_t dsc={&image};
  lv_draw_ppa_vision_scale(&task,&dsc,&task.area);
  assert(calls==1 && fallback==0 && !unit.img_sw_fallback);
  assert(seen.in.buffer==source && seen.out.buffer==framebuffer);
  assert(seen.in.block_w==320 && seen.in.block_h==240);
  assert(seen.scale_x==1.25f && seen.scale_y==1.25f);
  assert(seen.out.pic_w==800 && seen.out.pic_h==480);
  assert(seen.out.block_offset_x==100 && seen.out.block_offset_y==50);
  assert(seen.out.buffer_size==sizeof(framebuffer));
  task.clip_area.x2--; unit.img_sw_fallback=false;
  lv_draw_ppa_vision_scale(&task,&dsc,&task.area);
  assert(calls==1 && fallback==1 && unit.img_sw_fallback);
  task.clip_area.x2++; layer.buf_area.x2=508; unit.img_sw_fallback=false;
  lv_draw_ppa_vision_scale(&task,&dsc,&task.area);
  assert(calls==1 && fallback==2 && unit.img_sw_fallback);
  layer.buf_area.x2=809; result=-1; unit.img_sw_fallback=false;
  lv_draw_ppa_vision_scale(&task,&dsc,&task.area);
  assert(calls==2 && fallback==3 && unit.img_sw_fallback);
  result=0; image.data_size--; unit.img_sw_fallback=false;
  lv_draw_ppa_vision_scale(&task,&dsc,&task.area);
  assert(calls==2 && fallback==4 && unit.img_sw_fallback);
  image.data_size++; buffer.data_size--; unit.img_sw_fallback=false;
  lv_draw_ppa_vision_scale(&task,&dsc,&task.area);
  assert(calls==2 && fallback==5 && unit.img_sw_fallback);
  buffer.data_size++; task.area=(lv_area_t){410,200,729,439};
  task.clip_area=(lv_area_t){410,200,809,499}; unit.img_sw_fallback=false;
  lv_draw_ppa_vision_scale(&task,&dsc,&task.area);
  assert(calls==3 && fallback==5 && !unit.img_sw_fallback);
  assert(seen.out.block_offset_x==400 && seen.out.block_offset_y==180);
  task.area.x1--; layer.buf_area.x1=410; unit.img_sw_fallback=false;
  lv_draw_ppa_vision_scale(&task,&dsc,&task.area);
  assert(calls==3 && fallback==6 && unit.img_sw_fallback);
  return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "check.c"
    binary = Path(directory) / "check"
    source.write_text(stub + fragment.read_text() + checks)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PPA scaling: ownership, dimensions, clipping and error fallback PASS")
