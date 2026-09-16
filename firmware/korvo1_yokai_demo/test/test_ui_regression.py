#!/usr/bin/env python3
"""Source-level guards for the active LVGL 9 Weather and Wi-Fi UI."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
board = (root / "main/board_ui.c").read_text()
ui = (root / "main/ui/ui.c").read_text()
wifi = (root / "main/ui/ui_wifi.c").read_text()
weather = (root / "main/ui/ui_weather.c").read_text()
home = (root / "main/ui/ui_home.c").read_text()
partitions = (root / "partitions.csv").read_text()

assert "ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_FULL" in board
assert "lv_timer_create(ui_lv_timer_cb, 16" in board
assert "esp_wifi_connect();" in board
assert "board_ui_wifi_reconnect_saved" in board
assert "board_ui_wifi_forget_saved" in board

assert "ui_wifi_screen_create" in wifi
assert "ui_img_key_icon" in wifi
assert "[設定済み]" in wifi
assert "設定を削除" in wifi
assert "board_ui_wifi_is_saved" in wifi

assert "ui_weather_screen_create" in weather
assert "ui_weather_screen_update" in weather
assert "weather_service_trigger_refresh" in weather
assert "ui_home_screen_create" in home
assert "ui_drawer_create" in ui
assert "ui_switch_screen" in ui

assert "factory,  app,  factory, 0x10000,  9M," in partitions
assert "model,    data, spiffs,  0x910000, 6M," in partitions
assert "storage,  data, spiffs,  0xf10000, 960K," in partitions
print("LVGL UI regression checks passed")
