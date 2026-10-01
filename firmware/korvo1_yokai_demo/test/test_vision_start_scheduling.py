"""Exercise the actual UI startup scheduler with TLS contention and failures."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "main/ui/ui.c").read_text()
def extract(name):
    start = source.index("static void " + name + "(")
    return source[start:source.index("\n}\n", start) + 3]

functions = extract("vision_start_request") + extract("vision_start_tick")
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_FAIL -1
typedef int esp_err_t;
typedef struct { bool refreshing; } weather_info_t;
static bool s_vision_start_pending;
static uint32_t s_vision_start_requested_at, s_vision_start_attempt_at;
static uint32_t now;
static bool busy;
static int starts, stops, errors, toasts;
static esp_err_t reply;
static uint32_t lv_tick_get(void) { return now; }
static uint32_t lv_tick_elaps(uint32_t tick) { return now - tick; }
static void weather_service_get_info(weather_info_t *info) { info->refreshing = busy; }
static esp_err_t vision_service_start(void) { starts++; return reply; }
static void vision_service_stop(void) { stops++; }
static void ui_vision_show_start_error(void) { errors++; }
static void show_voice_toast(const char *text) { assert(text && *text); toasts++; }
'''
checks = r'''
int main(void) {
    busy = true;
    vision_start_request();
    now = 1000;
    vision_start_tick();
    assert(starts == 0 && s_vision_start_pending);
    busy = false;
    vision_start_tick();
    assert(starts == 1 && !s_vision_start_pending && errors == 0);
    vision_start_tick();
    assert(starts == 1); /* never start twice after success */

    vision_start_request();
    s_vision_start_pending = false; /* navigation cancels before service stop */
    vision_start_tick();
    assert(starts == 1);

    reply = ESP_ERR_NO_MEM;
    vision_start_request();
    vision_start_tick();
    assert(starts == 2 && stops == 1 && s_vision_start_pending);
    now += 499;
    vision_start_tick();
    assert(starts == 2);
    now++;
    vision_start_tick();
    assert(starts == 3 && stops == 2);
    reply = ESP_OK;
    now += 500;
    vision_start_tick();
    assert(starts == 4 && !s_vision_start_pending);

    reply = ESP_FAIL;
    vision_start_request();
    vision_start_tick();
    assert(starts == 5 && !s_vision_start_pending && errors == 1 && toasts == 1);

    busy = true;
    now = UINT32_MAX - 1000;
    vision_start_request();
    now += 29999;
    vision_start_tick();
    assert(s_vision_start_pending && starts == 5);
    now++;
    vision_start_tick();
    assert(!s_vision_start_pending && errors == 2 && toasts == 2);
    vision_start_tick();
    assert(errors == 2); /* one terminal notification */
}
'''
with tempfile.TemporaryDirectory(prefix="yokai-start-") as tmp:
    path = Path(tmp) / "check.c"
    binary = Path(tmp) / "check"
    path.write_text(harness + functions + checks)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Vision startup contention, cancellation, bounded retry and timeout checks passed")
