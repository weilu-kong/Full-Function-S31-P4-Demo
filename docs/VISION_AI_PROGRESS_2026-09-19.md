# Korvo-1 Vision AI progress — 2026-09-19

## Environment

- Branch: `codex/vision-ai`
- Project: `firmware/korvo1_yokai_demo`
- Target: ESP32-S31 / Korvo-1
- ESP-IDF: `v6.1-dev-7651-gc712a0dde3`
- Flash: 16 MB
- App partition: `0xb80000` (11.5 MiB)
- Current app image: `0x9addd0` bytes; `0x1d2230` bytes (16%) free

## Verified

- HOME remains active after boot; the temporary six-second automatic Vision transition is removed.
- Empty face database enters Vision without loading MFN.
- Camera preview and face boxes work with two V4L2 buffers.
- The one-buffer camera experiment was reverted. It caused the DVP driver to run out of queued buffers and abort in `esp_cache_msync()` with a null pointer.
- Two consecutive Vision entries completed face detection without Guru Meditation or WDT. First detections observed at 131 ms and 71 ms.
- MFN load is blocked unless the largest compatible PSRAM block is at least 900 KiB.
- Vision UI mojibake, right-edge spacing, enrollment placeholder, and text vertical alignment were corrected.
- Weather backgrounds now decode the selected sunny/cloudy/rain/night JPEG into one shared 750 KiB PSRAM buffer. Night mode was verified on-device with `Decoded ui_img_weather_night`.
- Voice, Synth, LVGL, Wi-Fi, and weather refresh remained active during the latest hardware checks.

## Current PSRAM observations

| Stage | Free PSRAM | Largest block | SIMD largest |
| --- | ---: | ---: | ---: |
| Boot/UI ready | 4,638,464 B | 4,587,520 B | 4,587,520 B |
| Before first Vision start | 4,636,588 B | 4,587,520 B | 4,587,520 B |
| After Camera start (two buffers) | 942,384 B | 917,504 B | 917,504 B |
| After detector has been retained | about 654 KiB free | 606,208 B | 606,208 B |

MFN requires about 820.75 KiB of contiguous PSRAM. The empty-database crash is fixed, but MFN memory feasibility has **not** passed.

## Reproducible third-party fixes

`tools/apply_managed_component_patches.py` is invoked by the project CMake file after dependency resolution. It applies and validates:

1. Null model-input guards in both ESP-DL `ImagePreprocessor` constructors.
2. Constant `HumanFaceFeat::get_feat_len() == 512`, preventing a database-count query from waking the lazy MFN model.

The script is idempotent and fails on unexpected upstream source instead of applying an unsafe fuzzy patch.

## Next gate

Do not force MFN while the largest compatible block is below 900 KiB. The next minimal experiment is:

1. For an existing database, prepare/load the recognizer before Camera allocation.
2. For first enrollment, stop/deinitialize Camera, load MFN, then restart Camera.
3. Re-measure PSRAM before and after MFN load.
4. Only after MFN Memory Gate passes, validate one-person and three-person enrollment, reboot recovery, cancel rollback, delete, re-enroll, and 20 Vision enter/exit cycles.

Phase 7 object detection remains out of scope until Phase 5/6 gates pass.
