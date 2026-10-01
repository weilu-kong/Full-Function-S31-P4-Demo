# Vision AI 5-Sample Face Enrollment UX & Reliability — 2026-09-24

## 1. Scope & Execution Context
- **Repository**: `weilu-kong/Full-Function-S31-P4-Demo`
- **Branch**: `codex/vision-ai`
- **Hardware**: ESP32-S31-Korvo-1 (16MB Octal Flash, 16MB Octal PSRAM, DVP SC101IOT Camera 1280×720 UYVY, 800×480 RGB LCD)
- **Flashing Baud Rate**: `920160` (Serial Port: `/dev/cu.usbserial-1120`)

---

## 2. Feature Implementation Summary

### 1) 5-Sample Guided Enrollment Pipeline (`vision_service.cpp` & `vision_service.h`)
- **Multi-angle Capture Sequence**:
  - Sample 1: Front Face (`正面を向いてください`)
  - Sample 2: Slightly Left (`少し左を向いてください`)
  - Sample 3: Slightly Right (`少し右を向いてください`)
  - Sample 4: Front Reset (`正面を向いてください`)
  - Sample 5: Front Verification & Commit (`正面を向いてください`)
- **Pose Angle Gating (Landmark Yaw)**:
  $$\text{yaw} = \frac{\text{nose}_x - \frac{\text{left\_eye}_x + \text{right\_eye}_x}{2}}{\text{right\_eye}_x - \text{left\_eye}_x}$$
  - Calibrated tolerance: $|\text{yaw}| \le 0.85$ across all enrollment steps to eliminate sensor landmark noise deadlock while rejecting extreme angles.
- **Stability Temporal Accumulator**:
  - Requires continuous face presence within central safe bounds ($64 \le c_x \le 256$, $48 \le c_y \le 192$, size $\ge 60\times 60$, score $\ge 0.50$) for $\ge 500\text{ ms}$ and $\ge 4\text{ frames}$ (`ENROLL_STABLE_MIN_MS=500`, `ENROLL_STABLE_MIN_FRAMES=4`).
- **Inter-Step Transition Grace**:
  - `ENROLL_STEP_GRACE_MS = 700ms`: When transitioning between steps or recovering from a transient error, the state machine holds in `WAITING` for 700ms with counters zeroed, giving the operator sufficient time to reposition their head before stability counting begins.
- **Atomic Journal Rollback**:
  - In-flight enrolled feature IDs are tracked in `/private/tmp/vision_enroll.jnl` (`/spiffs/vision_enroll.jnl` on target). If cancelled or aborted mid-sequence, partial embeddings are purged via `s_face_recognizer->delete_feat(feat_id)` preventing DB pollution.

### 2) Standardized Error Codes & Telemetry
- Detection & Pose: `1001-1007` (`NO_FACE`, `MULTIPLE_FACES`, `TOO_SMALL`, `OFF_CENTER`, `LOW_SCORE`, `WRONG_POSE`, `UNSTABLE`)
- Inference & Extraction: `2001-2003` (`MFN_NO_MEMORY`, `EXTRACT_FAILED`, `ID_INVALID`)
- Storage & DB: `3001-3002` (`METADATA_SAVE_FAILED`, `FACE_DB_FAILED`)
- User / API: `4001-4006` (`EMPTY_NAME`, `DUPLICATE_NAME`, `MAX_PERSONS`, `TIMEOUT`, `INVALID_SLOT`, `CANCELLED`)

---

## 3. Key Bugfixes & Diagnostic Details

### Issue A: 4/5 Stuck in `E1001` Latch Deadlock
- **Symptom**: Step 4/5 consistently failed, logging `[ENROLL_GATE] step=4 fail=E1001` indefinitely even when facing directly into the camera.
- **Root Cause**: During the head movement from Step 3 (Right) back to Step 4 (Front), the face was momentarily undetected, latching `s_enroll_txn.error_code = 1001`. On subsequent frames with a detected face, `s_enroll_txn.error_code` was never cleared back to `NONE`. Line 946 `if (s_enroll_txn.error_code != VISION_ENROLL_ERR_NONE)` remained permanently `true`, locking the state machine in error logging without accumulating stable frames.
- **Fix**: Introduced per-frame baseline `vision_enroll_error_code_t frame_err = VISION_ENROLL_ERR_NONE;` and updated `s_enroll_txn.error_code = frame_err;` on every frame, cleanly unlatching error states as soon as a valid face reappears.

### Issue B: 3/5 Instability (Instant Capture & E1001/E1006 Flapping)
- **Symptom**: Step 3/5 was either instantly captured without moving, or flapped between `E1001` and `E1006`.
- **Root Cause**:
  1. Lack of settling delay allowed Step 3 to trigger immediately while user was still in Step 2 pose.
  2. Overly restrictive yaw boundaries (0.70) rejected natural head rotation angles.
- **Fix**: Added `ENROLL_STEP_GRACE_MS` (700ms), tuned stability to 500ms / 4 frames, and widened yaw tolerance to 0.85.

### Issue C: Japanese Prompt Font Garbling (Missing CJK Glyphs)
- **Symptom**: Certain prompts displayed blank square tofu characters on the screen.
- **Root Cause**: The embedded LVGL font `ui_font_cjk_16.c` omitted characters: `✓`, `第`, `特`, `徴`, `抽`, `試`, `既`, `番`.
- **Fix**: Replaced all prompts with 100% font-safe synonyms (`OK: ステップ %d 完了！`, `認識データ取得エラー`, `同じ名前がすでに登録されています`, `登録が完了しました`). An automated Python audit verified **0 missing glyphs** across the entire codebase.

### Issue D: Text Overlap on Completion (`顔登録が完了しました` & Name)
- **Symptom**: On enrollment completion, the success message and enrolled name overlapped in the HUD box.
- **Root Cause**:
  1. Fixed Y coordinates (`y=24` for feedback, `y=48` for prompt/name) provided only a 24px gap.
  2. The string `"OK: 顔登録が完了しました"` (14 full-width characters, ~196px) wrapped to 2 lines ($y=24\sim 60$), colliding with the name drawn at $y=48$.
  3. The cancel button (`登録中止`) remained visible on completion, compressing vertical space.
- **Fix**:
  - Re-architected `s_box_enroll_hud` with an internal flex column container `s_box_enroll_labels` (`LV_FLEX_FLOW_COLUMN` + `LV_FLEX_ALIGN_SPACE_EVENLY`). LVGL automatically calculates spacing, preventing any overlapping regardless of label text length or line wrapping.
  - On completion: dynamically hide the cancel button, expand container height from 114px to 156px, display the title `顔登録 完了`, highlight the name in `UI_FONT_TITLE`, and show `登録が完了しました` on a single line.

---

## 4. Verification & Health Summary

| Verification Target | Command / Tool | Status | Details |
| :--- | :--- | :--- | :--- |
| **Font Glyph Audit** | Python CJK regex audit against `ui_font_cjk_16.c` | **PASSED** | 0 missing characters across all UI & service files |
| **Host Unit Tests** | `clang++ -std=c++17 test/test_vision_service.c` | **PASSED** | Lifecycle, error strings, yaw calculation, and state machine tests pass |
| **UI Regression** | `python3 test/test_ui_regression.py` | **PASSED** | Tear avoidance, buffer count, and screen linkages intact |
| **ESP-IDF Build** | `idf.py build` (ESP-IDF 6.2.0) | **PASSED** | App size `0x9b10d0` bytes, 16% partition free margin |
| **Hardware Flashing** | `idf.py -p /dev/cu.usbserial-1120 -b 920160 flash` | **PASSED** | `Hash of data verified`, RTS hard reset OK |
| **Live Device Health** | Serial telemetry (`115200` baud) | **HEALTHY** | `psram_free > 5.23 MB`, `cam_err = 0`, `no_free_buf = 0`, `tasks = 21` |
