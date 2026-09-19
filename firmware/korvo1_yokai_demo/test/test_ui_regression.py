#!/usr/bin/env python3
"""Small source-level guards for the Weather and Wi-Fi UI contract."""
from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]
ui = (root / "main" / "board_ui.c").read_text()
wifi = (root / "scenes" / "korvo_wifi_800.json").read_text()
weather = (root / "scenes" / "korvo_weather_800.json").read_text()
home = (root / "scenes" / "korvo_home_800.json").read_text()
partitions = (root / "partitions.csv").read_text()
weather_scene = json.loads(weather)
weather_objects = weather_scene["objects"]

assert '" *"' not in ui
assert 'esp_wifi_connect();' in ui
assert 'wifi_reconnect_saved' in ui
assert 'wifi_forget_saved' in ui
assert '"name": "wifi_lock_0"' in wifi
assert '[LOCK]' not in ui
assert '[設定済み]' in ui
assert '"font_size": 22' in wifi
assert '"w": 780' in wifi and '"h": 364' in wifi
assert 'wifi_saved_drawer' in wifi
assert '"x": 24' in weather and '"callback": "weather_refresh"' in weather
assert '"text": "更新"' not in weather
assert '"bind": "weather_clock_text"' in weather
assert 'GSP_KORVO_WEATHER_BIND_WEATHER_CLOCK_TEXT' in ui
assert weather_objects[11]["image"] == "../assets/images/yokai_weather_day_800.png"
assert weather_objects[12]["name"] == "weather_bg_night"
assert weather_objects[12]["visible"] is False
assert weather_objects[13]["x"] == 145
assert weather_objects[14]["text"] == "<"
assert weather_objects[14]["font_size"] == 24
assert weather_objects[15]["name"] == "weather_title_label"
assert not any(obj.get("name") == "weather_header" for obj in weather_objects)
assert not any(obj.get("name") == "weather_wifi_state" for obj in weather_objects)
assert 'esp_gsp_set_visible(ui, GSP_KORVO_WEATHER_BIND_WEATHER_BG_NIGHT' in ui
assert 'esp_gsp_set_image_borrowed' not in ui
assert 'static bool s_weather_status_dirty' in ui
assert '"text": "このネットワークを忘れる"' in wifi
assert weather_objects[23]["name"] == "quick_settings_drawer"
assert weather_objects[24]["parent"] == 23
assert weather_objects[25]["parent"] == 23
assert weather_objects[26]["parent"] == 25
assert 'set_wifi_lock_visibility' in ui
assert 's_wifi_enabled = true;' in ui
assert 'esp_gsp_page_flow_get_page' in ui
assert 'Home PageFlow page=%u dragging=%d' in ui
assert json.loads(home)['swipe'] is False
assert 'static bool s_wifi_toggle_sync_pending' in ui
assert 'esp_gsp_component_set_checked(' in ui
assert 'GSP_OBJ_KEY_WIFI_ENABLED, s_wifi_enabled' in ui
assert 'if (s_wifi_toggle_sync_pending)' in ui
assert 'factory,  app,  factory, 0x10000,  9M,' in partitions
assert 'model,    data, spiffs,  0x910000, 6M,' in partitions
assert 'storage,  data, spiffs,  0xf10000, 960K,' in partitions
print("UI regression checks passed")
