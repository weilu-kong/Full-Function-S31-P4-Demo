# Full-Function-S31-P4-Demo 开发交接（Yokai OS）

更新时间：2026-09-16
当前基线版本：**LVGL v9 + ThorVG + esp_lvgl_adapter (60 FPS locked)**

> **给后续接手 AI Agent 的必读入口**：
> 1. 先通读本文件，了解当前实际代码基线与架构；
> 2. 当前核心开发分支为 **`refactor/korvo1-lvgl9-yokai`**（位于工作树 `.worktrees/lvgl-groovebox`），已完成从老旧 ESP-GSP 方案向原生 LVGL 9 的全面重构；
> 3. 所有功能均已通过真实硬件（ESP32-S31 Korvo-1 800×480 RGB LCD）烧录验证，**不要推倒已验证的架构重新造轮子**。

---

## 1. 核心运行环境与硬件规格

| 属性 | 规格参数 |
| --- | --- |
| **硬件开发板** | ESP32-S31-Korvo-1（16MB Flash, 16MB Octal PSRAM, ES8311 Codec, GT1151 触摸） |
| **屏幕显示** | 800×480 RGB LCD 面板，ST7262 / 驱动已深度调优 |
| **活跃工作树** | `/Users/kongweilu/Development/Full Demo/.worktrees/lvgl-groovebox` |
| **主要分支** | `refactor/korvo1-lvgl9-yokai`（已 push 备份至 GitHub） |
| **开发框架** | ESP-IDF master (v6.2.0)，路径 `/Users/kongweilu/esp/esp-idf-master` |
| **图形栈** | **LVGL 9.2+** + **ThorVG (Lottie Player)** + `components/espressif__esp_lvgl_adapter` |
| **帧缓冲配置** | `TRIPLE_FULL`（3 块完整 800×480 PSRAM 帧缓冲），彻底杜绝切片撕裂与重绘卡顿 |
| **触控采样率** | 16ms（60Hz 满帧触控轮询，无跟手延迟） |
| **高速烧录口** | `/dev/cu.usbserial-1120`，支持 **921600 baud** 极速烧录（54 秒写完 7.3MB 固件） |

---

## 2. 架构演进与当前真机已确认功能

### 1) 系统全局与桌面架构（Home & Shell）
- **2 页水平 TileView 桌面**：
  - 第一页：雷神合成器（Synth）、雪女天气（Weather）、言灵神社（Voice）、目目连视觉（Vision）。
  - 第二页：夜空花花火（Fireworks）、狸屋时钟（Clock）、阴阳算盘（Calculator）、百鬼台所（Food）。
  - 页面翻动切换设定 180ms 弹性回弹，触控响应极快。
- **全屏沉浸式快捷设置（Full-Screen Control Center / 800×480）**：
  - 双重边距归零，消除任何偏向右下角的视觉不对称。
  - 手势支持：屏幕上边缘（$y \le 150$）下滑 $\ge 35\text{px}$ 调出；任意位置上滑 $\ge 35\text{px}$ 收起；右上角支持药丸按钮 `[閉じる]`。
  - 内置功能：Wi-Fi 状态卡片、Bluetooth 状态卡片、主音量全宽触控滑块（联动 ES8311 音频 Codec）。
- **iOS 风格纯黑中心几何展开场景过渡（95ms Bloom Transition）**：
  - 点击 App 卡片或点击 `< ホーム` 返回时，通过全局顶层遮罩自中心圆角胶囊（$60\times 36$）向外极速绽放至全屏（$800\times 480$）。
  - 动画时长精确锁定 **95ms `ease_out`**，**彻底消除了返回桌面时的 1-frame 纯黑屏突切突兀感**。
- **顶栏状态防裁切优化（Home Status Bar）**：
  - `top_bar` 高度 38px。左上角「妖怪端末 (Yokai OS)」及右侧时钟均使用 `UI_FONT_SMALL`（16px，行高 31px）并通过 `LV_ALIGN_LEFT_MID` / `LV_ALIGN_RIGHT_MID` 动态居中，**文字下方笔画完全无裁切**。

### 2) 八大场景功能完成度与真机实测

| App 场景 | 核心实现与已验证能力 | 下一步重点 / 待办 |
| --- | --- | --- |
| **雷神合成器 (Synth)** | 20 键真琴键界面（12 白键 + 8 悬浮黑键）；Cutoff 旋钮（50~10000Hz）、Resonance Q 旋钮（0.1~5.0）；实时示波器折线图；4 种波形切换预设（正弦波、矩形波、ノコギリ、和太鼓）。**波形切换按钮已拓宽至 138px，6px 内边距，字号 16px，各预设完全不溢出边框**。 | 接入更多合成音色与微调 ADSR 包络参数 |
| **雪女天气 (Weather)** | 和风像素神话画卷；真实接入 Open-Meteo REST API；动态状态机根据实时天气/昼夜自动切换底图（晴天、雨天、多云、夜晚）；接入 ThorVG Lottie 降雪降雨粒子动效；Wi-Fi 断开时优雅降级为 DEMO 本地状态。 | 增加未来 3 天天气预报滑动卡片 |
| **Wi-Fi 交信 (Wi-Fi)** | 真实后台扫描任务（`esp_wifi_scan_start`）；动态 AP 列表（显示 SSID、RSSI 信号格、加密锁）；点击 SSID 弹出密码输入抽屉，**集成全键盘虚拟键盘**；支持输入连接、获取 IP 及连接失败原因码反馈。 | 记住已连接过的 Wi-Fi 密码并在开机自动重连 |
| **蓝牙音频 (Bluetooth)** | A2DP Sink 设备名称 `Yokai-Groovebox`；蓝牙设备扫描及状态展示；主音量滑块直连硬件 Codec。 | 完善 BLE 配对握手与状态广播 |
| **阴阳电卓 (Calculator)** | 顶栏 LCD 液晶屏已升级为 **32px（`UI_FONT_LARGE`）**；16 颗大按键（0~9、加减乘除、C、=）标签均升级为 **32px**；支持完整连续四则运算。 | 增加浮点数与历史记录小徽标 |
| **目目连视觉 (Vision AI)** | 带有扫描光标与 HUD 准星的视觉推理 UI。 | 接入 DVP/USB 摄像头驱动及 ESP-DL 人脸/物体模型 |
| **言灵神社 (Voice Shrine)** | 日语与英语端侧命令词交互界面，监听状态动画指示。 | 接入 ESP-SR AFE（含 AEC 回声消除、WakeNet 唤醒词、MultiNet） |
| **狸屋时钟 (Clock & Timer)** | 巨型时钟显示与高精度秒表/计时器（開始、停止、リセット）。 | 联动 SNTP 网络授时校准系统 RTC |
| **百鬼台所 (Food Freshness)** | 食材保质期倒计时卡片，视觉状态徽章与分类列表。 | 支持添加/删除食材条目 |

---

## 3. 全局统一字号体系与资产地图

在 [ui_theme.h](file:///Users/kongweilu/Development/Full%20Demo/.worktrees/lvgl-groovebox/firmware/korvo1_yokai_demo/main/ui/ui_theme.h) 中统一定义，字库统一由 `all_project_symbols.txt` 经 `lv_font_conv` 提取编译成 4 套 NotoSans CJK 矢量点阵：

| 宏定义 | 字号 | 适用场景 |
| --- | --- | --- |
| `UI_FONT_TINY` | 14px | 极微状态标注、备用 |
| `UI_FONT_SMALL` | 16px | 顶栏状态文字、药丸按钮、卡片次要描述、波形切换按钮、各场景微标签 |
| `UI_FONT_REGULAR` | 20px | 全局通用正文、列表标题、各场景返回按钮 `< ホーム`、合成器旋钮读数 |
| `UI_FONT_TITLE` | 20px | 场景主标题、卡片大标题 |
| `UI_FONT_LARGE` | 32px | 巨型时钟、温度数字、计算器 LCD 显示屏与 16 颗按键 |

---

## 4. 关键源码地图（LVGL 9 架构）

```text
firmware/korvo1_yokai_demo/
├── CMakeLists.txt
├── sdkconfig.defaults             # 16MB PSRAM、TRIPLE_FULL 缓冲、ThorVG Lottie 配置
├── main/
│   ├── app_main.c                 # 系统入口、NVS、PSRAM 任务栈分配、服务初始化
│   ├── board_ui.c / .h            # 全局手势状态机、下滑呼出抽屉、上滑关闭、物理按键路由
│   ├── synth_service.c / .h       # 雷神合成器底层 DSP 混音引擎、波形发生器、A2DP Sink
│   ├── weather_service.c / .h     # Open-Meteo HTTP 请求、JSON 解析、天气与时间同步
│   └── ui/
│       ├── ui.c / .h              # UI 根控制器、页面路由注册、95ms iOS 纯黑中心展开转场
│       ├── ui_theme.c / .h        # Yokai OS 暗黑金色视觉主题样式库、字号宏
│       ├── ui_home.c / .h         # 双页 TileView 桌面、38px 顶栏（妖怪端末防裁切对齐）
│       ├── ui_drawer.c / .h       # 800×480 全屏控制中心（Wi-Fi/BT 双子卡、主音量滑块）
│       ├── ui_synth.c / .h        # 雷神合成器 UI（20 琴键、旋钮、138px 波形切换按钮）
│       ├── ui_weather.c / .h      # 雪女天气 UI、Lottie 降雪降雨、动态画卷底图
│       ├── ui_wifi.c / .h         # 真实 Wi-Fi 扫描界面、密码输入抽屉与全键盘
│       ├── ui_bluetooth.c / .h    # 蓝牙配对与设备管理界面
│       ├── ui_apps.c / .h         # 视觉、语音、花火、时钟、计算器、食材等剩余应用
│       ├── ui_wifi_signal.c / .h  # 4 格动态阶梯信号柱指示器组件
│       ├── ui_font_cjk_*.c        # 14px / 16px / 20px / 32px 完整中日文字库
│       └── ui_img_*.c             # 桌面、画卷、天气状态背景图 C 数组
```

---

## 5. 编译、烧录与验证标准流程

### 1) 环境变量激活
每次新开终端必须执行：
```bash
source /Users/kongweilu/esp/esp-idf-master/export.sh
```

### 2) 极速增量编译
```bash
cd "/Users/kongweilu/Development/Full Demo/.worktrees/lvgl-groovebox/firmware/korvo1_yokai_demo"
ninja -C build-korvo1-s31-synth
```
*注：编译成功产物为 `build-korvo1-s31-synth/korvo1_yokai_demo.bin`（约 7.3MB，剩余空间约 1.79MB / 19%）。*

### 3) 921600 波特率极速烧录（必须使用 921600）
```bash
python -m esptool --chip esp32s31 -p /dev/cu.usbserial-1120 -b 921600 write_flash 0x10000 build-korvo1-s31-synth/korvo1_yokai_demo.bin
```
*注：烧录时间约 54 秒，烧录后芯片会自动重启。如需查看串口日志：*
```bash
python -m serial.tools.miniterm --raw /dev/cu.usbserial-1120 115200
```

---

## 6. 接手后优先级开发计划（Task Roadmap）

按用户产品演示需求，建议后续 Agent 优先推进以下工作：

1. **Task 4: 端侧语音与 AEC（言灵神社 / Voice Shrine）**：
   - 接入 ESP-SR AFE 算法库（AEC 回声消除、降噪、VAD）；
   - 配置 WakeNet 日语唤醒词（`wn9l_ja_konnichihaesp_tts3`）；
   - 接入 MultiNet 离线命令词识别，并在 UI 上展示识别到的命令（如“音量を上げて”、“天気を教えて”等）。
2. **Task 5: 端侧物体识别（目目连视觉 / Vision AI）**：
   - 初始化 Korvo-1 板载摄像头接口（DVP / USB UVC）；
   - 集成 ESP-DL 轻量神经网络模型，将摄像头预览绘制到 LVGL 画布上并叠加目标检测识别框。
3. **Task 6: Wi-Fi 密码保存与自动重连**：
   - 使用 NVS 存储用户在密码键盘输入的 Wi-Fi SSID 和密码；
   - 启动时自动尝试重连已保存的 Wi-Fi 网络，连上后自动更新顶栏信号图标与时间。
4. **Task 7: 蓝牙配对与 BLE 广播常驻**：
   - 完善蓝牙设备连接状态反馈，让手机连接 A2DP 后能顺畅播放音乐并与合成器混音。
