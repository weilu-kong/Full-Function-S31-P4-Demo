"""Check real generated glyph maps cover static non-ASCII firmware text."""
from pathlib import Path
import re

main = Path(__file__).resolve().parents[1] / "main"
required = set()
for path in main.rglob("*"):
    if path.suffix not in (".c", ".cpp", ".h") or path.name.startswith(("ui_font_", "ui_img_")):
        continue
    for literal in re.findall(r'"(?:[^"\\]|\\.)*"', path.read_text()):
        required.update(ord(ch) for ch in literal if ord(ch) > 127)

for size in (14, 16, 20, 32):
    source = (main / "ui" / f"ui_font_cjk_{size}.c").read_text()
    arrays = {name: [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", values)]
              for name, values in re.findall(r"static const uint(?:8|16)_t (\w+)\[\] = \{(.*?)\};", source, re.S)}
    maps = re.search(r"cmaps\[\] =\s*\{(.*?)\n\};", source, re.S).group(1)
    supported = set()
    for entry in re.findall(r"\{(.*?)\}", maps, re.S):
        start = int(re.search(r"\.range_start = (\d+)", entry).group(1))
        length = int(re.search(r"\.range_length = (\d+)", entry).group(1))
        sparse = re.search(r"\.unicode_list = (\w+)", entry).group(1)
        offsets = re.search(r"\.glyph_id_ofs_list = (\w+)", entry).group(1)
        if sparse != "NULL":
            supported.update(start + offset for offset in arrays[sparse])
        elif offsets != "NULL":
            supported.update(start + i for i, offset in enumerate(arrays[offsets]) if i == 0 or offset)
        else:
            supported.update(range(start, start + length))
    missing = required - supported
    assert not missing, f"{size}px missing glyphs: {''.join(map(chr, sorted(missing)))}"
print("All four CJK font maps cover static firmware text")
