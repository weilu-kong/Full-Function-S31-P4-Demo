#!/usr/bin/env python3
"""Fail if drawer Wi-Fi/BT cards lack native enabled and still bind color."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "scenes"


def main():
    checked = 0
    for path in sorted(ROOT.glob("korvo_*_800.json")):
        data = __import__("json").loads(path.read_text())
        objs = {o.get("name"): o for o in data["objects"] if o.get("name")}
        if "wifi_card" not in objs:
            continue
        checked += 1
        for toggle in ("wifi_enabled", "bluetooth_enabled"):
            assert objs[toggle].get("callback"), f"{path.name} {toggle} missing callback"
        for card in ("wifi_card", "bluetooth_card"):
            assert objs[card].get("enabled") is False, f"{path.name} {card} must start disabled"
            assert "bind" not in objs[card], f"{path.name} {card} must not bind color"
    assert checked == 9, checked
    print("wifi_bt_drawer ok", checked)


if __name__ == "__main__":
    main()
