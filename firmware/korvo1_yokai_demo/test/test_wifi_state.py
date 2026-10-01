#!/usr/bin/env python3
"""Run the board's actual Wi-Fi transitions against small ESP-IDF host stubs."""
from pathlib import Path
import subprocess
import tempfile
import sys

source_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / 'main/board_ui.c'
source = source_path.read_text()
def extract(signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]

state = source[source.index('static bool s_wifi_ready'):source.index('static volatile uint32_t s_ui_tick_enter_count')]
functions = ''.join(extract(sig) for sig in (
    'static void on_board_wifi_event(', 'static void wifi_scan_worker_task(', 'esp_err_t board_ui_wifi_scan_async(',
    'void board_ui_wifi_connect(', 'void board_ui_wifi_reconnect_saved(',
    'bool board_ui_wifi_is_saved('))
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_ARG 2
#define WIFI_IF_STA 0
#define WIFI_AUTH_OPEN 0
#define WPA3_SAE_PWE_BOTH 0
#define WIFI_SCAN_TYPE_ACTIVE 0
#define MAX_WIFI_APS 14
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define WIFI_EVENT 1
#define IP_EVENT 2
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_CONNECTED 2
#define WIFI_EVENT_STA_DISCONNECTED 3
#define IP_EVENT_STA_GOT_IP 4
#define WIFI_REASON_STA_LEAVING 8
#define IPSTR "%d"
#define IP2STR(ip) (ip)->addr
typedef int esp_event_base_t;
typedef struct { uint8_t ssid[32], ssid_len; } wifi_event_sta_connected_t;
typedef struct { uint8_t reason; } wifi_event_sta_disconnected_t;
typedef struct { struct { struct { unsigned addr; } ip; } ip_info; } ip_event_got_ip_t;
typedef int esp_err_t;
typedef enum { BOARD_WIFI_DISCONNECTED, BOARD_WIFI_CONNECTING, BOARD_WIFI_CONNECTED, BOARD_WIFI_FAILED } board_wifi_state_t;
typedef struct { uint8_t ssid[33]; int authmode; } wifi_ap_record_t;
typedef struct { struct { uint8_t ssid[32], password[64]; struct { bool capable, required; } pmf_cfg; int sae_pwe_h2e; struct { int authmode; } threshold; } sta; } wifi_config_t;
typedef struct { int scan_type; struct { struct { int min,max; } active; } scan_time; } wifi_scan_config_t;
static wifi_config_t saved;
static int start_error, config_error, disconnect_error, connect_error;
static int sets, connects, disconnects, scans, delays;
static bool busy, cancel_during_scan;
static void (*delay_hook)(void);
static esp_err_t board_ui_wifi_ensure_started(void) { return start_error; }
static int esp_wifi_get_config(int i, wifi_config_t *c) { *c=saved; return 0; }
static int esp_wifi_set_config(int i, wifi_config_t *c) { sets++; if (busy) return ESP_FAIL; if(config_error) return config_error; saved=*c; return 0; }
static int esp_wifi_disconnect(void) { disconnects++; if(disconnect_error) return disconnect_error; busy=false; return 0; }
static int esp_wifi_connect(void) { connects++; busy=true; return connect_error; }
static int esp_wifi_scan_stop(void) { return 0; }
static const char *esp_err_to_name(int e) { return "failure"; }
static void weather_service_set_offline(void) {}
static void weather_service_trigger_refresh(void) {}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) { return ESP_FAIL; }
'''
scan_stubs = r'''
static void vTaskDelay(int t) { delays++; if(delay_hook) { void (*hook)(void)=delay_hook; delay_hook=NULL; hook(); } }
static void vTaskDelete(void *p) {}
static int esp_wifi_scan_start(wifi_scan_config_t *c, bool block) { scans++; if(cancel_during_scan) board_ui_wifi_connect("new", "password"); return 0; }
static int esp_wifi_scan_get_ap_num(uint16_t *n) { *n=1; return 0; }
static int esp_wifi_scan_get_ap_records(uint16_t *n, wifi_ap_record_t *a) { return 0; }
static int xTaskCreate(void (*f)(void *), const char *n, int stack, void *arg, int priority, void *handle) { return pdPASS; }
'''
main = r'''
static void reset(void) {
    memset(&saved,0,sizeof(saved)); start_error=config_error=disconnect_error=connect_error=0;
    sets=connects=disconnects=scans=delays=0; busy=cancel_during_scan=false; delay_hook=NULL;
    s_wifi_state=BOARD_WIFI_DISCONNECTED; s_wifi_scan_running=false;
    s_wifi_scan_generation=0; s_wifi_ap_count=0; s_wifi_scan_failed=false; s_wifi_results_dirty=false;
}
static void cancel_before_scan(void) {
    board_ui_wifi_connect("new", "password");
    assert(s_wifi_scan_running); /* Keep old worker ownership until it exits. */
    uint32_t generation=s_wifi_scan_generation;
    assert(board_ui_wifi_scan_async()==ESP_OK);
    assert(s_wifi_scan_generation==generation); /* No overlapping scan workers. */
}
int main(int argc, char **argv) {
    reset();
    switch(atoi(argv[1])) {
    case 0: /* Switching an associated/connecting station disconnects before config. */
        busy=true; s_wifi_state=BOARD_WIFI_CONNECTED;
        board_ui_wifi_connect("new", "password");
        assert(disconnects==1 && sets==1 && connects==1);
        assert(!memcmp(saved.sta.ssid,"new",4));
        break;
    case 1: config_error=ESP_FAIL; board_ui_wifi_connect("new","password");
        assert(connects==0 && s_wifi_state==BOARD_WIFI_FAILED); break;
    case 2: start_error=ESP_FAIL; board_ui_wifi_connect("new","password");
        assert(sets==0 && connects==0 && s_wifi_state==BOARD_WIFI_FAILED); break;
    case 3: { /* Exact 32-byte SSID and 64-byte PSK survive intact. */
        const char *ssid="12345678901234567890123456789012";
        const char *psk="1234567890123456789012345678901234567890123456789012345678901234";
        board_ui_wifi_connect(ssid,psk);
        assert(!memcmp(saved.sta.ssid,ssid,32)); assert(!memcmp(saved.sta.password,psk,64));
        assert(board_ui_wifi_is_saved(ssid));
        board_ui_wifi_connect(ssid,NULL); assert(!memcmp(saved.sta.password,psk,64));
        break; }
    case 4: disconnect_error=ESP_FAIL; busy=true; board_ui_wifi_connect("new","password");
        assert(sets==0 && connects==0 && s_wifi_state==BOARD_WIFI_FAILED); break;
    case 5: memcpy(saved.sta.ssid,"saved",5); memcpy(saved.sta.password,"password",8);
        busy=true; board_ui_wifi_reconnect_saved(); assert(disconnects==1 && connects==1); break;
    case 6: assert(board_ui_wifi_scan_async()==ESP_OK); delay_hook=cancel_before_scan;
        wifi_scan_worker_task((void *)(uintptr_t)1); assert(!s_wifi_scan_running && scans==0); break;
    case 7: assert(board_ui_wifi_scan_async()==ESP_OK); cancel_during_scan=true;
        wifi_scan_worker_task((void *)(uintptr_t)1);
        assert(!s_wifi_scan_running && s_wifi_ap_count==0 && !s_wifi_scan_failed); break;
    case 8: board_ui_wifi_connect("123456789012345678901234567890123","password");
        assert(sets==0 && connects==0); break;
    case 9: connect_error=ESP_FAIL; on_board_wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);
        /* No saved config means no attempt. */
        assert(connects==0);
        memcpy(saved.sta.ssid,"saved",5);
        on_board_wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);
        assert(connects==1 && s_wifi_state==BOARD_WIFI_FAILED); break;
    case 10: memset(saved.sta.ssid,'s',32); memset(saved.sta.password,'p',64);
        on_board_wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);
        assert(strlen(s_connecting_ssid)==32 && !memcmp(s_connecting_ssid,saved.sta.ssid,32));
        assert(s_wifi_state==BOARD_WIFI_CONNECTING); break;
    case 11: { board_ui_wifi_connect("new","password");
        wifi_event_sta_disconnected_t event={.reason=WIFI_REASON_STA_LEAVING};
        on_board_wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&event);
        assert(s_wifi_state==BOARD_WIFI_CONNECTING && s_wifi_last_disconnect_reason==0); break; }
    case 12: connect_error=ESP_FAIL; board_ui_wifi_connect("new","password");
        assert(s_wifi_state==BOARD_WIFI_FAILED); break;
    case 13: memcpy(saved.sta.ssid,"saved",5); s_wifi_state=BOARD_WIFI_CONNECTING;
        on_board_wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);
        assert(connects==0); break;
    }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'check.c'
    c.write_text(harness + '#include <stdlib.h>\n' + state + 'void board_ui_wifi_connect(const char *, const char *);\n' + scan_stubs + functions + main)
    exe = Path(tmp) / 'check'
    subprocess.run(['cc', '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
    failed = []
    for case in range(14):
        result = subprocess.run([str(exe),str(case)], capture_output=True, text=True)
        if result.returncode:
            failed.append(case)
            print(f'Case {case} failed:\n{result.stderr[:1200]}')
    assert not failed, f'Wi-Fi regression cases failed: {failed}'
print('Wi-Fi switching, SDK errors, full-length credentials and scan cancellation checks passed')
