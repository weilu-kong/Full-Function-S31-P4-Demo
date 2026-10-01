# Korvo-1 Weather and Wi-Fi Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make weather refresh, local time, weather art, and saved Wi-Fi reconnection reliable and directly controllable on Korvo-1.

**Architecture:** Keep ESP-GSP JSON responsible for touch targets and visuals. Keep `weather_service` responsible for snapshot state and time formatting, while `board_ui` owns GSP routing, STA configuration and repaint timers. Reuse ESP-IDF's one persisted STA configuration; do not add a credential store.

**Tech Stack:** ESP-IDF 6.2, ESP-GSP JSON/gspc, ESP Wi-Fi STA/NVS, C11 host tests.

---

### Task 1: Make weather presentation state testable

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/weather_service.h`
- Modify: `firmware/korvo1_yokai_demo/main/weather_service.c`
- Modify: `firmware/korvo1_yokai_demo/test/test_weather_service.c`

- [ ] **Step 1: Add failing background-selection tests.**

```c
assert(weather_background_for_condition(WEATHER_COND_SUNNY) == WEATHER_BACKGROUND_SUNNY);
assert(weather_background_for_condition(WEATHER_COND_CLOUDY) == WEATHER_BACKGROUND_SUNNY);
assert(weather_background_for_condition(WEATHER_COND_RAINY) == WEATHER_BACKGROUND_RAIN);
assert(weather_background_for_condition(WEATHER_COND_SNOWY) == WEATHER_BACKGROUND_RAIN);
assert(weather_background_for_condition(WEATHER_COND_THUNDER) == WEATHER_BACKGROUND_RAIN);
```

- [ ] **Step 2: Compile the host test and confirm it fails for the missing API.**

```bash
cc -std=c11 -Wall -Wextra -Werror -DHOST_TEST -I firmware/korvo1_yokai_demo/main \
  firmware/korvo1_yokai_demo/main/weather_service.c \
  firmware/korvo1_yokai_demo/test/test_weather_service.c -lcjson -o /tmp/weather_test && /tmp/weather_test
```

- [ ] **Step 3: Add the minimal enum and helper.**

```c
typedef enum { WEATHER_BACKGROUND_SUNNY, WEATHER_BACKGROUND_RAIN } weather_background_t;

weather_background_t weather_background_for_condition(weather_cond_t condition)
{
    return (condition == WEATHER_COND_RAINY || condition == WEATHER_COND_SNOWY ||
            condition == WEATHER_COND_THUNDER) ? WEATHER_BACKGROUND_RAIN : WEATHER_BACKGROUND_SUNNY;
}
```

- [ ] **Step 4: Re-run the host test and confirm it passes.**

### Task 2: Add the GSP controls and state-specific layers

**Files:**
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_weather_800.json`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_wifi_800.json`

- [ ] **Step 1: Add transparent weather image layers and controls.** Add a hidden rain image over the existing sunny image, transparent cloud/rain/water overlay components, a `weather_refresh` button, and a dynamic `weather_clock` label. Keep all text characters in the shared charset string and add the same characters in every scene before running the charset checker.

- [ ] **Step 2: Add the saved-network operation drawer.** Add `wifi_saved_drawer` with selected SSID text, `接続`, `このネットワークを削除`, and close actions. Keep the existing password drawer for unknown encrypted networks.

- [ ] **Step 3: Change only the keyboard `font_size` from 16 to 32.** Do not change its `x`, `y`, `w`, `h`, or touch key geometry.

- [ ] **Step 4: Run GSP generation through the normal build and correct all generated action/bind identifiers.**

### Task 3: Route saved Wi-Fi networks without overwriting the password

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c`
- Modify: `firmware/korvo1_yokai_demo/test/test_app_state.c`

- [ ] **Step 1: Write a failing pure helper test for saved SSID matching.** The helper must return true only for an exact non-empty STA SSID match; it must reject an empty stored SSID.

```c
assert(wifi_ssid_is_saved("elecom", "elecom"));
assert(!wifi_ssid_is_saved("elecom", ""));
assert(!wifi_ssid_is_saved("elecom", "elecom-guest"));
```

- [ ] **Step 2: Compile and run the host test, confirming the missing helper fails.**

- [ ] **Step 3: Implement `wifi_ssid_is_saved` and split connection paths.** For a saved selected SSID, open the operation drawer and let `接続` call `esp_wifi_connect()` without `esp_wifi_set_config()`. For a new encrypted SSID, retain `wifi_connect_to_ap(ssid, password)`. For open APs, use `wifi_connect_to_ap(ssid, NULL)`.

- [ ] **Step 4: Implement forget as `esp_wifi_disconnect()` when needed followed by zeroed `wifi_config_t` via `esp_wifi_set_config(WIFI_IF_STA, &empty)`.** Clear selected/connected text and repaint the list.

- [ ] **Step 5: Replace the literal security `*` with a per-row monochrome closed-lock composed from GSP rect components.** Set its visibility from each AP auth mode; leave `[OPEN]` unchanged and reserve its label space for the lock.

- [ ] **Step 6: Run host tests and commit the Wi-Fi behavior.**

### Task 4: Refresh and repaint weather deterministically

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/weather_service.c`
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c`

- [ ] **Step 1: Add failing tests for clock text formatting: valid local time yields `HH:MM`; invalid SNTP time yields the DEMO fallback.**

- [ ] **Step 2: Make `weather_service` expose the current local clock separately from `update_time`.** It must use `localtime_r` after the existing `JST-9` setup and never force a Wi-Fi reconnect.

- [ ] **Step 3: Route `weather_refresh` to `weather_service_trigger_refresh()`.** On `IP_EVENT_STA_GOT_IP`, trigger the same entry point; do not make UI calls in the event worker.

- [ ] **Step 4: In the existing UI timer, repaint the dynamic clock once per minute and repaint weather when dirty.** Select the sunny/rain GSP image layer and cloud/rain/water overlays from `weather_background_for_condition`; animation is limited to low-rate property animation, avoiding tree movement and background replacement.

- [ ] **Step 5: Run weather host tests and commit weather behavior.**

### Task 5: Verify the integrated firmware

**Files:**
- Verify: `firmware/korvo1_yokai_demo/test/check_wifi_bt_drawer.py`
- Verify: `firmware/korvo1_yokai_demo/test/check_font_charset.py`

- [ ] **Step 1: Run all host tests and static scene checks.**

```bash
python3 firmware/korvo1_yokai_demo/test/check_wifi_bt_drawer.py
python3 firmware/korvo1_yokai_demo/test/check_font_charset.py
cc -std=c11 -Wall -Wextra -Werror -I firmware/korvo1_yokai_demo/main \
  firmware/korvo1_yokai_demo/main/app_state.c firmware/korvo1_yokai_demo/test/test_app_state.c \
  -o /tmp/korvo1_app_state_test && /tmp/korvo1_app_state_test
```

- [ ] **Step 2: Build with the documented S31 directory.**

```bash
source /Users/kongweilu/esp/esp-idf-master/export.sh >/dev/null
idf.py -C firmware/korvo1_yokai_demo -B build-korvo1-s31-synth build
```

- [ ] **Step 3: Perform the documented device checks.** Verify JST clock, refresh button, cloudy/rain image selection, saved-network direct connect, forget→password path, lock icon, and 32 px keyboard glyphs.

- [ ] **Step 4: Commit only implementation and tests; leave flash/push for explicit user instruction.**
