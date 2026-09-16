# ESP32-S31 Korvo-1 Global Bilingual Voice Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add global Japanese WakeNet activation plus offline English and Japanese-phonetic commands that open Yokai OS apps/settings and control master volume, with continuous command recognition inside Voice Shrine.

**Architecture:** Keep the verified 44.1 kHz stereo codec, synthesizer, and A2DP path unchanged. Two ESP32-S31 hardware ASRC streams convert stereo microphone input and final speaker-reference PCM to 16 kHz, then feed ESP-SR AFE as `MMNR`; WakeNet and MultiNet publish small results to a queue consumed by the existing 16 ms LVGL tick. Reuse `ui_switch_screen()` and the existing master-volume API rather than adding a second router.

**Tech Stack:** ESP-IDF master/6.2, ESP32-S31, ESP-SR 2.5.3 AFE/WakeNet9l/MultiNet7 English, `espressif/esp_asrc` 1.1.0, ES8389/ESP Codec Dev, FreeRTOS, LVGL 9, host C assertion tests.

---

## Working directory and source map

Run all commands from:

```text
/Users/kongweilu/Development/Full Demo/.worktrees/lvgl-groovebox
```

Create:

- `firmware/korvo1_yokai_demo/main/voice_service.h` — dependency-light public command/result contract, mode control, command metadata, volume semantics, and playback-reference input.
- `firmware/korvo1_yokai_demo/main/voice_service.c` — microphone, ASRC, AFE, WakeNet, MultiNet, command table, tasks, and fixed queues/buffers.
- `firmware/korvo1_yokai_demo/test/test_voice_service.c` — host assertions for command normalization, target routing, mode, and volume semantics.
- `firmware/korvo1_yokai_demo/test/check_voice_integration.py` — static integration check ensuring every command is dispatched and UI updates stay on the LVGL path.

Modify:

- `firmware/korvo1_yokai_demo/main/idf_component.yml` — pin `espressif/esp_asrc` 1.1.0.
- `firmware/korvo1_yokai_demo/dependencies.lock` — component-manager resolved lock update.
- `firmware/korvo1_yokai_demo/sdkconfig.defaults` — select Japanese WakeNet9l and English MultiNet7.
- `firmware/korvo1_yokai_demo/main/CMakeLists.txt` — compile `voice_service.c` and link ASRC.
- `firmware/korvo1_yokai_demo/main/app_main.c` — start voice service without blocking other apps on failure.
- `firmware/korvo1_yokai_demo/main/synth_service.h` — expose generated feedback-tone request.
- `firmware/korvo1_yokai_demo/main/synth_service.c` — copy final PCM to the AEC reference buffer and generate a short wake tone.
- `firmware/korvo1_yokai_demo/main/ui/ui.c` — consume voice results, route commands, synchronize volume, and switch recognition mode.
- `firmware/korvo1_yokai_demo/main/ui/ui_apps.h` — expose Voice Shrine status update.
- `firmware/korvo1_yokai_demo/main/ui/ui_apps.c` — replace the simulated button with continuous-listening status and a scrollable command catalog.
- `firmware/korvo1_yokai_demo/main/ui/ui_drawer.h` — expose programmatic volume synchronization.
- `firmware/korvo1_yokai_demo/main/ui/ui_drawer.c` — update the volume slider without duplicating volume callbacks.
- `docs/AGENT-HANDOFF.md` — record final build, flash, calibration, and verified hardware state after validation.

Do not modify files under `managed_components/`.

### Task 1: Lock ASRC and speech-model configuration

**Files:**

- Modify: `firmware/korvo1_yokai_demo/main/idf_component.yml`
- Modify: `firmware/korvo1_yokai_demo/sdkconfig.defaults`
- Modify: `firmware/korvo1_yokai_demo/main/CMakeLists.txt`
- Modify after reconfigure: `firmware/korvo1_yokai_demo/dependencies.lock`

- [ ] **Step 1: Add the official ASRC dependency**

Add beside the existing audio components:

```yaml
  espressif/esp_asrc:
    version: ==1.1.0
```

- [ ] **Step 2: Select only the required speech models**

Append to `sdkconfig.defaults`:

```text
# ESP-SR global bilingual command control
CONFIG_MODEL_IN_FLASH=y
CONFIG_SR_WN_WN9L_JA_KONNICHIHAESP_TTS3=y
CONFIG_SR_MN_EN_MULTINET7_QUANT=y
```

Keep the default WebRTC NS/VAD models; do not add a second language model or neural NS model in this task.

- [ ] **Step 3: Register the new source and dependency**

Add `voice_service.c` to `SRCS`, then add `espressif__esp_asrc` to `PRIV_REQUIRES`:

```cmake
idf_component_register(
    SRCS "app_main.c" "app_state.c" "board_ui.c" "synth_service.c" "voice_service.c" "weather_service.c"
         # existing UI and asset sources stay unchanged
    INCLUDE_DIRS "." "ui"
    PRIV_REQUIRES esp_wifi esp_netif nvs_flash
        espressif__esp-sr espressif__esp_asrc espressif__esp-dl espressif__esp-dsp
        espressif__esp32_s31_korvo_1_noglib espressif__esp_audio_effects
        espressif__esp_codec_dev bt esp_ringbuf esp_http_client
        espressif__cjson mbedtls lvgl espressif__esp_lvgl_adapter
        espressif__esp_lv_lottie_player espressif__thorvg espressif__esp_new_jpeg)
```

Preserve the complete existing source list; only insert the new source and dependency.

- [ ] **Step 4: Add a temporary compilable service shell**

Create `voice_service.h` with only the start function initially:

```c
#pragma once

#include <stdbool.h>

bool voice_service_start(void);
```

Create `voice_service.c`:

```c
#include "voice_service.h"

bool voice_service_start(void)
{
    return false;
}
```

- [ ] **Step 5: Regenerate configuration and dependencies**

The project-level `sdkconfig` is generated and ignored. Preserve it before forcing the newly tracked defaults to apply:

```bash
mv firmware/korvo1_yokai_demo/sdkconfig /tmp/korvo1-sdkconfig-before-voice
source /Users/kongweilu/esp/esp-idf-master/export.sh
idf.py -C firmware/korvo1_yokai_demo -B build-korvo1-s31-synth reconfigure
```

Expected configuration contains:

```text
CONFIG_SR_WN_WN9L_JA_KONNICHIHAESP_TTS3=y
CONFIG_SR_MN_EN_MULTINET7_QUANT=y
```

Verify:

```bash
rg -n "CONFIG_SR_WN_WN9L_JA_KONNICHIHAESP_TTS3=y|CONFIG_SR_MN_EN_MULTINET7_QUANT=y" firmware/korvo1_yokai_demo/sdkconfig
rg -n "esp_asrc" firmware/korvo1_yokai_demo/dependencies.lock
```

Expected: two model-config matches and a locked `espressif/esp_asrc` 1.1.0 entry.

- [ ] **Step 6: Build the dependency/model baseline**

```bash
source /Users/kongweilu/esp/esp-idf-master/export.sh
ninja -C firmware/korvo1_yokai_demo/build-korvo1-s31-synth
wc -c firmware/korvo1_yokai_demo/build-korvo1-s31-synth/srmodels/srmodels.bin
```

Expected: build succeeds; `srmodels.bin` exists and is smaller than 6,291,456 bytes.

- [ ] **Step 7: Commit the configuration baseline**

```bash
git add firmware/korvo1_yokai_demo/main/idf_component.yml \
        firmware/korvo1_yokai_demo/sdkconfig.defaults \
        firmware/korvo1_yokai_demo/main/CMakeLists.txt \
        firmware/korvo1_yokai_demo/main/voice_service.h \
        firmware/korvo1_yokai_demo/main/voice_service.c \
        firmware/korvo1_yokai_demo/dependencies.lock
git commit -m "build: enable S31 bilingual speech models"
```

### Task 2: Define and test the command contract

**Files:**

- Modify: `firmware/korvo1_yokai_demo/main/voice_service.h`
- Modify: `firmware/korvo1_yokai_demo/main/voice_service.c`
- Create: `firmware/korvo1_yokai_demo/test/test_voice_service.c`

- [ ] **Step 1: Write the failing host test**

Create `test/test_voice_service.c`:

```c
#include <assert.h>
#include <stdio.h>

#include "voice_service.h"

static void test_command_table(void)
{
    assert(voice_service_command_count() == 15);
    const voice_command_info_t *music = voice_service_command_info(VOICE_COMMAND_SYNTH);
    assert(music != NULL);
    assert(music->target == VOICE_TARGET_SYNTH);
    assert(music->english != NULL);
    assert(music->japanese != NULL);
}

static void test_multinet_id_normalization(void)
{
    voice_language_t language = VOICE_LANGUAGE_ENGLISH;
    assert(voice_service_normalize_multinet_id(VOICE_COMMAND_WEATHER, &language) == VOICE_COMMAND_WEATHER);
    assert(language == VOICE_LANGUAGE_ENGLISH);
    assert(voice_service_normalize_multinet_id(100 + VOICE_COMMAND_WEATHER, &language) == VOICE_COMMAND_WEATHER);
    assert(language == VOICE_LANGUAGE_JAPANESE);
    assert(voice_service_normalize_multinet_id(999, &language) == VOICE_COMMAND_NONE);
}

static void test_volume_semantics(void)
{
    int saved = 80;
    assert(voice_service_apply_volume(VOICE_COMMAND_VOLUME_UP, 95, &saved) == 100);
    assert(voice_service_apply_volume(VOICE_COMMAND_VOLUME_DOWN, 5, &saved) == 0);
    assert(voice_service_apply_volume(VOICE_COMMAND_MUTE, 60, &saved) == 0);
    assert(saved == 60);
    assert(voice_service_apply_volume(VOICE_COMMAND_MUTE, 0, &saved) == 0);
    assert(saved == 60);
    assert(voice_service_apply_volume(VOICE_COMMAND_UNMUTE, 0, &saved) == 60);
}

int main(void)
{
    test_command_table();
    test_multinet_id_normalization();
    test_volume_semantics();
    puts("Voice command checks passed.");
    return 0;
}
```

- [ ] **Step 2: Run the test and verify it fails**

```bash
cc -std=c11 -Wall -Wextra -Werror \
  -DVOICE_SERVICE_LOGIC_ONLY \
  -I firmware/korvo1_yokai_demo/main \
  firmware/korvo1_yokai_demo/test/test_voice_service.c \
  firmware/korvo1_yokai_demo/main/voice_service.c \
  -o /tmp/korvo1_voice_test && /tmp/korvo1_voice_test
```

Expected: compilation fails because the command types and helpers are not defined yet.

- [ ] **Step 3: Replace the public header with the complete dependency-light contract**

Use this shape in `voice_service.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    VOICE_COMMAND_NONE = 0,
    VOICE_COMMAND_SYNTH = 1,
    VOICE_COMMAND_WEATHER,
    VOICE_COMMAND_VOICE,
    VOICE_COMMAND_VISION,
    VOICE_COMMAND_FIREWORKS,
    VOICE_COMMAND_CLOCK,
    VOICE_COMMAND_CALCULATOR,
    VOICE_COMMAND_FOOD,
    VOICE_COMMAND_WIFI,
    VOICE_COMMAND_BLUETOOTH,
    VOICE_COMMAND_HOME,
    VOICE_COMMAND_VOLUME_UP,
    VOICE_COMMAND_VOLUME_DOWN,
    VOICE_COMMAND_MUTE,
    VOICE_COMMAND_UNMUTE,
    VOICE_COMMAND_COUNT,
} voice_command_t;

typedef enum {
    VOICE_TARGET_NONE = 0,
    VOICE_TARGET_SYNTH,
    VOICE_TARGET_WEATHER,
    VOICE_TARGET_VOICE,
    VOICE_TARGET_VISION,
    VOICE_TARGET_FIREWORKS,
    VOICE_TARGET_CLOCK,
    VOICE_TARGET_CALCULATOR,
    VOICE_TARGET_FOOD,
    VOICE_TARGET_WIFI,
    VOICE_TARGET_BLUETOOTH,
    VOICE_TARGET_HOME,
    VOICE_TARGET_VOLUME,
} voice_target_t;

typedef enum {
    VOICE_LANGUAGE_ENGLISH = 0,
    VOICE_LANGUAGE_JAPANESE,
} voice_language_t;

typedef enum {
    VOICE_MODE_GLOBAL_WAKE = 0,
    VOICE_MODE_CONTINUOUS,
} voice_mode_t;

typedef enum {
    VOICE_EVENT_IDLE = 0,
    VOICE_EVENT_WAKE,
    VOICE_EVENT_LISTENING,
    VOICE_EVENT_COMMAND,
    VOICE_EVENT_RETRY,
    VOICE_EVENT_ERROR,
} voice_event_t;

typedef struct {
    voice_command_t command;
    voice_target_t target;
    const char *feature;
    const char *english;
    const char *japanese;
    const char *phonetic_alias;
    const char *phonemes;
} voice_command_info_t;

typedef struct {
    voice_event_t event;
    voice_command_t command;
    voice_language_t language;
    float confidence;
} voice_result_t;

size_t voice_service_command_count(void);
const voice_command_info_t *voice_service_command_info(voice_command_t command);
voice_command_t voice_service_normalize_multinet_id(int id, voice_language_t *language);
int voice_service_apply_volume(voice_command_t command, int current, int *saved_nonzero);

bool voice_service_start(void);
bool voice_service_is_ready(void);
const char *voice_service_error(void);
void voice_service_set_mode(voice_mode_t mode);
voice_mode_t voice_service_get_mode(void);
bool voice_service_receive(voice_result_t *result);
void voice_service_feed_playback(const int16_t *stereo, size_t frames);
```

- [ ] **Step 4: Add the command table and pure helpers before the hardware guard**

At the top of `voice_service.c`, include only standard headers and define the table. Use English-friendly syllables and explicit initial MultiNet phonemes:

```c
#include "voice_service.h"

#include <stddef.h>

#define VOICE_MN_JAPANESE_BASE 100

static const voice_command_info_t s_commands[] = {
    {VOICE_COMMAND_SYNTH,      VOICE_TARGET_SYNTH,      "雷神合成器", "OPEN THE SYNTHESIZER",    "音楽",           "OWN GAH COO",       "bN Gn Ko"},
    {VOICE_COMMAND_WEATHER,    VOICE_TARGET_WEATHER,    "雪女天气",   "SHOW THE WEATHER",         "天気",           "TEN KEY",           "TfN Km"},
    {VOICE_COMMAND_VOICE,      VOICE_TARGET_VOICE,      "言灵神社",   "OPEN VOICE CONTROL",       "言霊",           "COAT OH DAH MAH",   "KbT b Dn Mn"},
    {VOICE_COMMAND_VISION,     VOICE_TARGET_VISION,     "目目连视觉", "OPEN THE CAMERA",          "カメラ",         "CAMERA",            "KaMRc"},
    {VOICE_COMMAND_FIREWORKS,  VOICE_TARGET_FIREWORKS,  "烟火应用",   "SHOW THE FIREWORKS",       "花火",           "HAH NAH BEE",       "hn Nn Bm"},
    {VOICE_COMMAND_CLOCK,      VOICE_TARGET_CLOCK,      "时钟",       "OPEN THE CLOCK",           "時計",           "TOE KAY",           "Tb Kd"},
    {VOICE_COMMAND_CALCULATOR, VOICE_TARGET_CALCULATOR, "阴阳计算器", "OPEN THE CALCULATOR",      "計算",           "KAY SAHN",          "Kd SnN"},
    {VOICE_COMMAND_FOOD,       VOICE_TARGET_FOOD,       "食物应用",   "OPEN THE FOOD APP",        "料理",           "REE OH REE",        "Rm b Rm"},
    {VOICE_COMMAND_WIFI,       VOICE_TARGET_WIFI,       "Wi-Fi 设置", "OPEN WI FI SETTINGS",      "ワイファイ",     "WHY FLY",           "Wi FLi"},
    {VOICE_COMMAND_BLUETOOTH,  VOICE_TARGET_BLUETOOTH,  "蓝牙设置",   "OPEN BLUETOOTH SETTINGS",  "ブルートゥース", "BLUE TOOTH",         "BLo Tov"},
    {VOICE_COMMAND_HOME,       VOICE_TARGET_HOME,       "返回桌面",   "GO BACK HOME",             "ホーム",         "HOME",               "hbM"},
    {VOICE_COMMAND_VOLUME_UP,  VOICE_TARGET_VOLUME,     "音量增加",   "TURN THE VOLUME UP",        "上げて",         "AH GET EH",          "c GfT f"},
    {VOICE_COMMAND_VOLUME_DOWN,VOICE_TARGET_VOLUME,     "音量降低",   "TURN THE VOLUME DOWN",      "下げて",         "SAH GET EH",         "Sn GfT f"},
    {VOICE_COMMAND_MUTE,       VOICE_TARGET_VOLUME,     "静音",       "MUTE THE SOUND",            "ミュート",       "MEW TOE",            "MYo Tb"},
    {VOICE_COMMAND_UNMUTE,     VOICE_TARGET_VOLUME,     "恢复音量",   "RESTORE THE VOLUME",        "戻して",         "MOE DOUGH SHE TEH",  "Mb Db sm Tf"},
};

size_t voice_service_command_count(void)
{
    return sizeof(s_commands) / sizeof(s_commands[0]);
}

const voice_command_info_t *voice_service_command_info(voice_command_t command)
{
    if (command <= VOICE_COMMAND_NONE || command >= VOICE_COMMAND_COUNT) {
        return NULL;
    }
    return &s_commands[(size_t)command - 1];
}

voice_command_t voice_service_normalize_multinet_id(int id, voice_language_t *language)
{
    voice_language_t detected = VOICE_LANGUAGE_ENGLISH;
    if (id >= VOICE_MN_JAPANESE_BASE) {
        detected = VOICE_LANGUAGE_JAPANESE;
        id -= VOICE_MN_JAPANESE_BASE;
    }
    if (language != NULL) {
        *language = detected;
    }
    return id > VOICE_COMMAND_NONE && id < VOICE_COMMAND_COUNT
        ? (voice_command_t)id : VOICE_COMMAND_NONE;
}

static int clamp_volume(int value)
{
    return value < 0 ? 0 : (value > 100 ? 100 : value);
}

int voice_service_apply_volume(voice_command_t command, int current, int *saved_nonzero)
{
    current = clamp_volume(current);
    if (saved_nonzero == NULL) {
        return current;
    }
    switch (command) {
    case VOICE_COMMAND_VOLUME_UP:
        return clamp_volume(current + 10);
    case VOICE_COMMAND_VOLUME_DOWN:
        return clamp_volume(current - 10);
    case VOICE_COMMAND_MUTE:
        if (current > 0) {
            *saved_nonzero = current;
        }
        return 0;
    case VOICE_COMMAND_UNMUTE:
        return clamp_volume(*saved_nonzero > 0 ? *saved_nonzero : 80);
    default:
        return current;
    }
}

#ifndef VOICE_SERVICE_LOGIC_ONLY
/* ESP-IDF hardware implementation follows in later tasks. */
#endif
```

- [ ] **Step 5: Run the host test and verify it passes**

Run the command from Step 2 again.

Expected:

```text
Voice command checks passed.
```

- [ ] **Step 6: Commit the command contract**

```bash
git add firmware/korvo1_yokai_demo/main/voice_service.h \
        firmware/korvo1_yokai_demo/main/voice_service.c \
        firmware/korvo1_yokai_demo/test/test_voice_service.c
git commit -m "feat: define bilingual voice commands"
```

### Task 3: Build the 44.1 kHz microphone and hardware-ASRC path

**Files:**

- Modify: `firmware/korvo1_yokai_demo/main/voice_service.c`

- [ ] **Step 1: Add ESP-IDF audio and ASRC state inside the hardware guard**

Add these includes and constants after `#ifndef VOICE_SERVICE_LOGIC_ONLY`:

```c
#include "bsp/esp32_s31_korvo_1.h"
#include "esp_asrc.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define VOICE_CODEC_RATE       44100
#define VOICE_SR_RATE          16000
#define VOICE_IO_FRAMES        441
#define VOICE_SR_FRAMES        160
#define VOICE_AFE_CHANNELS     4
#define VOICE_REF_RING_FRAMES  (VOICE_CODEC_RATE / 4)
#define VOICE_AEC_DELAY_FRAMES (VOICE_CODEC_RATE * 40 / 1000)

static const char *TAG = "voice_service";
static float s_ref_mix[2] = {0.5f, 0.5f};
static esp_codec_dev_handle_t s_mic_dev;
static esp_asrc_handle_t s_mic_asrc;
static esp_asrc_handle_t s_ref_asrc;
static int16_t *s_ref_ring;
static size_t s_ref_read;
static size_t s_ref_write;
static size_t s_ref_count;
static SemaphoreHandle_t s_ref_mutex;
static QueueHandle_t s_result_queue;
static volatile voice_mode_t s_mode = VOICE_MODE_GLOBAL_WAKE;
static bool s_ready;
static char s_error[96] = "Voice service has not started";
```

- [ ] **Step 2: Open both S31 hardware ASRC streams**

Add:

```c
static bool open_asrc(void)
{
    esp_asrc_cfg_t mic_cfg = {
        .src_info = {.sample_rate = VOICE_CODEC_RATE, .channel = 2, .bits_per_sample = 16},
        .dest_info = {.sample_rate = VOICE_SR_RATE, .channel = 2, .bits_per_sample = 16},
        .perf_type = ESP_ASRC_PERF_TYPE_HW_ONLY,
        .complexity = 1,
        .timeout_ms = 100,
    };
    esp_asrc_cfg_t ref_cfg = {
        .src_info = {.sample_rate = VOICE_CODEC_RATE, .channel = 2, .bits_per_sample = 16},
        .dest_info = {.sample_rate = VOICE_SR_RATE, .channel = 1, .bits_per_sample = 16},
        .weight = s_ref_mix,
        .weight_len = 2,
        .perf_type = ESP_ASRC_PERF_TYPE_HW_ONLY,
        .complexity = 1,
        .timeout_ms = 100,
    };
    return esp_asrc_open(&mic_cfg, &s_mic_asrc) == ESP_ASRC_ERR_OK &&
           esp_asrc_open(&ref_cfg, &s_ref_asrc) == ESP_ASRC_ERR_OK;
}
```

The channel-mix weights have static lifetime because the component configuration accepts a pointer.

- [ ] **Step 3: Open the ES8389 input at the existing shared I2S format**

Add:

```c
static bool open_microphone(void)
{
    s_mic_dev = bsp_audio_codec_microphone_init();
    if (s_mic_dev == NULL) {
        return false;
    }
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = VOICE_CODEC_RATE,
        .channel = 2,
        .bits_per_sample = 16,
    };
    return esp_codec_dev_open(s_mic_dev, &fs) == ESP_CODEC_DEV_OK;
}
```

Do not reinitialize or retime I2S; `synth_service_init()` has already created the full-duplex 44.1 kHz channels.

- [ ] **Step 4: Add the non-blocking playback-reference ring**

Implement `voice_service_feed_playback()` and a private reader:

```c
void voice_service_feed_playback(const int16_t *stereo, size_t frames)
{
    if (stereo == NULL || frames == 0 || s_ref_ring == NULL || s_ref_mutex == NULL ||
        xSemaphoreTake(s_ref_mutex, 0) != pdTRUE) {
        return;
    }
    for (size_t i = 0; i < frames; ++i) {
        if (s_ref_count == VOICE_REF_RING_FRAMES) {
            s_ref_read = (s_ref_read + 1) % VOICE_REF_RING_FRAMES;
            --s_ref_count;
        }
        s_ref_ring[s_ref_write * 2] = stereo[i * 2];
        s_ref_ring[s_ref_write * 2 + 1] = stereo[i * 2 + 1];
        s_ref_write = (s_ref_write + 1) % VOICE_REF_RING_FRAMES;
        ++s_ref_count;
    }
    xSemaphoreGive(s_ref_mutex);
}

static void read_reference(int16_t *out, size_t frames)
{
    memset(out, 0, frames * 2 * sizeof(*out));
    if (xSemaphoreTake(s_ref_mutex, pdMS_TO_TICKS(2)) != pdTRUE) {
        return;
    }
    if (s_ref_count >= VOICE_AEC_DELAY_FRAMES + frames) {
        for (size_t i = 0; i < frames; ++i) {
            out[i * 2] = s_ref_ring[s_ref_read * 2];
            out[i * 2 + 1] = s_ref_ring[s_ref_read * 2 + 1];
            s_ref_read = (s_ref_read + 1) % VOICE_REF_RING_FRAMES;
            --s_ref_count;
        }
    }
    xSemaphoreGive(s_ref_mutex);
}
```

Add `#include <string.h>` above the hardware guard.

- [ ] **Step 5: Allocate aligned ASRC buffers and exercise one frame in the feed task**

Use `esp_asrc_get_buffer_alignment()` and `esp_asrc_align_alloc()` for:

```c
mic_44k:  VOICE_IO_FRAMES * 2 * sizeof(int16_t)
ref_44k:  VOICE_IO_FRAMES * 2 * sizeof(int16_t)
mic_16k:  VOICE_SR_FRAMES * 2 * sizeof(int16_t)
ref_16k:  VOICE_SR_FRAMES * sizeof(int16_t)
```

The conversion loop must use actual output sample counts:

```c
uint32_t mic_out_frames = VOICE_SR_FRAMES;
uint32_t ref_out_frames = VOICE_SR_FRAMES;
if (esp_codec_dev_read(s_mic_dev, mic_44k,
                       VOICE_IO_FRAMES * 2 * sizeof(int16_t)) == ESP_CODEC_DEV_OK) {
    read_reference(ref_44k, VOICE_IO_FRAMES);
    esp_asrc_process(s_mic_asrc, (uint8_t *)mic_44k, VOICE_IO_FRAMES,
                     (uint8_t *)mic_16k, &mic_out_frames);
    esp_asrc_process(s_ref_asrc, (uint8_t *)ref_44k, VOICE_IO_FRAMES,
                     (uint8_t *)ref_16k, &ref_out_frames);
}
```

Treat unequal `mic_out_frames` and `ref_out_frames` as a dropped feed frame and increment a diagnostic counter; never pass mismatched channel lengths to AFE.

- [ ] **Step 6: Build and inspect ASRC linkage**

```bash
source /Users/kongweilu/esp/esp-idf-master/export.sh
ninja -C firmware/korvo1_yokai_demo/build-korvo1-s31-synth
riscv32-esp-elf-nm -C firmware/korvo1_yokai_demo/build-korvo1-s31-synth/korvo1_yokai_demo.elf | rg "esp_asrc_(open|process)"
```

Expected: build succeeds and both ASRC symbols are resolved in the ELF.

- [ ] **Step 7: Commit the capture/conversion path**

```bash
git add firmware/korvo1_yokai_demo/main/voice_service.c
git commit -m "feat: add S31 speech audio conversion path"
```

### Task 4: Add AFE, Japanese WakeNet, and bilingual MultiNet state machine

**Files:**

- Modify: `firmware/korvo1_yokai_demo/main/voice_service.c`
- Modify: `firmware/korvo1_yokai_demo/main/app_main.c`

- [ ] **Step 1: Initialize models and AFE**

Include:

```c
#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"
```

Add persistent handles and create AFE with the exact input format:

```c
static srmodel_list_t *s_models;
static const esp_afe_sr_iface_t *s_afe;
static esp_afe_sr_data_t *s_afe_data;
static esp_mn_iface_t *s_mn;
static model_iface_data_t *s_mn_data;

static bool open_speech_models(void)
{
    s_models = esp_srmodel_init("model");
    if (s_models == NULL) {
        return false;
    }
    char *wake_name = esp_srmodel_filter(s_models, ESP_WN_PREFIX,
                                         "wn9l_ja_konnichihaesp_tts3");
    char *mn_name = esp_srmodel_filter(s_models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (wake_name == NULL || mn_name == NULL) {
        return false;
    }

    afe_config_t *cfg = afe_config_init("MMNR", s_models, AFE_TYPE_SR,
                                        AFE_MODE_HIGH_PERF);
    if (cfg == NULL) {
        return false;
    }
    cfg->wakenet_init = true;
    cfg->wakenet_model_name = wake_name;
    cfg->aec_init = true;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    cfg->afe_perferred_core = 0;
    cfg->afe_perferred_priority = 8;
    s_afe = esp_afe_handle_from_config(cfg);
    s_afe_data = s_afe != NULL ? s_afe->create_from_config(cfg) : NULL;
    afe_config_free(cfg);
    if (s_afe_data == NULL) {
        return false;
    }

    s_mn = esp_mn_handle_from_name(mn_name);
    s_mn_data = s_mn != NULL ? s_mn->create(mn_name, 5000) : NULL;
    return s_mn_data != NULL;
}
```

- [ ] **Step 2: Register the 30 phrases against 15 actions**

Add English as ordinary G2P phrases and Japanese aliases through explicit phonemes:

```c
static bool register_commands(void)
{
    if (esp_mn_commands_alloc(s_mn, s_mn_data) != ESP_OK) {
        return false;
    }
    for (size_t i = 0; i < voice_service_command_count(); ++i) {
        const voice_command_info_t *info = &s_commands[i];
        if (esp_mn_commands_add(info->command, info->english) != ESP_OK ||
            esp_mn_commands_phoneme_add(VOICE_MN_JAPANESE_BASE + info->command,
                                        info->phonetic_alias, info->phonemes) != ESP_OK) {
            return false;
        }
    }
    esp_mn_error_t *errors = esp_mn_commands_update();
    return errors == NULL || errors->num == 0;
}
```

Print active commands once at boot. A non-zero error count makes voice initialization fail with an explicit command-registration message.

- [ ] **Step 3: Assemble `MMNR` and feed complete AFE chunks**

Query `s_afe->get_feed_chunksize(s_afe_data)` at runtime. Accumulate ASRC frames and interleave them as:

```c
afe_frame[i * 4]     = mic_16k[i * 2];
afe_frame[i * 4 + 1] = mic_16k[i * 2 + 1];
afe_frame[i * 4 + 2] = 0;
afe_frame[i * 4 + 3] = ref_16k[i];
```

Call `s_afe->feed(s_afe_data, afe_frame)` only when exactly one queried AFE feed chunk is available. Keep at most one partial chunk between ASRC calls.

- [ ] **Step 4: Implement the recognition state machine**

Use these state rules in the fetch task:

```c
bool command_window = false;
unsigned retry_count = 0;
voice_mode_t active_mode = VOICE_MODE_GLOBAL_WAKE;

for (;;) {
    afe_fetch_result_t *fetch = s_afe->fetch_with_delay(s_afe_data,
                                                        pdMS_TO_TICKS(200));
    if (fetch == NULL || fetch->ret_value != ESP_OK) {
        continue;
    }

    voice_mode_t mode = s_mode;
    if (mode != active_mode) {
        active_mode = mode;
        command_window = mode == VOICE_MODE_CONTINUOUS;
        retry_count = 0;
        s_mn->clean(s_mn_data);
        if (mode == VOICE_MODE_CONTINUOUS) {
            s_afe->disable_wakenet(s_afe_data);
            publish_event(VOICE_EVENT_LISTENING, VOICE_COMMAND_NONE,
                          VOICE_LANGUAGE_ENGLISH, 0.0f);
        } else {
            s_afe->enable_wakenet(s_afe_data);
        }
    }
    if (mode == VOICE_MODE_CONTINUOUS) {
        command_window = true;
    } else if (fetch->wakeup_state == WAKENET_DETECTED) {
        command_window = true;
        retry_count = 0;
        publish_event(VOICE_EVENT_WAKE, VOICE_COMMAND_NONE,
                      VOICE_LANGUAGE_JAPANESE, 1.0f);
        s_mn->clean(s_mn_data);
    }

    if (!command_window || fetch->data == NULL) {
        continue;
    }

    esp_mn_state_t state = s_mn->detect(s_mn_data, fetch->data);
    if (state == ESP_MN_STATE_DETECTED) {
        esp_mn_results_t *results = s_mn->get_results(s_mn_data);
        if (results != NULL && results->num > 0) {
            voice_language_t language;
            voice_command_t command = voice_service_normalize_multinet_id(
                results->command_id[0], &language);
            publish_event(VOICE_EVENT_COMMAND, command, language,
                          results->prob[0]);
        }
        s_mn->clean(s_mn_data);
        command_window = mode == VOICE_MODE_CONTINUOUS;
    } else if (state == ESP_MN_STATE_TIMEOUT) {
        s_mn->clean(s_mn_data);
        if (mode == VOICE_MODE_CONTINUOUS) {
            command_window = true;
        } else if (retry_count++ == 0) {
            publish_event(VOICE_EVENT_RETRY, VOICE_COMMAND_NONE,
                          VOICE_LANGUAGE_JAPANESE, 0.0f);
        } else {
            publish_event(VOICE_EVENT_IDLE, VOICE_COMMAND_NONE,
                          VOICE_LANGUAGE_ENGLISH, 0.0f);
            command_window = mode == VOICE_MODE_CONTINUOUS;
            retry_count = 0;
        }
    }
}
```

Create a result queue of length eight. `publish_event()` uses `xQueueSend(..., 0)`; a full queue drops status events. For `VOICE_EVENT_COMMAND`, remove one oldest event with `xQueueReceive(..., 0)` and retry once so a stale status cannot suppress an action.

- [ ] **Step 5: Implement mode and result APIs**

```c
void voice_service_set_mode(voice_mode_t mode)
{
    s_mode = mode;
}

voice_mode_t voice_service_get_mode(void)
{
    return s_mode;
}

bool voice_service_receive(voice_result_t *result)
{
    return result != NULL && s_result_queue != NULL &&
           xQueueReceive(s_result_queue, result, 0) == pdTRUE;
}
```

- [ ] **Step 6: Complete robust service startup**

`voice_service_start()` must allocate the queue, reference ring and mutex; open the microphone, ASRC and speech models; register commands; then create feed and fetch tasks pinned to core 0 at priorities 9 and 7. Use this fixed sequence:

```c
s_result_queue = xQueueCreate(8, sizeof(voice_result_t));
s_ref_mutex = xSemaphoreCreateMutex();
s_ref_ring = heap_caps_calloc(VOICE_REF_RING_FRAMES * 2,
                              sizeof(int16_t), MALLOC_CAP_SPIRAM);
if (s_result_queue == NULL || s_ref_mutex == NULL || s_ref_ring == NULL) {
    return fail_start("Voice buffers unavailable");
}
if (!open_microphone()) return fail_start("Microphone open failed");
if (!open_asrc()) return fail_start("S31 ASRC open failed");
if (!open_speech_models()) return fail_start("Speech models unavailable");
if (!register_commands()) return fail_start("Command registration failed");
if (xTaskCreatePinnedToCore(feed_task, "voice_feed", 6144, NULL, 9,
                            NULL, 0) != pdPASS) {
    return fail_start("Voice feed task failed");
}
if (xTaskCreatePinnedToCore(fetch_task, "voice_fetch", 6144, NULL, 7,
                            NULL, 0) != pdPASS) {
    return fail_start("Voice fetch task failed");
}
s_ready = true;
s_error[0] = '\0';
return true;
```

`fail_start()` copies the stable text into `s_error`, sets `s_ready = false`, and releases only objects created before tasks start. A task-creation failure deletes the already-created sibling task before releasing its buffers.

```c
bool voice_service_is_ready(void) { return s_ready; }
const char *voice_service_error(void) { return s_error; }
```

Use `heap_caps_calloc(VOICE_REF_RING_FRAMES * 2, sizeof(int16_t), MALLOC_CAP_SPIRAM)` for the 44.1 kHz reference ring.

- [ ] **Step 7: Start voice after the full-duplex audio owner**

In `app_main.c`, add the include and non-fatal startup:

```c
#include "voice_service.h"

/* after synth_service_init(), before board_ui_start() */
if (!voice_service_start()) {
    ESP_LOGE(TAG, "Voice control unavailable: %s", voice_service_error());
}
```

- [ ] **Step 8: Run host test and firmware build**

```bash
cc -std=c11 -Wall -Wextra -Werror \
  -DVOICE_SERVICE_LOGIC_ONLY \
  -I firmware/korvo1_yokai_demo/main \
  firmware/korvo1_yokai_demo/test/test_voice_service.c \
  firmware/korvo1_yokai_demo/main/voice_service.c \
  -o /tmp/korvo1_voice_test && /tmp/korvo1_voice_test
source /Users/kongweilu/esp/esp-idf-master/export.sh
ninja -C firmware/korvo1_yokai_demo/build-korvo1-s31-synth
```

Expected: host checks pass and firmware links with no new warnings.

- [ ] **Step 9: Commit the speech engine**

```bash
git add firmware/korvo1_yokai_demo/main/voice_service.c \
        firmware/korvo1_yokai_demo/main/app_main.c
git commit -m "feat: add global WakeNet and bilingual MultiNet"
```

### Task 5: Feed the real playback reference and generate wake feedback

**Files:**

- Modify: `firmware/korvo1_yokai_demo/main/synth_service.h`
- Modify: `firmware/korvo1_yokai_demo/main/synth_service.c`

- [ ] **Step 1: Expose the feedback request**

Add to `synth_service.h`:

```c
void synth_service_play_feedback_tone(void);
```

- [ ] **Step 2: Write the final played PCM into the AEC reference path**

Include `voice_service.h` in `synth_service.c`. Immediately before every `esp_codec_dev_write()` call, including silence and feedback-only writes, call:

```c
voice_service_feed_playback(s_chunk_buf, SYNTH_CHUNK_SAMPLES);
```

This placement is after mixer, EQ, reverb and limiter, so AEC sees the same signal sent to the Codec.

- [ ] **Step 3: Add a generated 60 ms wake tone**

Use a single volatile request flag and audio-task-owned phase/sample count:

```c
static volatile bool s_feedback_tone_pending;

void synth_service_play_feedback_tone(void)
{
    s_feedback_tone_pending = true;
}
```

Before the existing inactive-task delay, consume the flag and render an 880 Hz sine at low amplitude for `SYNTH_SAMPLE_RATE * 60 / 1000` samples. Mix it into both channels before ALC, and allow the audio loop to run while tone samples remain even when `s_active == false`. Do not allocate an audio asset or start a second Codec writer.

Core loop condition:

```c
if (!s_active && !s_feedback_tone_pending && feedback_samples_left == 0) {
    vTaskDelay(pdMS_TO_TICKS(25));
    continue;
}
```

Tone mixing must saturate to signed 16-bit before writing.

- [ ] **Step 4: Build the shared audio path**

```bash
source /Users/kongweilu/esp/esp-idf-master/export.sh
ninja -C firmware/korvo1_yokai_demo/build-korvo1-s31-synth
```

Expected: build succeeds; only `synth_audio` calls `esp_codec_dev_write()`.

Verify ownership:

```bash
rg -n "esp_codec_dev_write" firmware/korvo1_yokai_demo/main
```

Expected: all matches remain in `synth_service.c`.

- [ ] **Step 5: Commit playback-reference integration**

```bash
git add firmware/korvo1_yokai_demo/main/synth_service.h \
        firmware/korvo1_yokai_demo/main/synth_service.c
git commit -m "feat: feed AEC playback reference"
```

### Task 6: Dispatch commands on the LVGL thread and synchronize volume

**Files:**

- Modify: `firmware/korvo1_yokai_demo/main/ui/ui_drawer.h`
- Modify: `firmware/korvo1_yokai_demo/main/ui/ui_drawer.c`
- Modify: `firmware/korvo1_yokai_demo/main/ui/ui.c`
- Create: `firmware/korvo1_yokai_demo/test/check_voice_integration.py`

- [ ] **Step 1: Write the failing integration check**

Create `test/check_voice_integration.py`:

```python
from pathlib import Path

root = Path(__file__).resolve().parents[1]
ui = (root / "main/ui/ui.c").read_text()
drawer_h = (root / "main/ui/ui_drawer.h").read_text()
synth = (root / "main/synth_service.c").read_text()

required = [
    "VOICE_TARGET_SYNTH", "VOICE_TARGET_WEATHER", "VOICE_TARGET_VOICE",
    "VOICE_TARGET_VISION", "VOICE_TARGET_FIREWORKS", "VOICE_TARGET_CLOCK",
    "VOICE_TARGET_CALCULATOR", "VOICE_TARGET_FOOD", "VOICE_TARGET_WIFI",
    "VOICE_TARGET_BLUETOOTH", "VOICE_TARGET_HOME", "VOICE_TARGET_VOLUME",
]
for token in required:
    assert token in ui, token

assert "ui_drawer_set_volume" in drawer_h
assert "voice_service_receive" in ui
assert "voice_service_set_mode" in ui
assert "esp_codec_dev_write" not in ui
assert "esp_codec_dev_write" in synth
print("Voice UI integration checks passed.")
```

- [ ] **Step 2: Run it and verify it fails**

```bash
python3 firmware/korvo1_yokai_demo/test/check_voice_integration.py
```

Expected: failure on the first missing voice target.

- [ ] **Step 3: Add one-way slider synchronization**

Declare in `ui_drawer.h`:

```c
void ui_drawer_set_volume(int volume);
```

Implement in `ui_drawer.c`:

```c
void ui_drawer_set_volume(int volume)
{
    if (s_slider_vol == NULL) {
        return;
    }
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    lv_slider_set_value(s_slider_vol, volume, LV_ANIM_OFF);
}
```

Calling the existing callback with the same value is harmless if this LVGL build emits a value-change event for programmatic updates.

- [ ] **Step 4: Add the UI-thread command dispatcher**

Include `voice_service.h` and add:

```c
static int s_voice_saved_volume = 80;

static void execute_voice_command(const voice_result_t *result)
{
    const voice_command_info_t *info = voice_service_command_info(result->command);
    if (info == NULL) {
        return;
    }

    switch (info->target) {
    case VOICE_TARGET_SYNTH:      ui_switch_screen(UI_SCREEN_SYNTH); break;
    case VOICE_TARGET_WEATHER:    ui_switch_screen(UI_SCREEN_WEATHER); break;
    case VOICE_TARGET_VOICE:      ui_switch_screen(UI_SCREEN_VOICE); break;
    case VOICE_TARGET_VISION:     ui_switch_screen(UI_SCREEN_VISION); break;
    case VOICE_TARGET_FIREWORKS:  ui_switch_screen(UI_SCREEN_FIREWORKS); break;
    case VOICE_TARGET_CLOCK:      ui_switch_screen(UI_SCREEN_CLOCK); break;
    case VOICE_TARGET_CALCULATOR: ui_switch_screen(UI_SCREEN_CALCULATOR); break;
    case VOICE_TARGET_FOOD:       ui_switch_screen(UI_SCREEN_FOOD); break;
    case VOICE_TARGET_WIFI:       on_wifi_details_requested(); break;
    case VOICE_TARGET_BLUETOOTH:  on_bt_details_requested(); break;
    case VOICE_TARGET_HOME:       ui_switch_screen(UI_SCREEN_HOME); break;
    case VOICE_TARGET_VOLUME: {
        int old_volume = synth_service_get_master_volume();
        int new_volume = voice_service_apply_volume(result->command, old_volume,
                                                     &s_voice_saved_volume);
        synth_service_set_master_volume(new_volume);
        ui_drawer_set_volume(new_volume);
        break;
    }
    default:
        break;
    }
}
```

- [ ] **Step 5: Drain results from the existing 16 ms tick**

At the end of `ui_tick_periodic()`:

```c
voice_result_t voice_result;
while (voice_service_receive(&voice_result)) {
    if (voice_result.event == VOICE_EVENT_WAKE) {
        synth_service_play_feedback_tone();
    } else if (voice_result.event == VOICE_EVENT_COMMAND) {
        execute_voice_command(&voice_result);
    }
    ui_voice_screen_update(&voice_result, synth_service_get_master_volume(),
                           voice_service_is_ready(), voice_service_error());
}
```

The `ui_voice_screen_update()` declaration and implementation land in Task 7; use a temporary declaration in `ui_apps.h` so this task compiles only after adding a no-op body there.

- [ ] **Step 6: Switch service mode from the existing screen router**

In `ui_switch_screen()`, after validating the target and before starting the transition:

```c
voice_service_set_mode(target == UI_SCREEN_VOICE
    ? VOICE_MODE_CONTINUOUS
    : VOICE_MODE_GLOBAL_WAKE);
```

Also set `VOICE_MODE_GLOBAL_WAKE` after initial home-screen creation in `ui_init()`.

Immediately after creating the drawer, synchronize its initial 70% default with the actual Codec value:

```c
ui_drawer_set_volume(synth_service_get_master_volume());
```

- [ ] **Step 7: Add a global wake/result toast**

Create one reusable black glass-card object on `lv_layer_top()` in `ui.c`. On `VOICE_EVENT_WAKE`, show `御用でしょうか`; on `VOICE_EVENT_RETRY`, show `もう一度`; on a volume command, show the feature plus the resulting percentage. Restart a single 1500 ms LVGL timer each time and hide the card in its timer callback. Do not create a toast from the recognition task.

Use one object and timer for the lifetime of the UI:

```c
static lv_obj_t *s_voice_toast;
static lv_obj_t *s_voice_toast_label;
static lv_timer_t *s_voice_toast_timer;

static void voice_toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_obj_add_flag(s_voice_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(s_voice_toast_timer);
}

static void show_voice_toast(const char *text)
{
    lv_label_set_text(s_voice_toast_label, text);
    lv_obj_remove_flag(s_voice_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(s_voice_toast_timer);
    lv_timer_resume(s_voice_toast_timer);
}
```

Create the card after the drawer in `ui_init()`, center it near the top, create its label, create the 1500 ms timer, then immediately pause the timer and hide the card.

- [ ] **Step 8: Run the checks and build**

```bash
python3 firmware/korvo1_yokai_demo/test/check_voice_integration.py
source /Users/kongweilu/esp/esp-idf-master/export.sh
ninja -C firmware/korvo1_yokai_demo/build-korvo1-s31-synth
```

Expected: static check passes and firmware builds.

- [ ] **Step 9: Commit UI-thread dispatch**

```bash
git add firmware/korvo1_yokai_demo/main/ui/ui.c \
        firmware/korvo1_yokai_demo/main/ui/ui_drawer.c \
        firmware/korvo1_yokai_demo/main/ui/ui_drawer.h \
        firmware/korvo1_yokai_demo/main/ui/ui_apps.c \
        firmware/korvo1_yokai_demo/main/ui/ui_apps.h \
        firmware/korvo1_yokai_demo/test/check_voice_integration.py
git commit -m "feat: route voice commands through LVGL"
```

### Task 7: Rebuild Voice Shrine as a continuous recognition dashboard

**Files:**

- Modify: `firmware/korvo1_yokai_demo/main/ui/ui_apps.h`
- Modify: `firmware/korvo1_yokai_demo/main/ui/ui_apps.c`

- [ ] **Step 1: Replace the simulated-recognition API**

Add to `ui_apps.h`:

```c
#include "voice_service.h"

void ui_voice_screen_update(const voice_result_t *result, int volume,
                            bool service_ready, const char *error_text);
```

- [ ] **Step 2: Replace the button/orb body with status and command-list regions**

Keep the existing 800×480 screen and common header. Inside the 768×412 glass card create:

```text
status card: x=12, y=12, width=744, height=86
command list: x=12, y=108, width=744, height=288, scrollable vertically
```

The status card contains:

- `s_lbl_voice_state` — “常時認識中” while Voice Shrine is open.
- `s_lbl_voice_result` — matched canonical command and function.
- `s_lbl_voice_detail` — language, confidence, volume result, retry, or error.

Populate the list from the single command table rather than duplicating labels:

```c
for (voice_command_t command = VOICE_COMMAND_SYNTH;
     command < VOICE_COMMAND_COUNT; ++command) {
    const voice_command_info_t *info = voice_service_command_info(command);
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_set_size(row, 712, 42);
    lv_obj_t *feature = lv_label_create(row);
    lv_label_set_text(feature, info->feature);
    lv_obj_t *english = lv_label_create(row);
    lv_label_set_text(english, info->english);
    lv_obj_t *japanese = lv_label_create(row);
    lv_label_set_text(japanese, info->japanese);
}
```

Use three fixed columns sized 150/350/190 px, `UI_FONT_SMALL` for English, and existing CJK fonts for feature/Japanese text. Remove `s_voice_listening`, `voice_btn_cb`, and the manual record button.

- [ ] **Step 3: Implement deterministic status updates**

Map events as follows:

```c
switch (result->event) {
case VOICE_EVENT_WAKE:
    state = "御用でしょうか";
    detail = "5 秒以内に命令を話してください";
    break;
case VOICE_EVENT_LISTENING:
    state = "常時認識中";
    detail = "英語または日本語の命令を待っています";
    break;
case VOICE_EVENT_COMMAND:
    state = info->feature;
    result_text = result->language == VOICE_LANGUAGE_JAPANESE
        ? info->japanese : info->english;
    break;
case VOICE_EVENT_RETRY:
    state = "もう一度";
    detail = "命令を確認できませんでした";
    break;
case VOICE_EVENT_ERROR:
    state = "音声認識を利用できません";
    detail = error_text;
    break;
default:
    state = "常時認識中";
    break;
}
```

For volume commands, append `音量: NN%` to the detail label. Clamp displayed confidence to 0–100%.

- [ ] **Step 4: Show initialization failure immediately**

After creating the Voice Shrine screen, call:

```c
ui_voice_screen_update(NULL, synth_service_get_master_volume(),
                       voice_service_is_ready(), voice_service_error());
```

When `service_ready` is false, the error state takes precedence over listening text.

- [ ] **Step 5: Build and run all source checks**

```bash
python3 firmware/korvo1_yokai_demo/test/check_voice_integration.py
python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py
python3 firmware/korvo1_yokai_demo/test/check_wifi_bt_drawer.py
source /Users/kongweilu/esp/esp-idf-master/export.sh
ninja -C firmware/korvo1_yokai_demo/build-korvo1-s31-synth
```

Expected: all scripts and firmware build pass.

- [ ] **Step 6: Commit the Voice Shrine UI**

```bash
git add firmware/korvo1_yokai_demo/main/ui/ui_apps.c \
        firmware/korvo1_yokai_demo/main/ui/ui_apps.h
git commit -m "feat: show continuous voice command dashboard"
```

### Task 8: Complete automated verification and capacity checks

**Files:**

- Modify if assertions expose omissions: files already listed above

- [ ] **Step 1: Run every host C check**

```bash
cc -std=c11 -Wall -Wextra -Werror \
  -I firmware/korvo1_yokai_demo/main \
  firmware/korvo1_yokai_demo/test/test_app_state.c \
  firmware/korvo1_yokai_demo/main/app_state.c \
  -o /tmp/korvo1_app_state_test && /tmp/korvo1_app_state_test

cc -std=c11 -Wall -Wextra -Werror \
  -DVOICE_SERVICE_LOGIC_ONLY \
  -I firmware/korvo1_yokai_demo/main \
  firmware/korvo1_yokai_demo/test/test_voice_service.c \
  firmware/korvo1_yokai_demo/main/voice_service.c \
  -o /tmp/korvo1_voice_test && /tmp/korvo1_voice_test
```

Expected: both executables exit 0; voice test prints `Voice command checks passed.`

- [ ] **Step 2: Run every Python regression check**

```bash
python3 firmware/korvo1_yokai_demo/test/check_font_charset.py
python3 firmware/korvo1_yokai_demo/test/check_wifi_bt_drawer.py
python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py
python3 firmware/korvo1_yokai_demo/test/check_voice_integration.py
```

Expected: all scripts exit 0.

- [ ] **Step 3: Perform the final firmware build**

```bash
source /Users/kongweilu/esp/esp-idf-master/export.sh
ninja -C firmware/korvo1_yokai_demo/build-korvo1-s31-synth
```

Expected: `korvo1_yokai_demo.bin` and `srmodels/srmodels.bin` are produced with no warnings introduced by project sources.

- [ ] **Step 4: Check both partition budgets**

```bash
wc -c firmware/korvo1_yokai_demo/build-korvo1-s31-synth/korvo1_yokai_demo.bin
wc -c firmware/korvo1_yokai_demo/build-korvo1-s31-synth/srmodels/srmodels.bin
```

Expected:

- App image smaller than 9,437,184 bytes.
- Model image smaller than 6,291,456 bytes.

- [ ] **Step 5: Inspect the final diff and commit any verification fixes**

```bash
git diff --check
git status --short
```

If verification required code corrections, commit only those corrections:

```bash
git add firmware/korvo1_yokai_demo
git commit -m "fix: harden bilingual voice control"
```

### Task 9: Flash, calibrate, and record real-device results

**Files:**

- Modify after successful hardware validation: `docs/AGENT-HANDOFF.md`

- [ ] **Step 1: Perform the one-time full flash including speech models**

```bash
source /Users/kongweilu/esp/esp-idf-master/export.sh
idf.py -C firmware/korvo1_yokai_demo \
  -B build-korvo1-s31-synth \
  -p /dev/cu.usbserial-1120 -b 921600 flash
```

Expected: bootloader, partition table, App, and `srmodels.bin` at model partition offset `0x910000` are written successfully.

- [ ] **Step 2: Confirm the boot pipeline**

Monitor serial output:

```bash
idf.py -C firmware/korvo1_yokai_demo \
  -B build-korvo1-s31-synth \
  -p /dev/cu.usbserial-1120 monitor
```

Expected logs include:

```text
wn9l_ja_konnichihaesp_tts3
mn7_en
MMNR
Voice service ready
```

No AFE allocation failure, ASRC timeout loop, I2S conflict, watchdog reset, or model-partition mount error is acceptable.

- [ ] **Step 3: Calibrate AEC delay**

Play synth or A2DP audio at 60% volume and speak the wake phrase from about 0.5 m. Adjust only `VOICE_AEC_DELAY_FRAMES` in 10 ms increments over 20–100 ms until wake/command reliability is best without false detection from device playback.

After each adjustment use the fast App-only flash:

```bash
source /Users/kongweilu/esp/esp-idf-master/export.sh
ninja -C firmware/korvo1_yokai_demo/build-korvo1-s31-synth
python -m esptool --chip esp32s31 -p /dev/cu.usbserial-1120 -b 921600 \
  write_flash 0x10000 \
  firmware/korvo1_yokai_demo/build-korvo1-s31-synth/korvo1_yokai_demo.bin
```

- [ ] **Step 4: Calibrate Japanese phonemes only where measured**

For each Japanese command, speak it five times at 0.5 m. If fewer than four detections succeed, change only that row's `phonemes` string in `s_commands`; keep command IDs, labels and English phrases stable. Record old/new phonemes and results in the commit message body.

- [ ] **Step 5: Execute the acceptance matrix**

Verify:

1. Wake phrase works from Home and every app.
2. Voice Shrine accepts repeated commands without wake phrase.
3. Leaving Voice Shrine restores wake-first behavior.
4. All 15 English commands and all 15 Japanese commands reach at least 4/5 in quiet conditions.
5. Wake and volume commands work with device playback at 60%.
6. Volume steps by 10%, clamps at 0/100, and mute restore is correct.
7. Drawer slider follows voice volume changes.
8. Recognition-to-action latency is approximately 1.5 seconds or less.
9. Thirty-minute operation shows no watchdog, audio DMA failure, or steadily falling free heap.
10. LCD remains at the verified 60 FPS and touch/synth/weather/Wi-Fi/Bluetooth do not regress.

- [ ] **Step 6: Update the handoff with measured facts**

Add to `docs/AGENT-HANDOFF.md`:

- Final AEC delay in milliseconds.
- Final Japanese phoneme overrides.
- App/model image sizes.
- Full-flash command and App-only flash command.
- Per-command pass counts and remaining known limitations.

- [ ] **Step 7: Commit calibrated values and handoff**

```bash
git add firmware/korvo1_yokai_demo/main/voice_service.c docs/AGENT-HANDOFF.md
git commit -m "test: calibrate Korvo-1 voice control"
```

## Official references

- ESP-SR 2.5.3 headers vendored in `firmware/korvo1_yokai_demo/managed_components/espressif__esp-sr/` are the API source of truth for this pinned project.
- ESP-ASRC 1.1.0 component: <https://components.espressif.com/components/espressif/esp_asrc/versions/1.1.0/readme>
- Official ASRC example: <https://github.com/espressif/esp-gmf/blob/main/packages/esp_asrc/examples/asrc_demo/main/asrc_example.c>
- ESP32-S31 ASRC HAL conversion table: <https://github.com/espressif/esp-idf/blob/master/components/esp_hal_asrc/esp32s31/asrc_periph.c>
- Approved design: `docs/superpowers/specs/2026-09-16-global-bilingual-voice-control-design.md`
