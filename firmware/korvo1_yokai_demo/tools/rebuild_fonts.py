#!/usr/bin/env python3
import glob
import re
import subprocess
import os

# 1. Read existing symbols from ui_font_cjk_20.c
with open("main/ui/ui_font_cjk_20.c", "r", encoding="utf-8") as f:
    for line in f:
        if "--symbols" in line:
            m = re.search(r"--symbols (.*?) --size", line)
            if m:
                existing_symbols = set(m.group(1))
                break

# 2. Collect all characters in codebase
codebase_chars = set()
for filepath in glob.glob("main/**/*.c", recursive=True) + glob.glob("main/**/*.h", recursive=True) + glob.glob("main/**/*.cpp", recursive=True):
    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        for ch in f.read():
            if ord(ch) > 127:
                codebase_chars.add(ch)

# 3. Explicitly required characters for all 3 apps and UI
extra_chars = set(
    "日月火水木金土曜日"
    "夜空の花火打上数発菊牡丹柳AUTOタップで打ち上げよう"
    "和風そろばん計算履歴を消去エラー全消去±×÷−＋＝"
    "狸屋の時計タイマーカウントダウン開始一時停止再開時間です停止取消ラップ記録押すとここにされます時刻同期済み時刻未設定東京晴れ曇雨雪最終同期"
    "屋台の食材棚新鮮消費期限追加削除確認保存"
    "御用でしょうかもう一度音量戻る設定"
    "°…※℃←↑→↓■□▲▶▼◀○●★☆♩♫✕　、。〃々〆〇「」『』【】〜・ー"
    "×÷±≠≈≤≥∞∑√∫"
)

all_symbols = "".join(sorted(list(existing_symbols | codebase_chars | extra_chars)))
print(f"Total unified symbols to generate: {len(all_symbols)}")

font_sizes = [
    (14, "ui_font_cjk_14"),
    (16, "ui_font_cjk_16"),
    (20, "ui_font_cjk_20"),
    (32, "ui_font_cjk_32"),
]

for size, font_name in font_sizes:
    out_file = f"main/ui/{font_name}.c"
    print(f"Generating {out_file} (size {size}px)...")
    cmd = [
        "npx", "-y", "lv_font_conv",
        "--font", "assets/fonts/NotoSansCJKjp-Regular.otf",
        "-r", "0x20-0x7F",
        "--symbols", all_symbols,
        "--size", str(size),
        "--format", "lvgl",
        "--bpp", "2",
        "--no-compress",
        "--lv-font-name", font_name,
        "-o", out_file
    ]
    subprocess.check_call(cmd)

print("All CJK fonts successfully rebuilt!")
