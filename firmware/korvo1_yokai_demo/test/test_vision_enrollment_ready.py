"""Production enrollment commands must not be accepted while startup waits."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
def extract(source, marker):
    start = source.index(marker)
    return source[start:source.index("\n}\n", start) + 3]

service = (root / "main/vision_service.cpp").read_text()
ui = (root / "main/ui/ui_apps.c").read_text()
functions = "".join(extract(service, 'extern "C" esp_err_t vision_service_' + name + '(')
                    for name in ("begin_enrollment", "cancel_enrollment", "reregister_person"))
functions += extract(ui, "static void enroll_submit_action(")
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define VISION_FACE_NAME_MAX_BYTES 48
#define VISION_MAX_PERSONS 10
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define LV_OBJ_FLAG_HIDDEN 1
typedef int esp_err_t;
enum { VISION_STATE_OFF, VISION_STATE_STARTING, VISION_STATE_RUNNING, VISION_STATE_ERROR };
enum { VISION_CMD_BEGIN_ENROLL, VISION_CMD_CANCEL_ENROLL, VISION_CMD_REREGISTER_PERSON };
typedef struct { int type; uint8_t slot; char name[49]; } vision_cmd_t;
static struct { int person_count; struct { bool active; char name[49]; } persons[10]; } s_people_file;
static void *s_cmd_queue = (void *)1;
static int s_state, queued;
static void trim_whitespace(char *name) { (void)name; }
static void *find_person_by_name(const char *name) { (void)name; return NULL; }
static bool app_storage_is_ready(void) { return true; }
static int xQueueSend(void *q, const vision_cmd_t *cmd, int ticks) {
    (void)q; (void)cmd; (void)ticks; queued++; return pdTRUE;
}
static int vision_service_get_state(void) { return s_state; }
static void *s_ta_enroll_name = (void *)1, *s_enroll_modal = (void *)2, *s_lbl_enroll_modal_err = (void *)3;
static int s_enroll_target_slot = -1;
static bool hidden;
static const char *message;
static const char *lv_textarea_get_text(void *obj) { (void)obj; return "Alice"; }
static void lv_label_set_text(void *obj, const char *text) { (void)obj; message = text; }
static void lv_obj_add_flag(void *obj, int flag) { (void)obj; (void)flag; hidden = true; }
'''
checks = r'''
int main(void) {
    assert(vision_service_get_state() == VISION_STATE_OFF);
    s_people_file.persons[0].active = true;
    for (int state : {VISION_STATE_OFF, VISION_STATE_STARTING, VISION_STATE_ERROR}) {
        s_state = state;
        assert(vision_service_begin_enrollment("Alice") == ESP_ERR_INVALID_STATE);
        assert(vision_service_reregister_person(0, "Alice") == ESP_ERR_INVALID_STATE);
        assert(vision_service_cancel_enrollment() == ESP_ERR_INVALID_STATE);
        enroll_submit_action();
        assert(!hidden && queued == 0 && message && *message);
    }
    s_state = VISION_STATE_RUNNING;
    assert(vision_service_begin_enrollment("Alice") == ESP_OK);
    assert(vision_service_reregister_person(0, "Alice") == ESP_OK);
    assert(vision_service_cancel_enrollment() == ESP_OK);
    enroll_submit_action();
    assert(hidden && queued == 4);
}
'''
with tempfile.TemporaryDirectory(prefix="yokai-enroll-ready-") as tmp:
    path = Path(tmp) / "check.cpp"
    binary = Path(tmp) / "check"
    path.write_text("#include <initializer_list>\n" + harness + functions + checks)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Vision production enrollment readiness and preserved input checks passed")
