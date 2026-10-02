#!/usr/bin/env python3
"""Run real audio lifecycle C functions against minimal RTOS/driver boundaries."""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]

def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]

def run(name, code):
    with tempfile.TemporaryDirectory() as tmp:
        source, binary = Path(tmp) / 'check.c', Path(tmp) / 'check'
        source.write_text(code)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print(name + ' PASS')

common = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
'''
voice = (ROOT / 'main/voice_service.c').read_text()
voice_stub = r'''
#define VOICE_IO_FRAMES 1
#define YOKAI_MIC_CHANNELS 2
#define VOICE_REF_RING_FRAMES 8
#define VOICE_AEC_DELAY_FRAMES 2
#define MALLOC_CAP_SPIRAM 0
#define ESP_CODEC_DEV_OK 0
#define ESP_ASRC_ERR_OK 0
typedef void *TaskHandle_t;
typedef int voice_event_t;
typedef int voice_command_t;
typedef int voice_language_t;
typedef struct {int event,command,language;float confidence;} voice_result_t;
enum {VOICE_EVENT_ERROR=1,VOICE_EVENT_COMMAND=2,VOICE_COMMAND_NONE=0,VOICE_LANGUAGE_ENGLISH=0};
static bool s_ready;
static char s_error[96];
static int16_t *s_ref_ring;
static void *s_result_queue,*s_models,*s_mn_data,*s_afe_data,*s_mic_asrc,*s_ref_asrc,*s_mic_dev;
static TaskHandle_t s_feed_task,s_fetch_task,current_task=(void*)1;
static size_t s_ref_read,s_ref_write,s_ref_count;
static struct { int16_t *mic_44k,*ref_stereo_44k,*ref_44k,*mic_16k,*ref_16k,*afe_frame;uint32_t mic_out_bytes,ref_out_bytes;int afe_frames;} s_feed;
static int s_ref_lock;
static void (*before_lock)(void);
static void enter_lock(int *lock) {if(before_lock){void (*hook)(void)=before_lock;before_lock=NULL;hook();}assert(!*lock);*lock=1;}
static void exit_lock(int *lock) {assert(*lock);*lock=0;}
#define portENTER_CRITICAL(lock) enter_lock(lock)
#define portEXIT_CRITICAL(lock) exit_lock(lock)
static int queued,queue_deletes,buffer_frees,reads,asrc_calls,fail_kind,task_creates,fail_create;
static voice_result_t queued_result,queue_items[8];
static jmp_buf task_exit;
static void vTaskDelete(void *task) { if(!task || task==current_task) longjmp(task_exit,1); }
static void vTaskSuspend(void *task) { assert(!task); longjmp(task_exit,2); }
static void vTaskDelay(int ticks) { (void)ticks; }
static int xQueueSend(void *q,const void *result,int wait) { assert(q);(void)wait;if(((const voice_result_t*)result)->event==VOICE_EVENT_ERROR)assert(!s_ready && !s_fetch_task);if(queued==8)return 0;queued_result=*(const voice_result_t*)result;queue_items[queued++]=queued_result;return 1; }
static int xQueueReceive(void *q,void *result,int wait) { assert(q);(void)wait;if(!queued)return 0;*(voice_result_t*)result=queue_items[0];--queued;memmove(queue_items,queue_items+1,queued*sizeof(*queue_items));return 1; }
static void vQueueDelete(void *q) {assert(q);++queue_deletes;queued=0;}
static void *xQueueCreate(int count,size_t size) {(void)count;(void)size;queued=0;return (void*)3;}
static void *heap_caps_calloc(size_t n,size_t size,int caps) {(void)caps;return calloc(n,size);}
static void esp_mn_commands_free(void) {}
static void destroy(void *data) {(void)data;}
static int feed(void *data,void *frame) {(void)data;(void)frame;return 0;}
static struct {void (*destroy)(void*);} mn_iface={destroy},*s_mn=&mn_iface;
static struct {void (*destroy)(void*);int (*feed)(void*,void*);} afe_iface={destroy,feed},*s_afe=&afe_iface;
static void esp_srmodel_deinit(void *m) {(void)m;}
static void esp_asrc_close(void *m) {(void)m;}
static void esp_codec_dev_close(void *m) {(void)m;}
static int esp_codec_dev_read(void *dev,void *buffer,int bytes) {(void)dev;(void)buffer;(void)bytes;++reads;return fail_kind==1 ? -1 : 0;}
static int esp_asrc_process(void *dev,uint8_t *in,int n,uint8_t *out,uint32_t *frames) {(void)dev;(void)in;(void)n;(void)out;(void)frames;++asrc_calls;return -1;}
static bool open_microphone(void) {s_mic_dev=(void*)5;return true;}
static bool open_asrc(void) {s_mic_asrc=(void*)6;s_ref_asrc=(void*)7;return true;}
static bool open_speech_models(void) {s_models=(void*)8;s_mn_data=(void*)9;s_afe_data=(void*)10;return true;}
static bool register_commands(void) {return true;}
static int16_t samples[8];
static bool allocate_feed_buffers(void) {s_feed.mic_44k=s_feed.ref_stereo_44k=s_feed.ref_44k=s_feed.mic_16k=s_feed.ref_16k=s_feed.afe_frame=samples;s_feed.mic_out_bytes=s_feed.ref_out_bytes=2;return true;}
static void free_feed_buffers(void) {++buffer_frees;memset(&s_feed,0,sizeof(s_feed));}
static void voice_fetch_task(void *arg) {(void)arg;}
static int xTaskCreatePinnedToCore(void (*task)(void*),const char *name,int stack,void *arg,int priority,void **handle,int core) {(void)task;(void)name;(void)stack;(void)arg;(void)priority;(void)core;if(++task_creates==fail_create)return 0;*handle=(void*)(uintptr_t)task_creates;return pdPASS;}
static void voice_service_cleanup_start_failure(void);
'''
voice_code = common + voice_stub + function(voice,'static bool fail_start(')
voice_code += function(voice,'void voice_service_feed_playback(') + function(voice,'static void read_reference(')
for signature in ('static void publish_event(', 'static void fail_runtime('):
    if signature in voice: voice_code += function(voice, signature)
voice_code += function(voice,'static void voice_feed_task(')
for signature in ('static void voice_service_cleanup_start_failure(void)\n{','void voice_service_stop(','bool voice_service_start(','bool voice_service_receive('):
    voice_code += function(voice,signature)
voice_code += r'''
int main(void) {
    for (fail_kind=1;fail_kind<=2;++fail_kind) {
        current_task=(void*)99;
        assert(voice_service_start());
        int created=task_creates;
        current_task=s_feed_task;
        queued=8;memset(queue_items,0,sizeof(queue_items)); /* terminal error must survive a full queue */
        int deleted=queue_deletes;
        if(setjmp(task_exit)==0) voice_feed_task(NULL);
        assert(!s_ready);
        assert(strstr(s_error,fail_kind==1 ? "Microphone" : "resampler"));
        assert(queue_deletes==deleted && s_result_queue);
        assert(queued_result.event==VOICE_EVENT_ERROR);
        voice_result_t result;int errors=0;while(voice_service_receive(&result))errors+=result.event==VOICE_EVENT_ERROR;
        assert(errors==1 && !voice_service_receive(NULL));
        assert(s_fetch_task==NULL);
        assert(s_feed_task==current_task); /* parked until owned cleanup */
        current_task=(void*)99;
        assert(voice_service_start()); /* cleans failed instance before restart */
        assert(s_ready && s_error[0]=='\0' && queue_deletes==deleted+1);
        created=task_creates;assert(voice_service_start() && task_creates==created);
        voice_service_stop();
        voice_service_stop(); /* repeated stop */
        assert(!s_ready && !s_result_queue && !s_feed_task && !s_fetch_task);
    }
    assert(reads>=200 && asrc_calls>=100 && buffer_frees>0);
    current_task=(void*)99;
    assert(voice_service_start());
    int16_t pcm[40];for(int i=0;i<40;++i)pcm[i]=i;
    voice_service_feed_playback(pcm,20);
    assert(s_ref_count==VOICE_REF_RING_FRAMES && s_ref_ring[0]==24 && s_ref_ring[15]==39);
    before_lock=voice_service_stop; /* stop between readiness check and ring access */
    voice_service_feed_playback(pcm,1);
    assert(!s_ref_ring && !s_ready && !s_ref_lock);
    voice_service_feed_playback(pcm,1); /* stopped playback is a no-op */
    assert(voice_service_start());
    voice_service_feed_playback(pcm,1);
    assert(s_ref_count==1 && s_ref_ring[0]==0 && s_ref_ring[1]==1);
    voice_service_stop();
    fail_create=task_creates+2; /* second task fails after feed handle exists */
    assert(!voice_service_start());
    assert(!s_ready && !s_feed_task && !s_fetch_task && !s_result_queue && !s_ref_ring);
    assert(strstr(s_error,"fetch task"));
    fail_create=0;
    assert(voice_service_start());
    voice_service_stop();
    return 0;
}
'''
# Keep the unused RTOS suspend boundary legal on the pre-fix revision.
voice_code = voice_code.replace('int main(void) {','int main(void) {\n    (void)vTaskSuspend;',1)

bt = (ROOT / 'main/synth_service.c').read_text()
bt_stub = r'''
static void bt_enter(int *lock);
static void bt_exit(int *lock);
#define portENTER_CRITICAL(lock) bt_enter(lock)
#define portEXIT_CRITICAL(lock) bt_exit(lock)
enum {ESP_A2D_CONNECTION_STATE_EVT,ESP_A2D_AUDIO_STATE_EVT,ESP_A2D_AUDIO_CFG_EVT,ESP_A2D_CONNECTION_STATE_CONNECTED,ESP_A2D_CONNECTION_STATE_DISCONNECTED,ESP_A2D_AUDIO_STATE_STARTED,ESP_A2D_MCT_SBC,ESP_A2D_SBC_CIE_SF_44K,ESP_A2D_SBC_CIE_SF_48K,ESP_A2D_SBC_CIE_SF_32K,ESP_A2D_SBC_CIE_SF_16K,ESP_A2D_SBC_CIE_CH_MODE_MONO};
enum {ESP_BT_NON_CONNECTABLE,ESP_BT_CONNECTABLE,ESP_BT_NON_DISCOVERABLE,ESP_BT_GENERAL_DISCOVERABLE};
typedef uint8_t esp_bd_addr_t[6];
typedef int esp_a2d_cb_event_t;
typedef struct {int type;struct {struct {int samp_freq,ch_mode;} sbc_info;} cie;} esp_a2d_mcc_t;
typedef struct {struct {int state;esp_bd_addr_t remote_bda;} conn_stat;struct {int state;} audio_stat;struct {esp_a2d_mcc_t mcc;} audio_cfg;} esp_a2d_cb_param_t;
static bool s_bt_enabled=true,s_bt_connected,s_bt_streaming,s_bt_inited=true;
static uint32_t s_bt_epoch,s_bt_input_rate;
static uint8_t s_bt_input_channels;
static int s_bt_lock,scan,discover,disconnects,sends;
static esp_bd_addr_t s_remote_bda;
static void *s_bt_ringbuf=(void*)1;
static struct {uint32_t bt_fifo_high_watermark,bt_overflow_count;uint64_t bt_rx_bytes,bt_dropped_bytes;} s_stats;
void synth_service_set_bt_enabled(bool enabled);
static void (*before_gap)(void), (*after_enabled_read)(void), (*deferred)(void);
static void bt_enter(int *lock) {assert(!*lock);*lock=1;}
static void bt_exit(int *lock) {assert(*lock);*lock=0;if(deferred){void (*hook)(void)=deferred;deferred=NULL;hook();}}
static bool read_enabled(void) {bool value=s_bt_enabled;if(after_enabled_read){void (*hook)(void)=after_enabled_read;after_enabled_read=NULL;if(s_bt_lock)deferred=hook;else hook();}return value;}
static int gap_calls;
static int esp_bt_gap_set_scan_mode(int connect,int discovery) {++gap_calls;if(before_gap){void (*hook)(void)=before_gap;before_gap=NULL;hook();}scan=connect;discover=discovery;return 0;}
static int esp_a2d_sink_disconnect(uint8_t *bda) {assert(bda[0]==42);++disconnects;return 0;}
static void synth_service_bt_a2dp_init(void) {s_bt_inited=true;}
static int xRingbufferSend(void *buf,const void *data,uint32_t len,int timeout) {(void)buf;(void)data;(void)len;(void)timeout;++sends;return 1;}
static uint32_t bt_fifo_fill(void) {return 0;}
'''
bt_code = common + bt_stub
if 'static void bt_update_scan_mode(' in bt: bt_code += function(bt,'static void bt_update_scan_mode(')
for signature in ('static void bt_a2dp_data_cb(const uint8_t *data, uint32_t len)\n{','static void bt_stream_changed(', 'static void bt_a2dp_event_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)\n{','void synth_service_set_bt_enabled('):
    if signature not in bt: continue
    body = function(bt,signature)
    if 'bt_a2dp_event_cb' in signature: body = body.replace('s_bt_enabled', 'read_enabled()')
    bt_code += body
bt_code += r'''
static void turn_off(void) {synth_service_set_bt_enabled(false);}
static void turn_on(void) {synth_service_set_bt_enabled(true);}
int main(void) {
    esp_a2d_cb_param_t p={0};
    p.conn_stat.state=ESP_A2D_CONNECTION_STATE_CONNECTED;p.conn_stat.remote_bda[0]=42;
    bt_a2dp_event_cb(ESP_A2D_CONNECTION_STATE_EVT,&p);
    assert(s_bt_connected);
    p.audio_stat.state=ESP_A2D_AUDIO_STATE_STARTED;
    bt_a2dp_event_cb(ESP_A2D_AUDIO_STATE_EVT,&p);
    assert(s_bt_streaming);
    synth_service_set_bt_enabled(false);
    assert(!s_bt_streaming);
    p.conn_stat.state=ESP_A2D_CONNECTION_STATE_DISCONNECTED;
    bt_a2dp_event_cb(ESP_A2D_CONNECTION_STATE_EVT,&p);
    assert(scan==ESP_BT_NON_CONNECTABLE && discover==ESP_BT_NON_DISCOVERABLE);
    p.conn_stat.state=ESP_A2D_CONNECTION_STATE_CONNECTED;
    bt_a2dp_event_cb(ESP_A2D_CONNECTION_STATE_EVT,&p);
    assert(!s_bt_connected && disconnects==2);
    bt_a2dp_event_cb(ESP_A2D_AUDIO_STATE_EVT,&p);
    assert(!s_bt_streaming);
    uint8_t pcm[4]={0};bt_a2dp_data_cb(pcm,sizeof(pcm));assert(sends==0);
    synth_service_set_bt_enabled(true);
    assert(scan==ESP_BT_CONNECTABLE && discover==ESP_BT_GENERAL_DISCOVERABLE);
    bt_a2dp_event_cb(ESP_A2D_CONNECTION_STATE_EVT,&p);
    bt_a2dp_event_cb(ESP_A2D_AUDIO_STATE_EVT,&p);
    bt_a2dp_data_cb(pcm,sizeof(pcm));assert(s_bt_connected && s_bt_streaming && sends==1);
    p.conn_stat.state=ESP_A2D_CONNECTION_STATE_DISCONNECTED;
    before_gap=turn_off; /* OFF is queued inside the stale callback's ON call */
    bt_a2dp_event_cb(ESP_A2D_CONNECTION_STATE_EVT,&p);
    assert(!s_bt_enabled && scan==ESP_BT_NON_CONNECTABLE && discover==ESP_BT_NON_DISCOVERABLE);
    before_gap=turn_on; /* opposite transition: stale OFF must not win */
    bt_a2dp_event_cb(ESP_A2D_CONNECTION_STATE_EVT,&p);
    assert(s_bt_enabled && scan==ESP_BT_CONNECTABLE && discover==ESP_BT_GENERAL_DISCOVERABLE);
    assert(gap_calls>0);
    p.conn_stat.state=ESP_A2D_CONNECTION_STATE_CONNECTED;
    bt_a2dp_event_cb(ESP_A2D_CONNECTION_STATE_EVT,&p);
    after_enabled_read=turn_off; /* OFF between callback's enabled read and state publication */
    bt_a2dp_event_cb(ESP_A2D_AUDIO_STATE_EVT,&p);
    assert(!s_bt_enabled && !s_bt_streaming && !s_bt_lock);
    p.conn_stat.state=ESP_A2D_CONNECTION_STATE_DISCONNECTED;
    bt_a2dp_event_cb(ESP_A2D_CONNECTION_STATE_EVT,&p);
    synth_service_set_bt_enabled(true);
    int prior_disconnects=disconnects;
    p.conn_stat.state=ESP_A2D_CONNECTION_STATE_CONNECTED;
    after_enabled_read=turn_off;
    bt_a2dp_event_cb(ESP_A2D_CONNECTION_STATE_EVT,&p);
    assert(!s_bt_enabled && disconnects==prior_disconnects+1 && !s_bt_lock);
    p.audio_cfg.mcc.type=ESP_A2D_MCT_SBC;
    p.audio_cfg.mcc.cie.sbc_info.samp_freq=ESP_A2D_SBC_CIE_SF_48K;
    synth_service_set_bt_enabled(true);
    s_bt_input_rate=0;
    after_enabled_read=turn_off;
    bt_a2dp_event_cb(ESP_A2D_AUDIO_CFG_EVT,&p);
    assert(!s_bt_enabled && !s_bt_streaming && !s_bt_lock);
    return 0;
}
'''
if len(sys.argv)==1 or sys.argv[1]=='voice': run('Voice failure event, stop and restart', voice_code)
if len(sys.argv)==1 or sys.argv[1]=='bt': run('Bluetooth OFF and delayed callbacks', bt_code)
