# Weather Native Status Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Use the approved day/night weather artwork with a native dynamic header and restore the top-edge Quick Settings gesture.

**Architecture:** The weather scene owns layout-only header widgets over the intentionally blank header in the artwork. `board_ui.c` owns shared Wi-Fi/volume state and publishes it whenever state changes; embedded immutable PNGs are submitted through the GSP borrowed-image API so runtime theme selection cannot fail due to a large heap copy.

**Tech Stack:** ESP-IDF 6.2, ESP-GSP 1.2, C, JSON scene bundle, Python source-regression script.

---

### Task 1: Lock in the scene contract

**Files:**
- Modify: `firmware/korvo1_yokai_demo/test/test_ui_regression.py`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_weather_800.json`

- [ ] **Step 1: Add failing assertions for native header ownership**

```python
weather_scene = json.loads(weather)
weather_objects = weather_scene["objects"]
assert weather_objects[11]["image"] == "../assets/images/yokai_weather_day_800.png"
assert not any(obj.get("name") == "weather_header" for obj in weather_objects)
assert 'esp_gsp_set_image_borrowed' in ui
assert 'weather_status_dirty' in ui
```

- [ ] **Step 2: Run the source regression script**

Run: `python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py`

Expected: failure because the current authored image is `yokai_weather_sunny_800.png`, the opaque header exists, and the copy API is used.

- [ ] **Step 3: Replace the obsolete authored image and delete the opaque header objects**

```json
{
  "type": "image",
  "parent": -1,
  "name": "weather_bg",
  "x": 0,
  "y": 0,
  "w": 800,
  "h": 480,
  "image": "../assets/images/yokai_weather_day_800.png",
  "bind": "weather_bg"
}
```

Delete `weather_header` and `weather_wifi_state`; retain only one named
header status label rendered after the title widgets.

- [ ] **Step 4: Run the source regression script**

Run: `python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py`

Expected: the scene assertions still fail until Task 2 changes the image API.

### Task 2: Publish immutable day/night art and shared status state

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c`
- Modify: `firmware/korvo1_yokai_demo/test/test_ui_regression.py`

- [ ] **Step 1: Add a failing source check for the borrow path and event-driven refresh**

```python
assert 'esp_gsp_set_image_borrowed(ui, GSP_KORVO_WEATHER_BIND_WEATHER_BG' in ui
assert 'static bool s_weather_status_dirty' in ui
assert 's_weather_status_dirty = true;' in ui
```

- [ ] **Step 2: Submit embedded immutable PNGs without copying them**

```c
static void weather_image_released(void *ctx, const void *data, size_t size)
{
    (void)ctx; (void)data; (void)size;
}

static void set_weather_background(esp_gsp_handle_t ui, weather_theme_t theme)
{
    const uint8_t *data = theme == WEATHER_THEME_NIGHT ? weather_night_png_start : weather_day_png_start;
    size_t size = theme == WEATHER_THEME_NIGHT ? (size_t)(weather_night_png_end - weather_night_png_start)
                                               : (size_t)(weather_day_png_end - weather_day_png_start);
    esp_gsp_err_t err = esp_gsp_set_image_borrowed(ui, GSP_KORVO_WEATHER_BIND_WEATHER_BG,
                                                    data, size, weather_image_released, NULL);
    if (err != ESP_GSP_OK) ESP_LOGE(TAG, "weather image submit failed: %d", (int)err);
}
```

- [ ] **Step 3: Set `s_weather_status_dirty` at each state source**

Set it after Wi-Fi connection-state mutations, after a Wi-Fi toggle mutation,
and when the Quick Settings volume differs from its last observed value. In the
50 ms timer, call `apply_weather_ui(ui)` only when the Weather scene is live and
the dirty flag is set; retain the minute check for the clock.

- [ ] **Step 4: Run the regression script**

Run: `python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py`

Expected: `UI regression checks passed`.

### Task 3: Restore header visuals and the pull-down gesture

**Files:**
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_weather_800.json`
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c`
- Modify: `firmware/korvo1_yokai_demo/test/test_ui_regression.py`

- [ ] **Step 1: Define the native header on the day/night art's blank band**

Use a transparent back button at x=8, a title label at x=40 (`天気`), the
existing `LIVE` badge, and one right-aligned `weather_clock_text` label at
x=420, y=7, w=360. The label displays `HH:MM`, a Wi-Fi glyph and `♪ NN`.
It must not include an opaque rectangle or any full-width touchable object.

- [ ] **Step 2: Build header text from the shared state**

```c
snprintf(clock_buf, sizeof(clock_buf), "%02d:%02d   %s  ♪%ld",
         clock_local.tm_hour, clock_local.tm_min, wifi_mark, (long)volume);
```

Keep `wifi_mark` tied to disconnected, connecting, connected and failed
connection states. Remove the duplicate `weather_wifi_state` update.

- [ ] **Step 3: Add a gesture-regression source check**

```python
assert weather_scene['swipe'] is False
assert not any(obj.get('name') == 'weather_header' for obj in weather_objects)
assert not any(obj.get('type') == 'rect' and obj.get('y') == 0 and obj.get('h') == 46
               for obj in weather_objects)
```

- [ ] **Step 4: Run the source regression script**

Run: `python3 firmware/korvo1_yokai_demo/test/test_ui_regression.py`

Expected: `UI regression checks passed`.

### Task 4: Build, flash, and validate

**Files:**
- Verify: `build-korvo1-s31-synth/korvo1_yokai_demo.bin`

- [ ] **Step 1: Build**

Run: `source /Users/kongweilu/esp/esp-idf-master/export.sh && idf.py -C firmware/korvo1_yokai_demo -B "/Users/kongweilu/Development/Full Demo/.worktrees/synth-groovebox/build-korvo1-s31-synth" build`

Expected: `Successfully created ESP32-S31 image` and app size below `0x900000`.

- [ ] **Step 2: Flash at 921600 baud**

Run: `source /Users/kongweilu/esp/esp-idf-master/export.sh && idf.py -C firmware/korvo1_yokai_demo -B "/Users/kongweilu/Development/Full Demo/.worktrees/synth-groovebox/build-korvo1-s31-synth" -p /dev/cu.usbserial-1120 -b 921600 flash`

Expected: `Hash of data verified` followed by `Hard resetting via RTS pin`.

- [ ] **Step 3: Hardware acceptance check**

Verify top-edge pull opens Quick Settings; changing volume changes `♪ NN`
without leaving Weather; Wi-Fi connect/disconnect changes its glyph; daytime
shows the approved day artwork and nighttime shows the approved night artwork.
