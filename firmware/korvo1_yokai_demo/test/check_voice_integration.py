#!/usr/bin/env python3
"""Source guards for voice command routing and single-owner audio output."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
ui = (root / "main/ui/ui.c").read_text()
drawer_h = (root / "main/ui/ui_drawer.h").read_text()
synth = (root / "main/synth_service.c").read_text()
voice = (root / "main/voice_service.c").read_text()

for token in (
    "VOICE_TARGET_SYNTH", "VOICE_TARGET_WEATHER", "VOICE_TARGET_VOICE",
    "VOICE_TARGET_VISION", "VOICE_TARGET_FIREWORKS", "VOICE_TARGET_CLOCK",
    "VOICE_TARGET_CALCULATOR", "VOICE_TARGET_FOOD", "VOICE_TARGET_WIFI",
    "VOICE_TARGET_BLUETOOTH", "VOICE_TARGET_HOME", "VOICE_TARGET_VOLUME",
):
    assert token in ui, token

assert "ui_drawer_set_volume" in drawer_h
assert "voice_service_receive" in ui
assert "voice_service_set_mode" in ui
assert "esp_codec_dev_write" not in ui
assert "esp_codec_dev_write" in synth
assert 'afe_config_init("MR"' in voice
assert ".channel = 1" in voice
assert "esp_asrc_get_out_sample_num" in voice
assert "mic_frames = s_feed.mic_out_bytes / sizeof(*s_feed.mic_16k)" in voice
assert "ref_frames = s_feed.ref_out_bytes / sizeof(*s_feed.ref_16k)" in voice
assert "while (s_ref_count < target)" in voice
assert "while (s_ref_count > target)" in voice
assert "allocate_feed_buffers" in voice
assert "vTaskDelete(s_feed_task)" in voice
assert "consecutive_failures" in voice
print("Voice UI integration checks passed.")
