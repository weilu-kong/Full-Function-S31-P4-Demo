# Synth/A2DP repair implementation plan

Goal: implement the supplied audio repair on codex/vision-ai, based on 173ca83.
Architecture: synth-only EQ/reverb; complete-frame Bluetooth byte FIFO with negotiated-rate software ASRC; queued UI SFX; saturating final mix at 44100 Hz, fed unchanged to Voice/AEC before codec output.
Tech stack: existing ESP-IDF 6.1, FreeRTOS, esp_asrc 1.1.0, LVGL 9.

- [x] Verify local branch, clean worktree, GitHub SHA, and serial port.
- [x] Reproduce sample-zero voice death with a host test compiled from the actual renderer.
- [x] Fix attack and implement nonblocking queued note on/off with 40 ms release.
- [x] Remove postmix synth FX and squared mixer attenuation; test unity BT and saturation.
- [x] Assemble byte-buffer fragments with retained partial input and resampler output; use 40 KiB PSRAM and 30 ms prebuffer, bounded timeout, counted drops.
- [x] Decode SBC configuration, use SW_SPEED ASRC without claiming Voice hardware streams; retain 44100 Hz output.
- [x] Add 2 second cumulative telemetry, codec write timing/errors, and queue drop counters.
- [x] Add reusable click registration to requested UI controls, excluding piano keys.
- [x] Run existing host regressions and full firmware build/size; review final diff.
- [x] Flash at 920160 and collect boot logs. 90-second boot capture complete; no phone stream observed. Ten-minute playback and listening/Voice/Vision operator tests remain unperformed and are explicitly documented.
- [x] Record root causes, changed files, pipelines, measurements/limitations, and final commit.
