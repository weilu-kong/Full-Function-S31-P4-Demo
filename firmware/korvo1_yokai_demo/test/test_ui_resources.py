"""Exercise the real background loader lifecycle with a stub JPEG decoder."""
from pathlib import Path
import subprocess
import tempfile

project = Path(__file__).resolve().parents[1]
source = (project / "main/ui/ui_image_loader.c").read_text()

def extract(signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]

functions = extract("esp_err_t ui_images_init(") + extract("esp_err_t ui_weather_background_load(")
if "void ui_weather_background_free(" in source:
    functions += extract("void ui_weather_background_free(")
else:
    functions += "void ui_weather_background_free(void) {}\n"

harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_LOGI(...)
typedef int esp_err_t;
typedef enum { WEATHER_COND_SUNNY, WEATHER_COND_CLOUDY, WEATHER_COND_RAINY,
               WEATHER_COND_THUNDER, WEATHER_COND_SNOWY } weather_cond_t;
typedef struct { const void *data; } lv_image_dsc_t;
static lv_image_dsc_t ui_img_home_p1, ui_img_home_p2, ui_img_weather_sunny;
static int s_weather_background = -1, weather_decodes, allocations, frees;
static bool fail_decode, cache_dropped;
#define JPEG(name) static const uint8_t name##_jpg[] = {0}; static const size_t name##_jpg_len = 1
JPEG(ui_img_home_p1); JPEG(ui_img_home_p2); JPEG(ui_img_weather_sunny);
JPEG(ui_img_weather_cloudy); JPEG(ui_img_weather_rain); JPEG(ui_img_weather_night);
static esp_err_t decode_jpeg_to_dsc(const char *n, const uint8_t *j, size_t l, lv_image_dsc_t *d) {
    if (fail_decode) return ESP_ERR_NO_MEM;
    if (d == &ui_img_weather_sunny) weather_decodes++;
    if (!d->data) { d->data = malloc(800*480*2); assert(d->data); allocations++; }
    cache_dropped = false;
    return ESP_OK;
}
static void lv_image_cache_drop(lv_image_dsc_t *d) { assert(d->data); cache_dropped = true; }
static void heap_caps_free(void *p) { assert(cache_dropped); free(p); frees++; }
'''
checks = r'''
int main(void) {
    assert(ui_images_init() == ESP_OK);
    assert(ui_img_home_p1.data && ui_img_home_p2.data);
    assert(weather_decodes == 0 && ui_img_weather_sunny.data == NULL);
    for (int i = 0; i < 20; i++) {
        int before = allocations;
        assert(ui_weather_background_load(WEATHER_COND_SUNNY, true) == ESP_OK);
        assert(ui_img_weather_sunny.data && allocations == before + 1);
        assert(ui_weather_background_load(WEATHER_COND_SUNNY, true) == ESP_OK);
        assert(allocations == before + 1);
        assert(ui_weather_background_load(WEATHER_COND_RAINY, true) == ESP_OK);
        assert(allocations == before + 1);
        ui_weather_background_free();
        assert(ui_img_weather_sunny.data == NULL && s_weather_background == -1);
        int released = frees;
        ui_weather_background_free();
        assert(frees == released);
    }
    fail_decode = true;
    assert(ui_weather_background_load(WEATHER_COND_SUNNY, true) == ESP_ERR_NO_MEM);
    assert(ui_img_weather_sunny.data == NULL && s_weather_background == -1);
    fail_decode = false;
    assert(ui_weather_background_load(WEATHER_COND_SUNNY, true) == ESP_OK);
    ui_weather_background_free();
    assert(allocations - frees == 2); /* only the two home buffers remain */
    free((void *)ui_img_home_p1.data); free((void *)ui_img_home_p2.data);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "resources.c"
    c.write_text(harness + functions + checks)
    exe = Path(tmp) / "resources"
    subprocess.run(["cc", "-std=c11", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("UI background lazy-load/free/reload checks passed")
