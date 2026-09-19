#!/usr/bin/env python3
"""Fail if drawer Wi-Fi/BT cards lack native enabled, still bind color, or have broken hierarchy."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "scenes"


def main():
    checked = 0
    for path in sorted(ROOT.glob("korvo_*_800.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        objs = data["objects"]
        objs_by_name = {o.get("name"): o for o in objs if o.get("name")}
        if "wifi_card" not in objs_by_name:
            continue
        checked += 1
        for toggle in ("wifi_enabled", "bluetooth_enabled"):
            assert objs_by_name[toggle].get("callback"), f"{path.name} {toggle} missing callback"
        for card in ("wifi_card", "bluetooth_card"):
            assert objs_by_name[card].get("enabled") is False, f"{path.name} {card} must start disabled"
            assert "bind" not in objs_by_name[card], f"{path.name} {card} must not bind color"

        # Validate drawer hierarchy
        drawer_idx = next(i for i, o in enumerate(objs) if o.get("name") == "quick_settings_drawer")
        assert objs[drawer_idx].get("parent") == -1, f"{path.name} drawer must have parent -1"

        drawer_children = [i for i, o in enumerate(objs) if o.get("parent") == drawer_idx]
        assert len(drawer_children) >= 2, (
            f"{path.name} drawer at index {drawer_idx} must have at least 2 children (rect and container), got {drawer_children}"
        )

        rect_idx = drawer_children[0]
        container_idx = drawer_children[1]
        assert objs[rect_idx].get("type") == "rect", f"{path.name} expected rect child at {rect_idx}"
        assert objs[container_idx].get("type") == "container", f"{path.name} expected container child at {container_idx}"

        for widget_name in ("wifi_enabled", "wifi_card", "bluetooth_enabled", "bluetooth_card", "brightness", "volume"):
            widget = objs_by_name[widget_name]
            assert widget.get("parent") == container_idx, (
                f"{path.name} widget {widget_name} has parent {widget.get('parent')}, expected {container_idx}"
            )

    assert checked == 9, checked
    print("wifi_bt_drawer ok", checked)


if __name__ == "__main__":
    main()
