# Full-Function-S31-P4-Demo 开发交接

更新时间：2026-09-14

给后续 AI Agent 的入口。先读本文件，再按需打开专项文档。不要从过期的「待修 Bug」段落开始重做已经真机确认的工作。

| 项 | 值 |
| --- | --- |
| 硬件 | ESP32-S31-Korvo-1，800×480 RGB LCD，GT1151 触摸 |
| 分支 | `feature/synth-groovebox`（从 `feature/second-page-apps` 分出） |
| 工作树 | `/Users/kongweilu/Development/Full Demo/.worktrees/synth-groovebox` |
| ESP-IDF | `/Users/kongweilu/esp/esp-idf-master`，实测 v6.2.0；不要静默换 SDK |
| 构建目录 | `firmware/korvo1_yokai_demo/build-korvo1-s31-synth` |
| 烧录口 | `/dev/cu.usbserial-1140`（重插可能变号），460800 |
| UI | ESP-GSP 场景 JSON + `board_ui.c` 事件路由 |

## 接手时先做什么

1. 读本文件、`docs/HANDOVER_KORVO1_WIFI_BT_UI_BUG.md`（已修复，是避坑与不变量，不是待办）、`docs/superpowers/plans/2026-09-10-korvo1-next-development.md`。
2. `git pull origin feature/synth-groovebox`。不要从旧 `main` 猜当前状态。
3. 在固件目录跑下方「复现检查」，再改代码。
4. 产品/硬件选择（语音命令词、天气服务、P4X 规格、声道映射）问用户，不要自行拍板。
5. 不要每次自动 push；本次用户已要求同步 GitHub。

## 产品目标（未变）

set-target 适配 ESP32-S31-Korvo-1、ESP32-S31 Mosaico、ESP32-P4X-Function-EV Board 的客户演示。优先把 Korvo-1 做完整。基线：ESP-IDF master + ESP-GSP；端侧语音必须含 AEC。

两页桌面：

| 页 | App |
| --- | --- |
| 第一页 | Synthesizer、天气、端侧语音控制、端侧物体识别 |
| 第二页 | 屏幕烟花、时间与计时器、计算器、食材保质期管理器 |

所有桌面和 App 页必须支持顶部下拉快捷设置。页面「ホーム」一键回桌面第一页。待机轮换同世界观图片（尚未进固件）。

视觉与场景语义见 `design/yokai-v1/DESIGN.md`、`APP-SCENES.md`。

## 当前真机已确认

到 2026-09-12，用户在 Korvo-1 上确认：

- 两页四宫格桌面、八个 App 可打开；App 内横滑不串页。
- 任意桌面/App 可下拉快捷设置，面板不点击穿透。
- **快捷设置 Wi-Fi / Bluetooth**
  - 开关 OFF：对应卡片变暗且不可点。
  - 开关 ON：卡片变亮，点击进入详情页。
  - Wi-Fi 详情：扫描完成后左上角从「確認中」变为 AP 数，SSID 列表无需拖动即可显示。
  - 详情页「ホーム」回到**进入前的页面**并立刻打开快捷设置，不闪空桌面。
- Synthesizer：20 键布局、波形/DSP 引擎、A2DP Sink 名 `Yokai-Groovebox` 已接入代码并完成过全量构建；groovebox 发声与混音以固件为准，后续 Agent 接手后应再真机听一次，不要假设未发声。

## 下一步（按用户优先级）

用户要求先做完第一页四个 App。建议顺序：

1. **Task 3 天气**（计划文件中下一未勾项）：复用已有 STA 扫描/初始化，补连接、SNTP、天气请求。城市/数据源/凭据问用户；密钥不得进仓库。
2. Task 4 语音/AEC → Task 5 物体识别。
3. Task 6 剩余：开关控制**真实** Wi-Fi/BT radio、跨场景同步、BLE 扫描/连接。UI 门禁与扫描列表已完成，不要重做。
4. 第二页业务、待机、Mosaico、P4X。

完整任务清单：`docs/superpowers/plans/2026-09-10-korvo1-next-development.md`。

## 硬不变量（改 UI 前必读）

违反任一条都会把已确认的快捷设置打回去。细节与踩坑见 `docs/HANDOVER_KORVO1_WIFI_BT_UI_BUG.md`。

1. **不要**对 toggle 调用 `esp_gsp_component_set_checked`。会再触发 callback，开关抖动。
2. GSP toggle **不发** `ESP_GSP_EVENT_CALL`。状态靠 50ms `esp_gsp_timer` 里 `get_checked` → `apply_toggle_from_widget`。不要删这个 poll，除非 GSP 提供 value-change 回调。
3. `event->arg` 是动画进度（常为 100），**不是**开关布尔值。用 `esp_gsp_component_get_checked`。
4. 卡片明暗用 `esp_gsp_component_set_enabled` + JSON `"enabled": false` / `disabled_opacity` / `disabled_color`。不要给卡片 `bind_target: "color"`（只改填充，不改边框）。
5. 9 个带抽屉的场景 JSON 必须保持卡片 `enabled: false` 且无 color bind。检查：`python3 firmware/korvo1_yokai_demo/test/check_wifi_bt_drawer.py`。
6. `app_state_t` 必须是 **`static`**（`app_main.c`）。`app_main` 返回后扫描任务仍读这个指针。
7. 扫描任务**只按 generation 取消**，不要用 `state->screen != WIFI_SETTINGS` 提前退出（曾把 `state` 当栈变量用时 screen 是垃圾，扫描永远不跑）。
8. **不要**从 Wi-Fi scan worker 调 GSP。填 `s_wifi_aps` / `s_wifi_ap_count`，设 `s_wifi_results_dirty`，由 UI 定时器/`SCENE_CHANGED` 调 `apply_wifi_scan_ui()`。
9. List **每个逻辑列表只 bind 一次**（`CONFIG_ESP_GSP_MAX_LISTS` 默认 5）。更新用 `set_total` + `refresh`，不要每次 `SCENE_CHANGED` 再 bind。
10. 动态列表总数若已等于 JSON 占位行数（Wi-Fi 为 10），`set_total(10)` 不会重绑已可见行。必须 `set_total(0)` 再 `set_total(N)` 然后 `refresh`，否则 SSID 要拖动才出现。
11. 从 Wi-Fi/BT 详情按「ホーム」：`remember_settings_return` → `s_reopen_drawer` → `ESP_GSP_NO_TRANSITION` → `SCENE_CHANGED` 里 `drawer_open(..., false)`。不要 `FADE_THROUGH_BLACK` + 带动画的 `drawer_open`（会闪空桌面）。
12. 全场景同一有序 `font_charset`。只改一个场景会 `GSPC-RS-FONT-ORDER-CONFLICT`。
13. 不要改 RGB panel 初始化路径（曾黑屏）。App 场景不要开 swipe。

## 关键代码地图

| 路径 | 职责 |
| --- | --- |
| `firmware/korvo1_yokai_demo/main/app_state.h/.c` | 页面/Home/语音状态 |
| `firmware/korvo1_yokai_demo/main/board_ui.c` | BSP、GSP 事件、抽屉、Wi-Fi 扫描 UI |
| `firmware/korvo1_yokai_demo/main/app_main.c` | NVS、static `app_state_t`、启动 |
| `firmware/korvo1_yokai_demo/main/synth_service.c/.h` | 合成器、DSP、A2DP Sink |
| `firmware/korvo1_yokai_demo/scenes/korvo_*_800.json` | 桌面、八 App、Wi-Fi、Bluetooth |
| `firmware/korvo1_yokai_demo/test/check_wifi_bt_drawer.py` | 抽屉卡片 JSON 不变量 |
| `firmware/korvo1_yokai_demo/test/test_app_state.c` | 宿主机导航测试 |
| `firmware/korvo1_yokai_demo/test/test_synth_math.c` | 合成器数学自检 |

抽屉 object key 在各场景相同：`GSP_OBJ_KEY_QUICK_SETTINGS_DRAWER`。Wi-Fi scene 9，Bluetooth scene 10。

## 复现检查

工作树：

```text
/Users/kongweilu/Development/Full Demo/.worktrees/synth-groovebox
```

```bash
python3 firmware/korvo1_yokai_demo/test/check_wifi_bt_drawer.py

cc -std=c11 -Wall -Wextra -Werror \
  -I firmware/korvo1_yokai_demo/main \
  firmware/korvo1_yokai_demo/main/app_state.c \
  firmware/korvo1_yokai_demo/test/test_app_state.c \
  -o /tmp/korvo1_app_state_test && /tmp/korvo1_app_state_test
```

固件：

```bash
source /Users/kongweilu/esp/esp-idf-master/export.sh >/dev/null
cd firmware/korvo1_yokai_demo
idf.py -B build-korvo1-s31-synth build
```

烧录：

```bash
BUILD="firmware/korvo1_yokai_demo/build-korvo1-s31-synth"
python -m esptool --chip esp32s31 -p /dev/cu.usbserial-1140 -b 460800 \
  --before default-reset --after hard-reset write-flash \
  0x2000 "$BUILD/bootloader/bootloader.bin" \
  0x8000 "$BUILD/partition_table/partition-table.bin" \
  0x10000 "$BUILD/korvo1_yokai_demo.bin"
```

布局：bootloader `0x2000`、partition table `0x8000`、app `0x10000`、srmodels `0x610000`。app 分区 6 MiB。

工作流程：最小可运行检查 → GSP pack/完整构建 → 真机烧录 → 用户确认。构建成功 ≠ 真机功能已确认。

## 仍未实现（不要写成已完成）

- 快捷设置开关仍只改 UI，**未**开关真实 Wi-Fi/BT radio。
- Bluetooth 详情仍是静态占位，无 BLE 扫描/连接。
- 天气未联网；语音未跑 AFE/WakeNet/MultiNet/AEC；物体识别无相机/推理。
- 烟花、时钟、计算器、食材管理主要是静态页。
- 待机轮播未进固件。Mosaico / P4X 未适配。
- Korvo-1 无可用软件背光；亮度滑条禁用「明るさ　固定」。音量滑条未接 codec。

## 已知历史回归（仍有效）

- 缺 CJK 字形整屏 `?`。
- 错误触摸/父级布局 → 滑动卡死、点击穿透。
- App swipe → 第二页 App 横滑串页。
- 删桌面底部 Home 必须回收布局高度。
- Wi-Fi/BT 长按进详情不可靠；方案是卡片单击（ON 时）。
