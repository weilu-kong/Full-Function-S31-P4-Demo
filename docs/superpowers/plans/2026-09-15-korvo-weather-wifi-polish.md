# Korvo Weather and Wi-Fi Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep Wi-Fi connected across scene changes and complete the saved-network, weather clock, layout, and live status UI.

**Architecture:** `board_ui.c` remains the owner of Wi-Fi connection state and scene synchronization. JSON scenes expose only renderable labels and controls; the 50 ms UI timer updates the active scene from the single runtime state. The existing Python regression script validates scene geometry and the source-level state paths that are hardware-specific.

**Tech Stack:** ESP-IDF 6.2, ESP32-S31, ESP-GSP 0.3.0, C, JSON scene bundle, Python standard library regression script.

---

## File structure

- `firmware/korvo1_yokai_demo/main/board_ui.c` — owns Wi-Fi toggle synchronization, saved-sheet state, top-clock and status values.
- `firmware/korvo1_yokai_demo/scenes/korvo_wifi_800.json` — saved-network action-sheet labels and lock glyph resources.
- `firmware/korvo1_yokai_demo/scenes/korvo_weather_800.json` — non-overlapping weather labels and live top-status bindings.
- `firmware/korvo1_yokai_demo/test/test_ui_regression.py` — static regression checks for each requested UI contract.

### Task 1: Prevent scene changes from disconnecting Wi-Fi

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c:334-503, 720-756`
- Test: `firmware/korvo1_yokai_demo/test/test_ui_regression.py`

- [ ] **Step 1: Write the failing regression test**

Add these assertions to `test_ui_regression.py`:

```python
assert "static bool s_wifi_toggle_syncing" in ui
assert "s_wifi_toggle_syncing = true;" in ui
assert "esp_gsp_component_set_checked(ui, GSP_OBJ_KEY_WIFI_ENABLED, s_wifi_enabled)" in ui
assert "if (s_wifi_toggle_syncing)" in ui
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py`

Expected: assertion failure because the synchronization guard is absent.

- [ ] **Step 3: Add the synchronization guard**

Add one state flag next to `s_wifi_enabled`:

```c
static bool s_wifi_toggle_syncing;
```

Add a helper that owns the programmatic control update:

```c
static void sync_wifi_toggle(esp_gsp_handle_t ui)
{
    s_wifi_toggle_syncing = true;
    (void)esp_gsp_component_set_checked(ui, GSP_OBJ_KEY_WIFI_ENABLED, s_wifi_enabled);
    s_wifi_toggle_syncing = false;
}
```

Call `sync_wifi_toggle(ui)` at the start of `apply_toggle_from_widget()` before reading the widget when a scene has just changed, and return early while `s_wifi_toggle_syncing` is true. Call it in the `ESP_GSP_EVENT_SCENE_CHANGED` branch before `update_drawer_quick_controls(ui)`.

- [ ] **Step 4: Run the regression test and build**

Run: `python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py`

Expected: `UI regression checks passed`.

Run: `source /Users/kongweilu/esp/esp-idf-master/export.sh >/dev/null && idf.py -C firmware/korvo1_yokai_demo -B /Users/kongweilu/Development/Full\ Demo/.worktrees/synth-groovebox/build-korvo1-s31-synth build`

Expected: `Successfully created ESP32-S31 image.`

- [ ] **Step 5: Commit the task**

```bash
git add firmware/korvo1_yokai_demo/main/board_ui.c firmware/korvo1_yokai_demo/test/test_ui_regression.py
git commit -m "fix: preserve Wi-Fi across Korvo scenes"
```

### Task 2: Make saved networks self-identifying and removable

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c:521-534, 841-881`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_wifi_800.json:567-646`
- Test: `firmware/korvo1_yokai_demo/test/test_ui_regression.py`

- [ ] **Step 1: Write the failing regression test**

Add:

```python
assert 'text": "保存済みネットワーク"' in wifi_scene
assert 'text": "このネットワーク設定を削除"' in wifi_scene
assert '"wifi_saved_ssid"' in wifi_scene
assert '"secure_lock"' in wifi_scene
assert '" [LOCK]"' not in ui
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py`

Expected: assertion failure because `[LOCK]` and `設定` remain.

- [ ] **Step 3: Replace textual security markers and sheet copy**

Use a small preloaded lock-glyph label named `secure_lock` in each Wi-Fi scan row rather than appending security text to the SSID. Bind it only for secure APs. Change the saved drawer header to `保存済みネットワーク`; retain `wifi_saved_ssid` as the selected SSID; replace `設定` with `このネットワーク設定を削除`. Keep the existing callbacks `wifi_saved_connect`, `wifi_saved_forget`, and `wifi_saved_cancel` unchanged.

- [ ] **Step 4: Run the regression test and build**

Run: `python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py`

Expected: `UI regression checks passed`.

Run the Task 1 build command.

Expected: `Successfully created ESP32-S31 image.`

- [ ] **Step 5: Commit the task**

```bash
git add firmware/korvo1_yokai_demo/main/board_ui.c firmware/korvo1_yokai_demo/scenes/korvo_wifi_800.json firmware/korvo1_yokai_demo/test/test_ui_regression.py
git commit -m "fix: clarify saved Wi-Fi actions"
```

### Task 3: Repair the Weather information layer and local clock

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c:580-640, 680-690`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_weather_800.json:130-220`
- Test: `firmware/korvo1_yokai_demo/test/test_ui_regression.py`

- [ ] **Step 1: Write the failing regression test**

Add:

```python
assert 'name": "weather_top_clock"' in weather_scene
assert 'bind": "weather_top_clock"' in weather_scene
assert 'name": "weather_cond_label"' in weather_scene and '"y": 292' in weather_scene
assert 'strftime(clock, sizeof(clock), "%H:%M", &local_tm)' in ui
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py`

Expected: assertion failure because the active weather scene lacks a top-clock binding.

- [ ] **Step 3: Bind and update the local clock**

Add a weather top-clock label that does not overlap the live Wi-Fi/volume region. In the UI timer, when `s_live_scene == GSP_BUNDLE_SCENE_KORVO_WEATHER`, derive local time and set the clock:

```c
time_t now = time(NULL);
struct tm local_tm;
char clock[6];
localtime_r(&now, &local_tm);
strftime(clock, sizeof(clock), "%H:%M", &local_tm);
(void)esp_gsp_set_text(ui, GSP_KORVO_WEATHER_BIND_WEATHER_TOP_CLOCK, clock);
```

Move `weather_cond_label` above its separator and retain the refresh hit target over the update row.

- [ ] **Step 4: Run the regression test and build**

Run: `python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py`

Expected: `UI regression checks passed`.

Run the Task 1 build command.

Expected: `Successfully created ESP32-S31 image.`

- [ ] **Step 5: Commit the task**

```bash
git add firmware/korvo1_yokai_demo/main/board_ui.c firmware/korvo1_yokai_demo/scenes/korvo_weather_800.json firmware/korvo1_yokai_demo/test/test_ui_regression.py
git commit -m "fix: synchronize Korvo weather time"
```

### Task 4: Add shared live Weather status icons

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c:94-124, 580-640, 680-690`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_weather_800.json:130-220`
- Test: `firmware/korvo1_yokai_demo/test/test_ui_regression.py`

- [ ] **Step 1: Write the failing regression test**

Add:

```python
assert 'name": "weather_wifi_icon"' in weather_scene
assert 'name": "weather_volume_icon"' in weather_scene
assert 'WIFI_CONN_STATE_CONNECTING' in ui
assert 'WIFI_CONN_STATE_CONNECTED' in ui
assert 'WIFI_CONN_STATE_FAILED' in ui
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py`

Expected: assertion failure because the Weather scene has no live icon bindings.

- [ ] **Step 3: Add the minimal shared state renderer**

Add weather labels `weather_wifi_icon` and `weather_volume_icon` beside the top clock. In `apply_weather_ui`, set Wi-Fi to `○`, `◌`, `●`, or `!` for off, connecting, connected, or failed. Set volume to `⌁` when audible and `×` when muted. Update these labels from the existing timer after Wi-Fi state changes.

- [ ] **Step 4: Run the full static regression test and build**

Run: `python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py`

Expected: `UI regression checks passed`.

Run the Task 1 build command.

Expected: `Successfully created ESP32-S31 image.`

- [ ] **Step 5: Flash and verify the resident image**

Write the exact build directory with esptool, then read back application offset `0x32c1d0` and compare it to file offset `0x31c1d0`. Expected: matching bytes and a boot log containing `UI live: 11 scene(s)`.

- [ ] **Step 6: Commit the task**

```bash
git add firmware/korvo1_yokai_demo/main/board_ui.c firmware/korvo1_yokai_demo/scenes/korvo_weather_800.json firmware/korvo1_yokai_demo/test/test_ui_regression.py
git commit -m "feat: show live Korvo weather status"
```
