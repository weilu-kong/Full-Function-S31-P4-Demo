# Vision AI stability handoff — 2026-09-22

## Scope and code

- Repository: `weilu-kong/Full-Function-S31-P4-Demo`; branch: `codex/vision-ai`.
- Hardware: ESP32-S31 / Korvo-1; project: `firmware/korvo1_yokai_demo`.
- The diagnostic/fix sequence started from `daabee0`. Commits `ddf0780`, `3ae51e3`, `54d82a0`, `6c6d73c`, `0a804b2`, and `d17ed9a` add UI/preview telemetry, guarantee inference-buffer release, reserve the preview write buffer, sequence frame publication, snapshot health timestamps, and fix display triple-buffer synchronization.
- The managed-component fix is reproduced by `firmware/korvo1_yokai_demo/tools/apply_managed_component_patches.py` after dependency resolution. It registers `pending_fb` and old-buffer ownership **before** starting a panel switch in the normal and dummy-draw LVGL v9 `TRIPLE_FULL` paths.

## Root cause and verification

The diagnostic firmware reproduced a home-screen UI freeze at about 448 s. The independent health task kept running, but UI enter/exit stopped at `11991/11991`. `[DISPLAY_STALL]` showed a pending framebuffer with `frame_complete=11992` and `switch_seq=11991`: the frame-complete callback had run before `pending_fb` was registered, so the LVGL worker waited indefinitely for a switch notification that had already passed. Camera, inference, and heap loss were not required to trigger this freeze.

- Host Vision unit test passed, the managed-component patch check passed, and the ESP-IDF app build passed. The app image was `0x9aeef0` bytes with 16% of the smallest app partition free.
- The fixed app was flashed at 921600 baud; esptool verified the written-data hash.
- Home screen: approximately 632 s; UI enter/exit reached `16965/16965`, with no observed `DISPLAY_STALL`, WDT, or reboot.
- Vision screen: entered at device uptime about 67 s and remained active past uptime 1889 s (over 30 minutes in Vision). Final `cap/pub/infer=28488/28488/28482`, UI enter/exit `13167/13167`, `cam_err=0`, `no_free_buf=0`. The monitored logs showed no persistent preview/display stall, WDT, or reboot. The serial monitor was reattached once with `--no-reset`; telemetry and counters continued across that short observation gap.

## Memory interpretation and risk

`int_free` and `psram_free` are **remaining free heap bytes**, not bytes in use. In the steady Vision state, `int_free` was about **68,295 B (66.7 KiB)** and `psram_free` was generally about **492,000 B (480 KiB)**. The largest compatible PSRAM block was **450,560 B (440 KiB)**. Current free values did not decline monotonically during the 30-minute run; occasional PSRAM dips to about 485 KB recovered. Recorded low-water marks were `int_min=29,484 B` and `psram_min=436,484 B`; these are historical minima, not current free space.

This is **tight headroom**, not an all-clear: the device has 16 MB physical PSRAM, but most is committed to the UI, model/detector, camera, and preview buffers. A new allocation larger than the largest free contiguous block can fail even while aggregate PSRAM free is about 492 KB. In particular, the earlier MFN recognition memory gate (at least 900 KiB contiguous) is **not met** in this steady Vision state. Do not infer that enrollment/recognition memory feasibility passed from the preview soak.

## Remaining plan / acceptance gates

1. Keep the current telemetry while testing 20 Vision enter/exit cycles, including touch/UI responsiveness and stable heap after returning home.
2. Test enrollment failure, cancel, deletion/re-enrollment, and reboot recovery. Re-measure contiguous PSRAM before loading MFN. Only attempt MFN when its memory gate passes; if necessary, test loading it before camera allocation or while camera is stopped, then restart camera and re-check stability.
3. Exercise voice commands concurrently with Vision, then run a longer soak with screen transitions and interaction. The 30-minute result covers a mostly stationary Vision page, not all interaction paths.
4. Treat a future stall as a new observation: compare UI enter/exit, `cap/pub/consumed/infer`, buffer indices, `DISPLAY_STALL`, and heap before changing code. Do not add LVGL refresh throttling or image-descriptor changes without evidence they address a reproduced failure.

The original detailed debug plan is the local document `vision_preview_stall_debug_fix_plan.md`; this handoff records the executed phases and outstanding gates without copying that historical 1594-line plan into the repository.
