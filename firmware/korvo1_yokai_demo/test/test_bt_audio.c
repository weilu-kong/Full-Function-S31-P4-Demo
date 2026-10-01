/* Host checks for the actual consumer, with only RTOS/ASRC boundaries faked. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef void *RingbufHandle_t;
typedef void *esp_asrc_handle_t;
typedef int esp_asrc_err_t;
typedef struct { uint32_t sample_rate; uint8_t channel, bits_per_sample; } audio_info_t;
typedef struct { audio_info_t src_info, dest_info; int perf_type, complexity; } esp_asrc_cfg_t;
#define pdTRUE 1
#define ESP_ASRC_ERR_OK 0
#define ESP_ASRC_PERF_TYPE_SW_SPEED 3
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)

static unsigned char fifo[100000];
static size_t fifo_read, fifo_write, fragment_size = 137;
static TickType_t ticks;
static bool fail_asrc;
static esp_asrc_cfg_t asrc_cfg;
static uint32_t asrc_fraction;
static RingbufHandle_t s_bt_ringbuf = fifo;
static bool s_bt_streaming = true;
static int s_bt_lock;
static uint32_t s_bt_epoch = 1;
static uint32_t s_bt_input_rate = 44100;
static uint8_t s_bt_input_channels = 2;
static struct {
    uint64_t bt_flushed_bytes, bt_rx_bytes, bt_dropped_bytes;
    uint32_t bt_overflow_count;
    uint32_t bt_asrc_errors, bt_fifo_high_watermark, bt_underflow_count, bt_short_read_count;
} s_stats;

static int xRingbufferSend(RingbufHandle_t handle, const void *data, uint32_t len, TickType_t wait)
{
    (void)handle; (void)wait;
    if (fifo_write + len > 40 * 1024) return 0;
    memcpy(fifo + fifo_write, data, len);
    fifo_write += len;
    return pdTRUE;
}
static void vRingbufferGetInfo(RingbufHandle_t handle, void *a, void *b, void *c, void *d, UBaseType_t *fill)
{
    (void)handle; (void)a; (void)b; (void)c; (void)d;
    *fill = fifo_write - fifo_read;
}
static void *xRingbufferReceiveUpTo(RingbufHandle_t handle, size_t *bytes, TickType_t wait, size_t max)
{
    (void)handle;
    *bytes = fifo_write - fifo_read;
    if (*bytes > max) *bytes = max;
    if (*bytes > fragment_size) *bytes = fragment_size;
    if (!*bytes) { ticks += wait; return NULL; }
    void *item = fifo + fifo_read;
    fifo_read += *bytes;
    return item;
}
static void vRingbufferReturnItem(RingbufHandle_t handle, void *item) { (void)handle; (void)item; }
static TickType_t xTaskGetTickCount(void) { return ticks; }
static void esp_asrc_close(esp_asrc_handle_t handle) { (void)handle; }
static esp_asrc_err_t esp_asrc_open(esp_asrc_cfg_t *cfg, esp_asrc_handle_t *handle)
{
    if (fail_asrc) return -2;
    assert(cfg->perf_type == ESP_ASRC_PERF_TYPE_SW_SPEED);
    asrc_cfg = *cfg; asrc_fraction = 0; *handle = &asrc_cfg;
    return 0;
}
static esp_asrc_err_t esp_asrc_process(esp_asrc_handle_t handle, uint8_t *in, uint32_t frames,
                                      uint8_t *out, uint32_t *capacity)
{
    (void)handle;
    uint32_t count = (frames * 44100 + asrc_fraction) / asrc_cfg.src_info.sample_rate;
    asrc_fraction = (frames * 44100 + asrc_fraction) % asrc_cfg.src_info.sample_rate;
    assert(count <= *capacity);
    /* Constant channel fixtures: verify framing/channel order and fractional
       output retention, not the numerical quality of Espressif's ASRC. */
    for (uint32_t i = 0; i < count; ++i) {
        ((int16_t *)out)[i * 2] = ((int16_t *)in)[0];
        ((int16_t *)out)[i * 2 + 1] = ((int16_t *)in)[asrc_cfg.src_info.channel == 2 ? 1 : 0];
    }
    *capacity = count;
    return 0;
}

#include "bt_audio_under_test.h"

static void append_pcm(unsigned frames, uint8_t channels)
{
    for (unsigned i = 0; i < frames; ++i) {
        int16_t samples[2] = {1000, -2000};
        assert(fifo_write + channels * 2 <= sizeof(fifo));
        memcpy(fifo + fifo_write, samples, channels * 2);
        fifo_write += channels * 2;
    }
}
static void reset_stream(uint32_t rate, uint8_t channels)
{
    fifo_read = fifo_write = 0;
    memset(&s_stats, 0, sizeof(s_stats));
    s_bt_input_rate = rate; s_bt_input_channels = channels; ++s_bt_epoch;
    assert(!bt_render_chunk());
}
static void assert_chunk(int16_t right)
{
    for (int i = 0; i < SYNTH_CHUNK_SAMPLES; ++i) {
        assert(s_bt_buf[i * 2] == 1000);
        assert(s_bt_buf[i * 2 + 1] == right);
    }
}
int main(void)
{
    unsigned char packet[2048] = {0};
    fifo_read = fifo_write = 0;
    for (int i = 0; i < 20; ++i) bt_a2dp_data_cb(packet, sizeof(packet));
    assert(s_stats.bt_rx_bytes == 40960 && s_stats.bt_overflow_count == 0);
    assert(s_stats.bt_fifo_high_watermark == 40960);
    bt_a2dp_data_cb(packet, sizeof(packet));
    assert(s_stats.bt_rx_bytes == 43008 && s_stats.bt_overflow_count == 1);
    assert(s_stats.bt_dropped_bytes == 2048 && fifo_write == 40960);
    reset_stream(44100, 2);
    append_pcm(100, 2);
    assert(!bt_render_chunk() && s_stats.bt_underflow_count == 0); /* prebuffer */
    append_pcm(1500, 2);
    for (int i = 0; i < 6; ++i) { assert(bt_render_chunk()); assert_chunk(-2000); }
    assert(s_stats.bt_short_read_count > 0 && s_stats.bt_underflow_count == 0);
    /* 64 input frames remain; a genuine timeout retains them for next call. */
    assert(!bt_render_chunk() && s_stats.bt_underflow_count == 1);
    assert(s_bt_input_bytes == 64 * 4);
    append_pcm(1500, 2);
    assert(bt_render_chunk()); assert_chunk(-2000);
    assert(s_stats.bt_underflow_count == 1);

    reset_stream(48000, 2);
    append_pcm(8000, 2);
    for (int i = 0; i < 20; ++i) { assert(bt_render_chunk()); assert_chunk(-2000); }
    assert(s_stats.bt_underflow_count == 0 && s_bt_output_frames < 256);
    assert(fifo_read == 22 * 256 * 4); /* fractional output is carried */

    reset_stream(44100, 1);
    append_pcm(2000, 1);
    assert(bt_render_chunk()); assert_chunk(1000);

    s_bt_streaming = false; ++s_bt_epoch;
    assert(!bt_render_chunk());
    assert(s_bt_output_frames == 0 && s_bt_input_bytes == 0 && fifo_read == fifo_write);
    assert(s_stats.bt_flushed_bytes > 0);
    s_bt_streaming = true;
    fail_asrc = true;
    reset_stream(48000, 2);
    append_pcm(2000, 2);
    assert(!bt_render_chunk() && s_stats.bt_asrc_errors == 1);
    assert(!bt_render_chunk() && s_stats.bt_asrc_errors == 1); /* no open retry flood */
    puts("Bluetooth framing, timeout retention, rate and transition checks passed.");
}
