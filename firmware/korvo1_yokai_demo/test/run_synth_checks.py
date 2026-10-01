#!/usr/bin/env python3
"""Compile the actual firmware DSP functions into the existing host checks."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "main/synth_service.c").read_text()
api = (root / "main/synth_service.h").read_text()
with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    declarations = "#include <stdlib.h>\n"
    declarations += "\n".join(re.findall(r"^#define SYNTH_.*", source, re.M)) + "\n"
    declarations += re.search(r"typedef enum \{.*?\} synth_wave_t;", api, re.S)[0]
    declarations += re.search(r"typedef struct \{.*?\} synth_voice_t;", source, re.S)[0]
    start = source.index("static inline float synth_render_sample(")
    end = source.index("\n}\n", start) + 3
    declarations += source[start:end]
    for signature in ("static inline float audio_limiter_gain(", "static inline int16_t audio_mix_sample("):
        start = source.index(signature)
        end = source.index("\n}\n", start) + 3
        declarations += source[start:end]
    (directory / "synth_render_under_test.h").write_text(declarations)
    binary = directory / "synth_checks"
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-DSYNTH_RENDER_TEST",
                    "-I", tmp, str(root / "test/test_synth_math.c"), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

    declarations = "\n".join(re.findall(r"^#define (?:SYNTH_|BT_).*", source, re.M)) + "\n"
    start = source.index("static esp_asrc_handle_t s_bt_asrc;")
    end = source.index("static esp_bd_addr_t", start)
    declarations += source[start:end]
    declarations += re.search(r"^static volatile bool s_bt_enabled.*", source, re.M)[0] + "\n"
    declarations += "static int16_t s_bt_buf[SYNTH_CHUNK_SAMPLES * SYNTH_CHANNELS];\n"
    for signature in ("static uint32_t bt_fifo_fill(void)\n{", "static void bt_a2dp_data_cb(const uint8_t *data, uint32_t len)\n{", "static void bt_reset_consumer(",
                      "static bool bt_epoch_matches(", "static bool bt_render_chunk("):
        start = source.index(signature)
        end = source.index("\n}\n", start) + 3
        declarations += source[start:end]
    (directory / "bt_audio_under_test.h").write_text(declarations)
    binary = directory / "bt_checks"
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-I", tmp,
                    str(root / "test/test_bt_audio.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
