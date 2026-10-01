# Korvo-1 Wi-Fi / Bluetooth 快捷设置 — 已修复交接

> **状态（2026-09-12 用户真机确认）**：下文三个 UI Bug 已修复，不要当待办重做。本文保留根因、踩坑和不变量，供后续 Agent review / 防回归。入口总览见 [`docs/AGENT-HANDOFF.md`](AGENT-HANDOFF.md)。

| 项 | 说明 |
| --- | --- |
| 工作树 | `/Users/kongweilu/Development/Full Demo/.worktrees/synth-groovebox` |
| 分支 | `feature/synth-groovebox` |
| 硬件 | ESP32-S31-Korvo-1，800×480，GT1151 |
| 烧录 | `/dev/cu.usbserial-1140`，460800 |
| IDF | `/Users/kongweilu/esp/esp-idf-master` v6.2.0 |
| 构建目录 | `firmware/korvo1_yokai_demo/build-korvo1-s31-synth` |
| 主文件 | `firmware/korvo1_yokai_demo/main/board_ui.c`、`app_main.c`、9 个带抽屉的 `scenes/korvo_*_800.json` |
| JSON 检查 | `python3 firmware/korvo1_yokai_demo/test/check_wifi_bt_drawer.py`（必须 9 个场景通过） |

构建/烧录命令见 `docs/AGENT-HANDOFF.md`。

---

## 已确认行为

1. Toggle OFF：`wifi_card` / `bluetooth_card` 变暗，触摸打不中（GSP `enabled=false`）。
2. Toggle ON：卡片变亮，点击进入 `korvo_wifi` / `korvo_bluetooth`。
3. Wi-Fi 扫描：worker 填 `s_wifi_aps`；UI 线程 `apply_wifi_scan_ui()`。左上角从「確認中」变为 `Wi-Fi N`，SSID 无需拖动即显示。
4. 详情「ホーム」：回到进入前的场景（任意桌面/App），`ESP_GSP_NO_TRANSITION`，抽屉无动画打开，不闪空桌面。

开关仍只改 UI，**未**控制真实 radio。BLE 扫描未做。那是计划 Task 6 剩余项。

---

## UI 结构

抽屉 `quick_settings_drawer`（`GSP_OBJ_KEY_QUICK_SETTINGS_DRAWER`）嵌在 9 个业务场景（home + 8 apps），不含 wifi/bt 详情场景本身。

| name | type | callback |
| --- | --- | --- |
| `wifi_enabled` | toggle | `wifi_toggle` |
| `wifi_card` | button | `wifi_details` |
| `bluetooth_enabled` | toggle | `bluetooth_toggle` |
| `bluetooth_card` | button | `bluetooth_details` |

场景：Wi-Fi = 9 `korvo_wifi_800.json`；Bluetooth = 10 `korvo_bluetooth_800.json`。

卡片 JSON（9 场景一致）：

```json
"enabled": false,
"disabled_opacity": 90,
"disabled_color": "#1A2035"
```

不要再加 `"bind": "...", "bind_target": "color"`。

---

## 实际采用的修复（不要改回旧方案）

### 1. 卡片明暗 + 能否点击

- JSON 原生 disabled 态；C 端 `esp_gsp_component_set_enabled(ui, GSP_OBJ_KEY_WIFI_CARD, s_wifi_enabled)`。
- OFF 时框架阻断 hit-test，C 侧 `is_*_details_event` 仍要求 `s_*_enabled`（双保险）。

### 2. Toggle 与 C 状态同步

GSP toggle **不发 CALL**（callback-only 与 click→call 都验证过 0 事件）。

- `board_ui_start` 里 `esp_gsp_timer_create(ui, 50, toggle_sync_timer_cb, NULL)`。
- `apply_toggle_from_widget`：`get_checked` → 更新 `s_wifi_enabled` / `s_bluetooth_enabled` → `set_enabled` 卡片。
- **禁止** `set_checked`。`event->arg` 不是布尔。

若 CALL 路径偶尔出现，handler 里也用 `get_checked`，不要 `!s_wifi_enabled` 翻转。

### 3. `app_state_t` 生命周期

`app_main` 在 `board_ui_start` 后返回。扫描任务会读 `state`。必须：

```c
static app_state_t state;
```

栈上的 `app_state_t state` 会在返回后变成垃圾。曾经用 `state->screen != WIFI_SETTINGS` 取消扫描，screen 读成 `1075572156`，阻塞扫描永不执行，页面一直停在 JSON 默认「確認中」。

### 4. 扫描与列表

- Worker 只按 `s_wifi_scan_generation` 取消，**不**看 `state->screen`。
- Worker **不**调 GSP。写 `s_wifi_aps`、`s_wifi_ap_count`、`s_wifi_results_dirty`。
- UI：`apply_wifi_scan_ui()` 仅当 `s_live_scene == KORVO_WIFI`。
- List bind **一次**。`CONFIG_ESP_GSP_MAX_LISTS` 默认 5；每次 SCENE_CHANGED 再 bind 会耗尽配额。
- 刷新可见行：`set_total(0)` → `set_total(ap_count)` → `refresh`。总数已是 10（与 JSON 占位相同）时只 `refresh` 不会重绑视口内的行，SSID 要拖才出。真机日志：`list_applied` 后约 55ms 即对 index 0/1/2 调用 `bind_cb`。

STA 阻塞扫描参考：[Station 场景 — 扫描](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s31/api-guides/wifi-driver/station-scenarios.html#id14)。

### 5. 详情「ホーム」

- `remember_settings_return(event->scene_id)` 记下父场景与对应 `APP_EVENT_*`。
- `s_reopen_drawer = true`。
- `board_ui_open_scene`：不关抽屉；`ESP_GSP_NO_TRANSITION`（不要 `FADE_THROUGH_BLACK`，此前 home_click → SCENE_CHANGED ≈ 463ms 并闪空桌面）。
- `SCENE_CHANGED`：`esp_gsp_drawer_open(..., false)`。真机 home_click → SCENE_CHANGED ≈ 43ms。

---

## 禁止再踩的坑

| # | 不要做 | 后果 |
| --- | --- | --- |
| 1 | `component_set_color(..., 0x002563EB)` | RGB565 管线拒绝 24-bit |
| 2 | 卡片 `bind_target: "color"` | 只改填充，`border_color` `#3B82F6` 仍常亮 |
| 3 | 仅靠 `s_wifi_enabled` 门禁、用 `!` 翻转、信 `event->arg` | C 状态与滑块脱节，ON 也进不了详情 |
| 4 | `set_checked` | 二次 callback，滑块在左槽仍绿 |
| 5 | 栈上 `app_state_t` + 用 `state->screen` 取消扫描 | 扫描被误杀，永远「確認中」 |
| 6 | 从 scan task 调 GSP bind/refresh | 与渲染并发；且 list handle 易过期 |
| 7 | 每次 SCENE_CHANGED 再 `list_bind_component` | 耗尽 list 配额 |
| 8 | `set_total(10)` 当总数已是 10 | 视口行不重绑，白线，要拖才出 SSID |
| 9 | ホーム `FADE_THROUGH_BLACK` + 动画 `drawer_open` | 先闪空桌面再弹出抽屉 |

---

## 代码锚点（`board_ui.c`）

- `s_wifi_enabled` / `s_bluetooth_enabled`
- `apply_toggle_from_widget` / `toggle_sync_timer_cb`（50ms）
- `sync_wifi_controls` / `sync_bluetooth_controls` → `set_enabled` only
- `wifi_scan_task` / `wifi_scan_async` / `apply_wifi_scan_ui` / `s_wifi_results_dirty`
- `remember_settings_return` / `s_reopen_drawer` / `board_ui_open_scene`
- `app_main.c`：`static app_state_t state`

生成头：`build-korvo1-s31-synth/esp-idf/main/gsp_gen_bundle/bundle_gsp.h`。

---

## Task 6 还没做的

- 开关驱动真实 `esp_wifi_start/stop` 与 BT radio，并跨场景同步。
- BLE 扫描/连接替换蓝牙页静态列表。
- 非 UTF-8 SSID 显示策略。
- 实体 Home 仍应始终回桌面第一页（与详情「ホーム」回上一层抽屉不是同一条路径）。
