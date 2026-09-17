#!/usr/bin/env python3
"""Convert 800x480 background images to JPEG byte arrays in C for hardware decompression."""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS_DIR = ROOT / "assets/images"
UI_DIR = ROOT / "main/ui"

IMAGES = [
    ("ui_img_home_p1", ASSETS_DIR / "yokai_home_p1_800.png", UI_DIR / "ui_img_home_p1.c"),
    ("ui_img_home_p2", ASSETS_DIR / "yokai_home_p2_800.png", UI_DIR / "ui_img_home_p2.c"),
    ("ui_img_weather_sunny", ASSETS_DIR / "yokai_weather_sunny_800.png", UI_DIR / "ui_img_weather_sunny.c"),
    ("ui_img_weather_cloudy", ASSETS_DIR / "yokai_weather_cloudy_800.png", UI_DIR / "ui_img_weather_cloudy.c"),
    ("ui_img_weather_rain", ASSETS_DIR / "yokai_weather_rain_800.png", UI_DIR / "ui_img_weather_rain.c"),
    ("ui_img_weather_night", ASSETS_DIR / "yokai_weather_night_800.png", UI_DIR / "ui_img_weather_night.c"),
]

def compress_and_generate(var_name: str, src_png: Path, dst_c: Path, quality: int = 80):
    if not src_png.exists():
        print(f"Error: source image not found: {src_png}", file=sys.stderr)
        sys.exit(1)
        
    tmp_jpg = Path(f"/tmp/{var_name}.jpg")
    cmd = ["sips", "-s", "format", "jpeg", "-s", "formatOptions", str(quality), str(src_png), "-o", str(tmp_jpg)]
    subprocess.check_call(cmd, stdout=subprocess.DEVNULL)
    
    jpg_bytes = tmp_jpg.read_bytes()
    tmp_jpg.unlink()
    
    lines = []
    lines.append('#include "lvgl.h"')
    lines.append('#include <stddef.h>')
    lines.append('#include <stdint.h>')
    lines.append('')
    lines.append(f'/* JPEG compressed 800x480 background ({len(jpg_bytes):,} bytes, quality {quality}) */')
    lines.append(f'const uint8_t {var_name}_jpg[] = {{')
    
    # 16 bytes per line
    for i in range(0, len(jpg_bytes), 16):
        chunk = jpg_bytes[i:i+16]
        hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_str},")
        
    lines.append('};')
    lines.append(f'const size_t {var_name}_jpg_len = sizeof({var_name}_jpg);')
    lines.append('')
    lines.append(f'lv_image_dsc_t {var_name} = {{')
    lines.append('    .header = {')
    lines.append('        .magic = LV_IMAGE_HEADER_MAGIC,')
    lines.append('        .cf = LV_COLOR_FORMAT_RGB565,')
    lines.append('        .w = 800,')
    lines.append('        .h = 480,')
    lines.append('        .stride = 1600,')
    lines.append('    },')
    lines.append('    .data_size = 800 * 480 * 2,')
    lines.append('    .data = NULL,')
    lines.append('};')
    lines.append('')
    
    dst_c.write_text("\n".join(lines))
    print(f"Generated {dst_c.name}: {len(jpg_bytes):,} bytes (saved {768000 - len(jpg_bytes):,} bytes)")

def main():
    total_jpg = 0
    total_raw = 6 * 768000
    for var_name, src_png, dst_c in IMAGES:
        compress_and_generate(var_name, src_png, dst_c, quality=80)
        
    for _, _, dst_c in IMAGES:
        # read the size
        text = dst_c.read_text()
        count = text.count("0x")
        total_jpg += count
        
    print(f"\nTotal Flash consumption for 6 images:")
    print(f"  Before: {total_raw:,} bytes ({total_raw/1024/1024:.2f} MB)")
    print(f"  After:  {total_jpg:,} bytes ({total_jpg/1024/1024:.2f} MB)")
    print(f"  Saved:  {total_raw - total_jpg:,} bytes ({(total_raw - total_jpg)/1024/1024:.2f} MB)")

if __name__ == "__main__":
    main()
