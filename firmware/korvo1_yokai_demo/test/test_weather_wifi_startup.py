"""ASan-check the real weather startup against full-width synthetic credentials."""
from pathlib import Path
import subprocess
import tempfile

project = Path(__file__).resolve().parents[1]
source = (project / "main/weather_service.c").read_text()
start = source.index("static void weather_worker_task(")
startup = source[start:source.index("    while (1) {", start)] + "}\n"
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define ESP_OK 0
#define WIFI_IF_STA 0
#define WPA3_SAE_PWE_BOTH 0
#define WIFI_AUTH_OPEN 0
#define TAG "test"
#define ESP_LOGI(tag, ...) capture_log(__VA_ARGS__)
typedef struct { struct {
    uint8_t ssid[32], password[64];
    struct { bool capable, required; } pmf_cfg;
    int sae_pwe_h2e;
    struct { int authmode; } threshold;
} sta; } wifi_config_t;
typedef enum { BOARD_WIFI_DISCONNECTED, BOARD_WIFI_CONNECTING, BOARD_WIFI_CONNECTED, BOARD_WIFI_FAILED } board_wifi_state_t;
typedef struct { board_wifi_state_t state; } board_wifi_info_t;
static board_wifi_state_t state;
static int reconnects, direct_connects, direct_configs, board_connects;
static void capture_log(const char *format, ...) {
    char text[256];
    va_list args; va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    /* Only synthetic data is used; never print credential-containing log text. */
    assert(strstr(text, "ZZZTEST") == NULL);
}
static int board_ui_wifi_ensure_started(void) { return ESP_OK; }
static void board_ui_wifi_get_info(board_wifi_info_t *out) { out->state = state; }
static void board_ui_wifi_reconnect_saved(void) { reconnects++; }
static void board_ui_wifi_connect(const char *ssid, const char *password) {
    assert(strlen(ssid) == 32 && strlen(password) == 64);
    board_connects++;
}
static int esp_wifi_get_config(int mode, wifi_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    memset(cfg->sta.ssid, 'S', sizeof(cfg->sta.ssid));
    memset(cfg->sta.password, 'P', sizeof(cfg->sta.password));
    memcpy(cfg->sta.password, "ZZZTEST", 7);
    return ESP_OK;
}
static int esp_wifi_set_config(int mode, wifi_config_t *cfg) { direct_configs++; return ESP_OK; }
static int esp_wifi_connect(void) { direct_connects++; return ESP_OK; }
'''
checks = r'''
int main(void) {
    state = BOARD_WIFI_DISCONNECTED;
    weather_worker_task(NULL);
#ifdef CONFIG_YOKAI_WIFI_SSID
    assert(board_connects == 1 && reconnects == 0);
#else
    assert(reconnects == 1 && board_connects == 0);
#endif
    assert(direct_connects == 0 && direct_configs == 0);
    state = BOARD_WIFI_CONNECTING;
    weather_worker_task(NULL);
    assert(reconnects + board_connects == 1);
    state = BOARD_WIFI_CONNECTED;
    weather_worker_task(NULL);
    assert(reconnects + board_connects == 1);
    state = BOARD_WIFI_FAILED;
    weather_worker_task(NULL);
    assert(reconnects + board_connects == 2);
    puts("Weather Wi-Fi startup checks PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "startup.c"
    c.write_text(harness + startup + checks)
    exe = Path(tmp) / "check"
    for defines in [[], ['-DCONFIG_YOKAI_WIFI_SSID="' + 'S' * 32 + '"',
                         '-DCONFIG_YOKAI_WIFI_PASSWORD="' + 'P' * 64 + '"']]:
        subprocess.run(["cc", "-std=c11", "-fsanitize=address", "-g", *defines,
                        str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
