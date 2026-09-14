# Korvo-1 Weather, Wi-Fi, Status, and Day/Night Theme Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a reliable Weather page with approved day/night artwork, live shared status icons, and a saved-network Wi-Fi experience.

**Architecture:** Keep Wi-Fi persistence in ESP-IDF STA flash storage and add small UI helpers that distinguish a saved reconnect from a new credential submission. Keep a shared runtime status snapshot in `board_ui.c`, refresh it from Wi-Fi/audio events and apply it to each open scene. Weather uses its existing `is_day` value to choose embedded day/night artwork; only dynamic information is drawn by GSP bindings.

**Tech Stack:** ESP-IDF 6.2, ESP-GSP scenes/bindings, FreeRTOS, ESP Wi-Fi STA, Open-Meteo, C host tests.

---

### Task 1: Verify and expose weather theme selection

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/weather_service.h`
- Modify: `firmware/korvo1_yokai_demo/main/weather_service.c`
- Test: `firmware/korvo1_yokai_demo/test/test_weather_service.c`

- [ ] Add a failing host test for `weather_theme_for_info(false) == WEATHER_THEME_NIGHT` and fallback JST hour selection.
- [ ] Implement `weather_theme_t` with `DAY` and `NIGHT`; return `info.is_day` for live data and `06:00 <= local_hour < 18:00` for fallback data.
- [ ] Run the existing host compiler command for `test_weather_service.c`; expected output: `All weather service unit tests passed.`
- [ ] Commit: `feat(weather): select day night theme`.

### Task 2: Install approved artwork and correct Weather layout

**Files:**
- Create: `firmware/korvo1_yokai_demo/assets/images/yokai_weather_day_800.png`
- Create: `firmware/korvo1_yokai_demo/assets/images/yokai_weather_night_800.png`
- Modify: `firmware/korvo1_yokai_demo/main/CMakeLists.txt`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_weather_800.json`
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c`

- [ ] Copy the approved generated day/night artwork into project assets, preserve the source generated copies, resize to 800x480, and use versioned filenames.
- [ ] Embed both PNGs in the `main` component; bind `weather_bg` as an image source.
- [ ] Delete `weather_clock`; preserve the common top-bar clock only.
- [ ] Replace the top-right refresh button with a left-card button over the existing `weather_time` row. Its label remains `更新 HH:MM`; its action id calls `weather_service_trigger_refresh()`.
- [ ] In `apply_weather_ui`, set the image bytes to the day or night asset based on `weather_theme_for_info`.
- [ ] Build with `idf.py -C firmware/korvo1_yokai_demo -B build-korvo1-s31-synth build`; expected output: `Project build complete.`
- [ ] Commit: `feat(weather): add approved day night themes`.

### Task 3: Create shared live status icon state

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_home_800.json`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_weather_800.json`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_wifi_800.json`
- Modify: every other `firmware/korvo1_yokai_demo/scenes/korvo_*_800.json` that contains `volume`

- [ ] Add shared enum values for Wi-Fi: disconnected/weak/medium/strong, and volume: mute/low/medium/high.
- [ ] Map connected STA RSSI to weak below -75 dBm, medium below -60 dBm, strong otherwise; map no IP to disconnected.
- [ ] Read speaker output volume and map 0/mute, 1–33, 34–66, 67–100 to the four volume states.
- [ ] Replace baked Wi-Fi and speaker images with named GSP binds or runtime-drawn icon objects on every scene; update only the currently visible scene when state changes.
- [ ] Add a small host-testable mapping function test for RSSI and volume thresholds.
- [ ] Commit: `feat(ui): synchronize live status icons`.

### Task 4: Implement saved-network connect and forget flow

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_wifi_800.json`
- Test: `firmware/korvo1_yokai_demo/test/test_wifi_ui_logic.c`

- [ ] Write a host test for exact SSID matching, saved reconnect selection, and clear-config behavior.
- [ ] Add `wifi_is_saved_ssid`, `wifi_reconnect_saved`, and `wifi_forget_saved` helpers. `wifi_reconnect_saved` calls `esp_wifi_connect()` without `esp_wifi_set_config`; `wifi_forget_saved` disconnects and stores a zeroed `wifi_config_t`.
- [ ] Add a saved-network drawer with `接続`, `このネットワーク設定を削除`, and close actions.
- [ ] Route encrypted saved SSIDs to this drawer; route unsaved encrypted SSIDs to the current password drawer; preserve direct connection for open SSIDs.
- [ ] On reconnect failure retain credentials and show the existing connection error text. On forget success clear selected SSID and return to scan state.
- [ ] Run host test and build; expected output: test PASS and `Project build complete.`
- [ ] Commit: `feat(wifi): reconnect and forget saved network`.

### Task 5: Polish secure scan rows and keyboard glyphs

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_wifi_800.json`

- [ ] Replace the `" *"` string in `apply_wifi_scan_ui` with a fixed secure-row lock object/bind; hide it for open networks.
- [ ] Set `wifi_keyboard.font_size` from 16 to 32; do not alter x/y/w/h.
- [ ] Add a scene regression script that asserts no secure row label contains `*` and the keyboard retains `w: 780`, `h: 364`, `font_size: 32`.
- [ ] Run the script and full firmware build.
- [ ] Commit: `fix(wifi): show locks and readable keyboard glyphs`.

### Task 6: Hardware acceptance and release verification

**Files:**
- Modify: `docs/AGENT-HANDOFF.md`

- [ ] Flash using the root worktree build directory only: `build-korvo1-s31-synth`, 16 MB, DIO, 80 MHz; do not use the stale `firmware/.../build-korvo1-s31-synth` directory.
- [ ] Require `Hash of data verified` for bootloader, partition table, app, and model partition.
- [ ] Capture boot log showing 16 MB Flash, valid partition table, app entering `app_main()`, and no build checksum mismatch.
- [ ] Manually verify the seven acceptance checks in the design; record pass/fail and any observed RSSI/volume state transitions in `docs/AGENT-HANDOFF.md`.
- [ ] Commit: `docs: record weather wifi verification`.
