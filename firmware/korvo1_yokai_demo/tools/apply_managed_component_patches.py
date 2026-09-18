#!/usr/bin/env python3
"""Apply the two required ESP-DL crash fixes after dependency resolution."""

import argparse
from pathlib import Path


PATCHES = (
    (
        "managed_components/espressif__esp-dl/vision/image/dl_image_preprocessor.cpp",
        "    m_model_input = model->get_input(input_name);\n    assert(m_model_input->dtype",
        "    m_model_input = model->get_input(input_name);\n"
        "    if (!m_model_input) {\n"
        "        ESP_LOGE(\"ImagePreprocessor\", \"Failed to get model input tensor '%s'\", input_name.c_str());\n"
        "        return;\n"
        "    }\n"
        "    assert(m_model_input->dtype",
        2,
    ),
    (
        "managed_components/espressif__human_face_recognition/human_face_recognition.hpp",
        "                  bool lazy_load = true);\n\nprivate:",
        "                  bool lazy_load = true);\n"
        "    int get_feat_len() override { return 512; }\n\n"
        "private:",
        1,
    ),
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    for relative, before, after, expected_count in PATCHES:
        path = args.project_root / relative
        if not path.is_file():
            raise SystemExit(f"missing managed component source: {path}")
        text = path.read_text()
        if text.count(after) == expected_count:
            continue
        if args.check:
            raise SystemExit(f"required patch is not applied: {relative}")
        if text.count(before) != expected_count:
            raise SystemExit(f"unexpected upstream source; cannot patch safely: {relative}")
        path.write_text(text.replace(before, after))
        print(f"patched {relative}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
