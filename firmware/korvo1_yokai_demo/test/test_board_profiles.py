"""Compile actual board constants for both targets; catch unsupported P4 paths."""
from pathlib import Path
import subprocess
import tempfile

project = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    (root / "sdkconfig.h").write_text("")
    for p4, channels, resampler in ((0, 2, 10), (1, 1, 20)):
        source = root / "check.c"
        source.write_text(f'''
#include <assert.h>
#define CONFIG_IDF_TARGET_ESP32P4 {p4}
#define ESP_ASRC_PERF_TYPE_HW_ONLY 10
#define ESP_ASRC_PERF_TYPE_SW_SPEED 20
#include "board_profile.h"
int main(void) {{
    assert(YOKAI_MIC_CHANNELS == {channels});
    assert(YOKAI_VOICE_ASRC == {resampler});
    assert(YOKAI_HAS_A2DP == {1-p4});
}}
''')
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-I", root, "-I", project / "main", source,
                        "-o", root / "check"], check=True)
        subprocess.run([root / "check"], check=True)
print("S31/P4X board capability checks passed")

def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]

def check(code):
    with tempfile.TemporaryDirectory() as tmp:
        source, binary = Path(tmp) / 'check.c', Path(tmp) / 'check'
        source.write_text(code)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', source, '-o', binary], check=True)
        subprocess.run([binary], check=True)

audio = (project / 'main/synth_service.c').read_text()
start = audio.index('            if (YOKAI_MIC_CHANNELS == 1) {')
end = audio.index('\n            write_result', start)
check('''
#include <assert.h>
#include <stdint.h>
#define SYNTH_CHUNK_SAMPLES 4
#define YOKAI_MIC_CHANNELS 1
int main(void) {
    int16_t s_chunk_buf[8] = {32767,32767,-32768,-32768,32767,-32768,1000,-1000};
''' + audio[start:end] + '''
    assert(s_chunk_buf[0] == 32767 && s_chunk_buf[1] == -32768);
    assert(s_chunk_buf[2] == 0 && s_chunk_buf[3] == 0);
}
''')

voice = (project / 'main/voice_service.c').read_text()
start = voice.index('    if (s_mic_dev != NULL) {')
end = voice.index('\n\n    portENTER_CRITICAL', start)
check('''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define CONFIG_IDF_TARGET_ESP32P4 1
static void *s_mic_dev;
static void *synth_service_audio_codec(void) { return (void*)1; }
static void esp_codec_dev_set_in_gain(void *dev, float gain) { assert(dev == (void*)1 && gain == 34.0f); }
''' + function(voice, 'static bool open_microphone(void)') + '''
int main(void) {
    assert(open_microphone() && s_mic_dev == (void*)1);
''' + voice[start:end] + '''
    assert(s_mic_dev == NULL);
}
''')

# Every screen factory must use the same native-size root/content layout.
ui = project / "main/ui"
assert "lv_obj_create(NULL)" not in "".join(p.read_text() for p in ui.glob("ui_*.c") if p.name != "ui_theme.c")
assert "lv_obj_get_screen(s_screen_objs[" in (ui / "ui.c").read_text()
assert "ui_screen_create" in (ui / "ui_theme.c").read_text()

theme = (ui / 'ui_theme.c').read_text()
check('''
#include <assert.h>
#include <stddef.h>
typedef struct obj {struct obj *parent; int w,h,x,y;} lv_obj_t;
typedef int lv_display_t;
static int width,height,objects;
static lv_obj_t pool[8];
#define UI_COLOR_BG_DARK 0
#define LV_OPA_COVER 255
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_OBJ_FLAG_CLICKABLE 2
static lv_display_t *lv_display_get_default(void) { return NULL; }
static int lv_display_get_horizontal_resolution(lv_display_t *d) { (void)d; return width; }
static int lv_display_get_vertical_resolution(lv_display_t *d) { (void)d; return height; }
static lv_obj_t *lv_obj_create(lv_obj_t *p) { lv_obj_t *o=&pool[objects++]; *o=(lv_obj_t){.parent=p,.w=width,.h=height}; return o; }
static void lv_obj_set_size(lv_obj_t *o,int w,int h) { o->w=w;o->h=h; }
static void lv_obj_center(lv_obj_t *o) { assert(o->parent);o->x=(o->parent->w-o->w)/2;o->y=(o->parent->h-o->h)/2; }
#define lv_obj_remove_style_all(...) ((void)0)
#define lv_obj_remove_flag(...) ((void)0)
#define lv_obj_set_style_bg_color(...) ((void)0)
#define lv_obj_set_style_bg_opa(...) ((void)0)
#define lv_obj_set_style_pad_all(...) ((void)0)
#define lv_obj_set_style_border_width(...) ((void)0)
''' + function(theme, 'lv_obj_t *ui_content_create(') + function(theme, 'lv_obj_t *ui_screen_create(') + '''
int main(void) {
    width=800;height=480;
    lv_obj_t *s31=ui_screen_create(); assert(!s31->parent && objects==1);
    width=1024;height=600;
    lv_obj_t *p4=ui_screen_create(); assert(p4->parent && objects==3);
    assert(p4->w==800 && p4->h==480 && p4->x==112 && p4->y==60);
    assert(p4->parent->w==1024 && p4->parent->h==600);
}
''')

# Exercise the actual IPA generator with multiple filenames containing spaces.
ipa = project / 'managed_components/espressif__esp_ipa/tools/config/esp_ipa_config.py'
if ipa.is_file():
    import sys
    with tempfile.TemporaryDirectory(prefix='yokai ipa ') as tmp:
        files = [Path(tmp) / name for name in ('first config.json', 'second config.json')]
        config = project / 'managed_components/espressif__esp_cam_sensor/sensors/sc2336/cfg/sc2336_default_p4_eco5.json'
        files[0].write_text(config.read_text())
        files[1].write_text('{"version": 1}')
        subprocess.run([sys.executable, ipa, '-i', *files, '-o', Path(tmp) / 'out.c', '-v', '1'], check=True)
print('Mono audio, shared codec, native viewport and IPA path checks passed')
