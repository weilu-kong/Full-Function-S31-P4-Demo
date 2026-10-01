# Yokai quality and resource improvements implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Complete the user-approved audit roadmap while retaining the verified 400×300 PPA preview and bounded resource use.

**Architecture:** Repair existing service state/lifecycle paths and reuse queues, periodic UI updates and storage. Release hidden weather JPEG decode memory before Vision. Add a bounded Food record service with existing LVGL widgets rather than models or new dependencies. Diagnostic task statistics belong to a separate build configuration.

**Tech Stack:** ESP32-S31 Korvo-1, ESP-IDF 6.1, LVGL, ESP-DL, ESP-SR, C/C++, Python host checks.

User approval: the user explicitly requested implementation in the preceding audit's order and has authorized autonomous decisions. No additional design approval is required. Large new object models and A/B OTA remain deferred as recommended; document their status honestly.

## 1. Quality fixes (independent service scopes)

- [x] Audio: trace voice startup/runtime cleanup and every Bluetooth enabled callback; add behavioral host fault checks, observe failure, repair cleanup/event ordering and OFF state races. Files: main/voice_service.c, main/synth_service.c and focused test scripts.
- [x] Calculator/weather: add regression assertions for sign after operator, invalid JSON types/ranges, stale weather status; observe failures; minimally correct engines and weather UI as necessary. Files: main/calculator_engine.c, main/weather_service.[ch], main/ui/ui_weather.c and existing tests.
- [x] Wi-Fi: exercise SDK connection/config failures, 32-byte SSIDs and stale scan worker completion; repair ownership/return handling in main/board_ui.c and bounded host checks.
- [x] Vision: propagate startup and management completion/failure through existing result state; test queue/storage failure and refresh on completion. Files: main/vision_service.[h/cpp], main/ui/ui.c, main/ui/ui_apps.c, host tests.
- [x] Timer: consume existing completion event from global UI tick and reuse existing notification/sound. Verify inactive Clock completion produces exactly one event.
- [x] Fireworks: align test with recycle-on-full behavior, assert bounded pool and particle replacement. Add a single portable host runner, CI and current SDK/preview documentation.

## 2. Resource recovery and measurement

- [x] Add weather background detach/cache-drop/free and reload on entry, preserving descriptors until no LVGL references remain. Verify repeated Weather/Home/Vision transitions and allocation failure fallback. Files: main/ui/ui_image_loader.[ch], main/ui/ui_weather.c, main/ui/ui.c and focused host guard/behavior check.
- [x] Add optional diagnostic profile with FreeRTOS runtime stats and bounded per-task sampling via existing health logging. Keep production profile lean and document CPU interpretation and mixed-load protocol.
- [ ] Run complete host checks and ESP-IDF 6.1 build; compare binary bytes, partitions and map with 10,563,344-byte baseline. Flash only a verified build and record heap minima/largest blocks, task stacks, preview and audio counters; keep serial capture finite and preserve rollback artifact.

## 3. Bounded completeness

- [x] Food: at most 32 records, bounded name and expiry date, add/edit/delete, existing storage with atomic replace, existing clock/date validation, no camera/model. Create main/food_service.[ch], focused host test; minimally replace static Food page with existing LVGL modal/list patterns and compile registration. Test capacity, corrupted storage, failed write and date boundaries before implementation.
- [x] Replace fictitious object-confidence output with explicit unavailable/demo status while retaining actual face pipeline.

## 4. Integration and release

- [x] Independently review combined diff and fix findings; run unified host checks, full firmware build and appropriate board checks after final changes.
- [ ] Update handoff/report with verified vs unmeasured results, resource deltas, deployment hash and remaining mixed-load/manual checks.
- [ ] Commit cohesive verified changes, push current branch, create/attach integration PR to main with final scope and test evidence. Prefer reviewable PR over merging unrelated old branches or moving default branch without a concrete verified result.

No step is marked complete from source inspection alone. Resource savings and runtime CPU remain measurements to obtain, not promised outcomes.

2026-10-01 checkpoint: bfbe0b1 quality changes and c7ed078 CI context fix pushed; PR #2 attached. Diagnostic board run exposed weather TLS stack overflow; rollback to original stable app. 8192-byte weather stack and public-header patch built/host checked, board revalidation and CI pending. See implementation report for paths.
