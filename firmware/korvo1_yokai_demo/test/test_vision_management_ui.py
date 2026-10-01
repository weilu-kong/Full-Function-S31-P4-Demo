"""Check actual management callbacks wait for a service completion."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "main/ui/ui_apps.c").read_text()
def extract(name):
    start = source.index("static void " + name + "(")
    return source[start:source.index("\n}\n", start) + 3]
functions = ""
for name in ("manage_command_started", "manage_result_update"):
    if "static void " + name + "(" in source:
        functions += extract(name)
    else:
        functions += "static void " + name + ("(esp_err_t e) {}\n" if name == "manage_command_started" else "(const vision_result_t *r) {}\n")
functions += extract("delete_person_btn_cb") + extract("clear_all_btn_cb")
harness = r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
typedef int esp_err_t;
typedef struct { uint32_t management_sequence; esp_err_t management_error; } vision_result_t;
typedef void lv_event_t;
static void *s_lbl_manage_status = (void *)1;
static bool s_manage_pending;
static uint32_t s_manage_sequence_seen;
static int refreshes, requests;
static esp_err_t reply;
static const char *message;
static void *lv_event_get_user_data(lv_event_t *e) { return (void *)2; }
static void lv_label_set_text(void *l, const char *text) { message = text; }
static void refresh_manage_list(void) { refreshes++; }
static esp_err_t vision_service_delete_person(uint8_t s) { assert(s == 2); requests++; return reply; }
static esp_err_t vision_service_clear_all_people(void) { requests++; return reply; }
'''
checks = r'''
int main(void) {
    delete_person_btn_cb(NULL);
    assert(s_manage_pending && requests == 1 && refreshes == 0);
    clear_all_btn_cb(NULL); /* coalesce clicks while one command is pending */
    assert(requests == 1);
    vision_result_t result = {0};
    manage_result_update(&result);
    assert(s_manage_pending && refreshes == 0);
    result.management_sequence = 1;
    manage_result_update(&result);
    assert(!s_manage_pending && refreshes == 1);
    manage_result_update(&result);
    assert(refreshes == 1); /* persistent completion is delivered once */
    reply = ESP_FAIL;
    clear_all_btn_cb(NULL);
    assert(!s_manage_pending && refreshes == 1 && message);
    reply = ESP_OK;
    clear_all_btn_cb(NULL);
    result.management_sequence = 2;
    result.management_error = ESP_FAIL;
    manage_result_update(&result);
    assert(!s_manage_pending && refreshes == 2);
    assert(strcmp(message, "保存できませんでした") == 0);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "management.c"
    c.write_text(harness + functions + checks)
    exe = Path(tmp) / "management"
    subprocess.run(["cc", "-std=c11", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("Vision management completion/UI checks passed")
