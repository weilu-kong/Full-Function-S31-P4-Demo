# Vision startup scheduling

Goal: open Vision during Weather HTTPS without a manual Home/re-entry or RTC stack fallback.

The user authorized autonomous improvements in the existing roadmap. Reuse the UI periodic tick: mark entry pending, wait while the thread-safe weather snapshot reports refreshing, then invoke the existing Vision start. Retry only ESP_ERR_NO_MEM, at most every 500 ms and within a 30-second total deadline. Preserve real errors and cancel on navigation. No task, queue, image buffer, stack enlargement or permanent SRAM reservation is added.

Alternatives considered: permanently reserve the inference stack (costs idle SRAM and shifts TLS pressure), or add a dedicated startup worker (extra stack/lifecycle). The existing tick needs only three small pending/timing fields. Memory allocation remains authoritative if a weather refresh races the snapshot.

- [x] Add a behavioral host harness using the actual UI scheduler functions; verify busy→ready, cancellation, retry rate, timeout/rollover, and non-memory failure. Observe failure before implementation.
- [x] Implement scheduling in ui.c, reuse one error display in ui_apps.[ch], return ESP_ERR_NO_MEM for task allocation failure after cleanup in vision_service.cpp.
- [x] Run unified host checks, native build, Flash gate and independent review; flash a verified production image and collect bounded Home logs. Original rollback is preserved.
- [ ] Verify early-boot automatic Vision entry and repeated Home/Vision transitions; the first 180-second capture remained Home, so this awaits device feedback.
- [x] Update README/report with measured results and remaining mixed-load/CPU/Food acceptance. Integration remains on the existing branch and PR.

Review found enrollment could be accepted during the new waiting window and discarded by a failed-start cleanup. A production-function harness reproduced OFF-state queue acceptance before the fix. Begin/cancel/re-register now require RUNNING like delete/clear; the UI retains input and reports not-ready. Both behavior checks, the full host gate, production build and independent re-review passed. Home/HTTPS validation passed; Vision entry validation is pending.

Production image: 10,574,736 bytes (400 bytes above the prior stack fix); application partition reserve 1,483,888 bytes. Pending/timing state is 12 bytes including alignment in the linker map; no new stack/task/queue/image buffer. SHA256: be185e0f7b093b028bc1832b97585f20f34fd63b8d463a6369a629d96a709fe1.
