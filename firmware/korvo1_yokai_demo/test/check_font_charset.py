#!/usr/bin/env python3
"""Verify that font_charset is 100% identical and in the same order across all scene JSON files."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "scenes"


def main():
    scene_files = sorted(ROOT.glob("korvo_*_800.json"))
    assert len(scene_files) >= 11, f"Expected at least 11 scenes, found {len(scene_files)}"

    first_charset = None
    first_path = None

    for path in scene_files:
        data = json.loads(path.read_text(encoding="utf-8"))
        charset = data.get("font_charset")
        assert charset, f"{path.name} missing font_charset"

        if first_charset is None:
            first_charset = charset
            first_path = path.name
        else:
            assert charset == first_charset, (
                f"Charset mismatch between {first_path} and {path.name}!\n"
                f"Len {first_path}: {len(first_charset)}, Len {path.name}: {len(charset)}"
            )

    print(f"font_charset ok {len(scene_files)} scenes (len={len(first_charset)})")


if __name__ == "__main__":
    main()
