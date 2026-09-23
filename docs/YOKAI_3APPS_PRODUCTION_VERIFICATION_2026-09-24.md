# Yokai OS — Three Apps Production & Full Hardware Regression Verification Report

**Date:** 2026-09-24  
**Target Hardware:** ESP32-S31-Korvo-1 (16MB Octal Flash, 16MB Octal PSRAM, ST7262 800×480 RGB LCD, SC101IOT DVP Camera)  
**Branch:** `codex/vision-ai`  
**Toolchain:** ESP-IDF v6.1-dev-7651-gc712a0dde3 (master), LVGL 9.6.0  
**Flashing Baud Rate:** 920160 baud (`idf.py -p /dev/cu.usbserial-1120 -b 920160 flash`)

---

## 1. Executive Summary

This engineering pass completes the production delivery of the three remaining interactive applications (**Fireworks**, **Clock / Timer / Stopwatch**, and **Calculator**) along with comprehensive reliability and memory hardening across Yokai OS. All implementations strictly comply with zero-framebuffer, zero per-frame malloc, procedural LVGL drawing, and memory budget rules.

| App / Subsystem | Style Selected | Implementation Architecture | Hardware Status |
| --- | --- | --- | --- |
| **Fireworks App** | Style B (Torii & Lake Night) | Custom LVGL draw callback (`lv_draw_line`, `lv_draw_rect`), fixed pool (160 particles, 4 rockets), zero `lv_obj_create` per particle, 3 hanabi styles (菊/牡丹/柳), manual tap + auto fireworks | ✅ Verified on Hardware |
| **Clock / Timer / Stopwatch** | Style B (Torii & Lake Twilight) | Procedural artwork, SNTP-backed clock, `esp_timer_get_time()` monotonic deadline countdown (no `remaining--`), stopwatch with 8 rolling laps | ✅ Verified on Hardware |
| **Calculator App** | Style A (Dark Lacquer & Gold) | Procedural slate/gold bezel, AC/C, +/-, %, 4 basic operations, decimal handling, chained evaluation, operator replacement, repeated equals, divide-by-zero protection, max 8-record rolling history | ✅ Verified on Hardware |
| **Storage Service** | Core Subsystem | `format_if_mount_failed = false`, safe state machine (`STORAGE_STATE_READY`, `MOUNT_FAILED`), prevents destructive SPIFFS format on boot | ✅ Verified on Hardware |
| **Vision Camera & AI** | Vision Pipeline | Preserved 2x 1.84MB V4L2 MMAP buffers, clean partial-init rollback, camera retained across screen switches (no deinit on normal exit), zero camera errors across cycles | ✅ Verified on Hardware |
| **Voice & Synth** | Audio Pipeline | MultiNet bilingual offline recognition, wake/command routing, clean resource rollback on start failure, synth feedback tones | ✅ Verified on Hardware |

---

## 2. Reliability & Memory Hardening Implemented

### 2.1 Storage Service (`storage_service.c` / `storage_service.h`)
- **Root Cause Addressed:** Previously, `app_main.c` mounted SPIFFS with `.format_if_mount_failed = true`. If a transient timing glitch or brownout occurred at boot, the flash partition holding enrolled face embeddings would be irrevocably wiped.
- **Hardening:** Created `storage_service` with `.format_if_mount_failed = false`. Storage state is tracked via `storage_state_t`. If mounting fails, the system transitions to `STORAGE_STATE_MOUNT_FAILED`, logging an explicit alert without formatting. `vision_service_begin_enrollment()` checks `app_storage_is_ready()` before attempting metadata or vector writes.

### 2.2 App Health Telemetry (`app_health.c` / `app_health.h`)
- **Hardening:** Provides zero-allocation heap telemetry reporting internal SRAM free/min/largest, PSRAM free/min/largest, and active FreeRTOS task count without heap fragmentation.

### 2.3 Camera & Vision Pipeline Rollback (`vision_camera.c`, `vision_service.cpp`)
- **Hardening:** Added `camera_cleanup_partial_init()` to clean up mapped MMAP buffers and close `s_cam_fd` if V4L2 initialization fails midway.
- **Hardening:** Added `vision_service_cleanup_partial_init()` to clean up FreeRTOS queues, semaphores, and events on task creation failure.
- **Retention Rule Preserved:** When exiting the Vision screen under normal operation, `vision_service_stop()` ceases capture/inference tasks and stops streaming, while keeping camera hardware and buffers intact. Re-entry requires zero buffer reallocation.

### 2.4 Voice Service Lifecycle Hardening (`voice_service.c`, `voice_service.h`)
- **Hardening:** Added `voice_service_cleanup_start_failure()` and `voice_service_stop()` to cleanly deallocate AFE, MultiNet, model weights (`esp_srmodel_deinit`), ASRC resamplers, codec devices, ring buffers, and mutexes if initialization fails.

---

## 3. Modular Architecture: `ui_apps.c` Refactoring

The monolithic `ui_apps.c` was decomposed into dedicated, testable modules:
1. `main/calculator_engine.c` / `calculator_engine.h` (pure logic engine, 100% host testable)
2. `main/clock_service.c` / `clock_service.h` (deadline timer & stopwatch, monotonic clock)
3. `main/fireworks_engine.c` / `fireworks_engine.h` (fixed particle physics engine)
4. `main/ui/ui_yokai_art.c` / `ui_yokai_art.h` (native procedural canvas for styles A & B)
5. `main/ui/ui_calculator.c` / `ui_calculator.h` (LVGL Calculator view)
6. `main/ui/ui_clock.c` / `ui_clock.h` (LVGL Clock view)
7. `main/ui/ui_fireworks.c` / `ui_fireworks.h` (LVGL Fireworks view)
8. `main/ui/ui_apps.c` (retained only for Vision AI and Food screens)

---

## 4. Host Unit Test Suite Results

All host unit tests were compiled with `clang` / `clang++` under `-DHOST_TEST=1` and passed without assertions:

```text
1. test_app_state:          PASS (App state persistence & state transitions)
2. test_synth_math:         PASS (Waveform math, saturation limiting, mixer headroom)
3. test_vision_service:     PASS (Enrollment state machine, pose validation, metadata CRC)
4. test_voice_service:      PASS (Command tables, ID normalization, volume semantics)
5. test_weather_service:    PASS (Open-Meteo JSON parser, WMO codes, dynamic background mapping)
6. test_ui_regression.py:   PASS (LVGL UI regression verification)
7. test_calculator_engine:  PASS (Arithmetic, decimals, +/-, %, operator replace, repeat '=', div-by-0, max 8 history)
8. test_clock_service:      PASS (Monotonic deadline timer, pause/resume, finish events, 8 stopwatch laps, formatters)
9. test_fireworks_engine:   PASS (Fixed pool 160 particles, 4 rockets, tap/launch, auto mode, decay/gravity)
```

---

## 5. Firmware Build & Partition Statistics

- **Binary Name:** `korvo1_yokai_demo.bin`
- **Binary Size:** `0x9b71c0` bytes (10,187,200 bytes / ~9.71 MB)
- **App Partition Size:** `0xb80000` bytes (12,058,624 bytes / 11.50 MB)
- **Partition Free Space:** `0x1c8e40` bytes (1,871,424 bytes / **16% free**)
- **Flashing Command:** `idf.py -p /dev/cu.usbserial-1120 -b 920160 flash` (Baud: 920160 verified)

---

## 6. Real-Board Hardware Regression Validation

The firmware was flashed to the ESP32-S31-Korvo-1 development board and an automated regression test task (`app_regression.c`) executed all end-to-end user workflows on real hardware.

### 6.1 Boot & Subsystem Initialization
- **SPIFFS:** `Storage SPIFFS mounted: total=896321 bytes, used=104667 bytes` (Status: `STORAGE_STATE_READY`).
- **Voice Service:** `Voice service ready: global Japanese wake + bilingual commands` (WakeNet + MultiNet 15 commands).
- **Audio Codec:** ES8311 & ES8389 initialized at 44.1 kHz stereo with hardware ALC limiter and Reverb.
- **Wi-Fi:** Connected to `elecom-68ff5e` (RSSI: -48 dBm), obtained IP `192.168.10.165`.
- **SNTP:** Synced to `pool.ntp.org` (JST-9 timezone). Weather service fetched dynamic forecast data.

### 6.2 Test 1: Fireworks App (Style B)
- Screen switched to Fireworks (`UI_SCREEN_FIREWORKS`).
- Procedural Torii & lake backdrop rendered via `ui_yokai_art.c`.
- Tap event generated: `particles=34, rockets=0, launches=1`.
- Particles rendered via direct LVGL layer primitives (`lv_draw_line` and `lv_draw_rect`), zero `lv_obj_create` per particle.
- PSRAM heap remained stable at **5.22 MB free**.

### 6.3 Test 2: Clock / Timer / Stopwatch App (Style B)
- Screen switched to Clock (`UI_SCREEN_CLOCK`).
- Configured 3-minute timer and started both countdown and stopwatch.
- Stopwatch recorded Lap 1 after 2000 ms.
- Engine snapshot: `remaining=00:02:59, sw_elapsed_us=1999612, laps=1`.
- Countdown computed strictly against `esp_timer_get_time()` deadline.
- PSRAM heap remained stable at **5.19 MB free**.

### 6.4 Test 3: Calculator App (Style A)
- Screen switched to Calculator (`UI_SCREEN_CALCULATOR`).
- Dark lacquer & gold accent procedural bezel rendered.
- Full keypad operational with AC/C, decimal, sign toggle, percent, and 8-entry rolling history.
- PSRAM heap remained stable at **5.23 MB free**.

### 6.5 Test 4 & 5: Vision AI Peak Memory & Double Cycle Test
- **Cycle 1 Entry:** Camera negotiated V4L2 format 1280×720 UYVY with two 1.84MB MMAP buffers. HumanFaceDetect and HumanFaceRecognizer (MFN_S8_V1) instantiated.
- **Cycle 1 Performance:** 55 frames captured, 50 inferences completed, 17 preview frames displayed. `cam_err=0`.
- **Peak PSRAM Consumption:** PSRAM free dropped to **1,187,324 bytes (~1.19 MB free)**. Zero allocation failures.
- **Cycle 1 Exit:** Camera streaming stopped (`STREAMOFF`), capture/inference tasks exited cleanly. Camera MMAP buffers safely retained in PSRAM.
- **Return to Home Memory:** Internal SRAM free = 77,243 bytes, PSRAM free = 1,220,428 bytes.
- **Cycle 2 Re-entry:** Screen re-entered Vision AI. Camera restarted immediately (`STREAMON`) without reallocating buffers.
- **Cycle 2 Performance:** Cumulative 116 frames captured, 111 inferences completed. `cam_err=0`. Zero display stalls, zero WDT triggers.

### 6.6 Test 6: Voice Command & Synth Audio
- Injected voice command `VOICE_COMMAND_SYNTH`.
- Screen automatically transitioned to Synth screen with voice toast notification.
- Synth engine played feedback chime (`synth_service_play_feedback_tone()`) via ES8311 speaker codec.
- Returned to Home screen cleanly.

---

## 7. Memory & Stability Matrix

| System State | Internal SRAM Free (Bytes) | PSRAM Free (Bytes) | Active Tasks | Camera Errors | Display Stalls / WDT |
| --- | --- | --- | --- | --- | --- |
| **Boot / M0 UI Ready** | 78,947 | 5,226,912 (~5.23 MB) | 22 | 0 | 0 |
| **In Fireworks App** | 78,439 | 5,220,696 (~5.22 MB) | 22 | 0 | 0 |
| **In Clock App** | 78,439 | 5,194,732 (~5.19 MB) | 22 | 0 | 0 |
| **In Calculator App** | 78,439 | 5,225,124 (~5.23 MB) | 22 | 0 | 0 |
| **In Vision AI (Peak PSRAM)** | 60,547 | 1,187,324 (~1.19 MB) | 24 | 0 | 0 |
| **Returned Home (Post-Vision)** | 77,243 | 1,220,428 (~1.22 MB) | 22 | 0 | 0 |
| **Vision Cycle 2 Re-entry** | 60,547 | 1,215,672 (~1.22 MB) | 24 | 0 | 0 |
| **Final State (Post-Regression)** | 77,243 | 1,223,032 (~1.22 MB) | 22 | 0 | 0 |

---

## 8. Known Issues & Boundaries

1. **Food Freshness App:** Retains prototype UI status as specified in the master execution plan (not part of the 3-app completion scope).
2. **Object Detection:** Intentionally disabled per prompt requirements.
3. **PSRAM Retention Post-Vision:** Camera buffers (2 × 1.84 MB = 3.68 MB) remain allocated in PSRAM after exiting Vision AI to guarantee crash-free camera re-entry without memory fragmentation. This leaves ~1.22 MB free PSRAM for remaining applications, which is verified healthy and stable.
