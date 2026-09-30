"""Compile and exercise the real generated driver's raw-frame size function."""
import os
from pathlib import Path
import subprocess
import tempfile

project = Path(__file__).resolve().parents[1]
if not os.environ.get("IDF_PATH"):
    raise SystemExit("Set IDF_PATH to the ESP-IDF used to build this firmware")
idf = Path(os.environ["IDF_PATH"])
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    driver = root / "camera.c"
    subprocess.run(["python3", str(project / "tools/prepare_camera_driver.py"),
                    str(idf), str(driver)], check=True)
    text = driver.read_text()
    def extract(signature):
        start = text.index(signature)
        return text[start:text.index("\n}\n", start) + 3]
    function = extract("static IRAM_ATTR esp_err_t esp_cam_ctlr_dvp_start_trans(")
    function += extract("static uint32_t IRAM_ATTR esp_cam_ctlr_dvp_get_recved_size(")
    function += extract("static IRAM_ATTR bool esp_cam_ctlr_recv_frame_done_isr(")
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define IRAM_ATTR
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ALIGN_UP_BY(n,a) (((n)+(a)-1)&~((a)-1))
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C 0
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 1
#define ESP_RETURN_ON_ERROR(x,...) do { int ret=(x); if(ret) return ret; } while(0)
#define ESP_RETURN_ON_ERROR_ISR ESP_RETURN_ON_ERROR
#define TAG "camera_test"
typedef int esp_err_t;
typedef void *gdma_channel_handle_t;
typedef int gdma_event_data_t;
typedef struct { uint8_t *buffer; size_t buflen, received_size; } esp_cam_ctlr_trans_t;
typedef struct {
    bool pic_format_jpeg; uint32_t fb_size_in_bytes;
    uint8_t *cur_buf, *backup_buffer;
    bool bk_buffer_dis, bk_buffer_exposed;
    int dma, base;
    void *cbs_user_data;
    struct {
        bool (*on_get_new_trans)(void *, esp_cam_ctlr_trans_t *, void *);
        bool (*on_trans_finished)(void *, esp_cam_ctlr_trans_t *, void *);
    } cbs;
} esp_cam_ctlr_dvp_cam_t;
static int synced;
static uint8_t *queued, *dma_buffer, *published;
static size_t received, published_size;
static unsigned publishes;
static bool esp_ptr_external_ram(void *p) { return true; }
static int esp_cache_msync(void *p, size_t n, int flags) { assert(n > 0); synced++; return 0; }
static uint32_t esp_cam_ctlr_dvp_get_jpeg_size(const uint8_t *p, uint32_t n) { return n; }
static int esp_cam_ctlr_dvp_dma_stop(int *dma) { return 0; }
static int esp_cam_ctlr_dvp_dma_reset(int *dma) { return 0; }
static int esp_cam_ctlr_dvp_dma_start(int *dma, uint8_t *p, size_t n) { assert(p); dma_buffer=p; return 0; }
static size_t esp_cam_ctlr_dvp_dma_get_recv_size(int *dma) { return received; }
static bool get_buffer(void *h, esp_cam_ctlr_trans_t *t, void *u) {
    t->buffer=queued; t->buflen=1843200; queued=NULL; return t->buffer != NULL;
}
static bool finish(void *h, esp_cam_ctlr_trans_t *t, void *u) {
    published=t->buffer; published_size=t->received_size; publishes++; return true;
}
'''
    main = r'''
int main(void) {
    esp_cam_ctlr_dvp_cam_t c = {.fb_size_in_bytes=1843200};
    uint8_t data = 0;
    assert(esp_cam_ctlr_dvp_get_recved_size(&c, &data, 1843200) == 1843200);
    assert(synced == 1);
    assert(esp_cam_ctlr_dvp_get_recved_size(&c, &data, 1752647) == 0);
    assert(esp_cam_ctlr_dvp_get_recved_size(&c, &data, 0) == 0);
    assert(esp_cam_ctlr_dvp_get_recved_size(&c, &data, 1843201) == 0);
    assert(synced == 1);
    c.pic_format_jpeg = true;
    assert(esp_cam_ctlr_dvp_get_recved_size(&c, &data, 12345) == 12345);
    c.pic_format_jpeg=false;
    c.bk_buffer_dis=true;
    c.cbs.on_get_new_trans=get_buffer;
    c.cbs.on_trans_finished=finish;
    uint8_t first, second;
    c.cur_buf=&first;
    received=1843200;
    /* No queued buffer: retain ownership and recycle, never publish to readers. */
    assert(!esp_cam_ctlr_recv_frame_done_isr(NULL, NULL, &c));
    assert(c.cur_buf == &first && dma_buffer == &first && publishes == 0);
    /* A returned buffer resumes ordinary completed-frame publication. */
    queued=&second;
    assert(esp_cam_ctlr_recv_frame_done_isr(NULL, NULL, &c));
    assert(c.cur_buf == &second && published == &first && published_size == 1843200);
    received=1752647; queued=&first;
    assert(esp_cam_ctlr_recv_frame_done_isr(NULL, NULL, &c));
    assert(published == &second && published_size == 0);
    /* Initial start with no buffer returns an error without touching NULL. */
    assert(esp_cam_ctlr_dvp_start_trans(&c, NULL) == ESP_ERR_NOT_FOUND);
}
'''
    source = root / "check.c"
    source.write_text(harness + function + main)
    binary = root / "check"
    subprocess.run(["cc", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Camera DMA completeness and buffer-starvation checks passed")
