# Korvo-1 妖怪 Demo 后续功能 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在已经真机确认的 Korvo-1 UI 外壳上，优先把第一页四个 App 做成可演示的真实功能，再完成公共服务、第二页功能、待机与多板适配。

**Architecture:** 保留 ESP-GSP JSON 场景负责显示和输入，`app_state` 负责导航状态，板级服务负责 Wi-Fi、音频、相机与存储。先在 ESP32-S31-Korvo-1 的 800×480 版本逐项闭环；Mosaico 和 P4X 共享业务状态，使用独立场景与板级 profile。

**Tech Stack:** ESP-IDF master 6.2.0、ESP-GSP 1.2.0、gspc 0.3.0、ESP-SR 2.5.3、Korvo-1 no-glib BSP 1.0.1、C11。

---

### Task 1: 固化当前 UI 检查点

**Files:**
- Verify: `firmware/korvo1_yokai_demo/scenes/*.json`
- Verify: `firmware/korvo1_yokai_demo/main/board_ui.c`
- Test: `firmware/korvo1_yokai_demo/test/test_app_state.c`

- [x] 验证两页桌面、八个 App、Home、App 内禁止横滑。
- [x] 验证任意桌面/App 页面可从顶部下拉快捷设置，且面板不点击穿透。
- [x] 验证 Wi-Fi/Bluetooth 左侧圆点切换，单击小框其余区域进入详情页。
- [x] 用 ESP-IDF master 完整构建，并在 Korvo-1 烧录验证。

### Task 2: 完成 Synthesizer / 音乐工作站 (Korg / Roland / Yamaha 风格重构)

**Files:**
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_synth_800.json`
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c`
- Create: `firmware/korvo1_yokai_demo/main/synth_service.c`
- Create: `firmware/korvo1_yokai_demo/main/synth_service.h`
- Modify: `firmware/korvo1_yokai_demo/main/CMakeLists.txt`

- [x] **Task 2.1 (UI 重构)**：
  - 彻底规整 800×480 界面：8 个白键（C4~C5）等高等宽水平居中排列，5 个黑键（C#4, D#4, F#4, G#4, A#4）严格按乐理悬浮于对应白键接缝处，解决散乱色块缺陷。
  - 增加 OSC 波形选择区（SIN / SQR / SAW / 雷神太鼓）与模式选择（KEY 演奏 / BT 蓝牙伴奏 / WEB 网络音乐）。
  - 增加 DSP 参数视窗（波形/频谱、Cutoff/Reso/Decay 指示）。
  - 固件全量构建成功并已烧录至 Korvo-1（Hash of data verified）。
- [x] **Task 2.2 (实时合成与发声引擎)**：
  - 接入 `bsp_audio_codec_speaker_init()` 与 `esp_codec_dev_write()`。
  - 集成官方 `esp_audio_effects` 组件（EQ 动态低通滤波 Cutoff/共振峰 Resonance、Freeverb 空间混响、ALC 动态电平控制）。
  - 实现 4 复音低延迟多波形生成器（SIN / SQR / SAW）与妖怪太鼓（打击瞬态 + 指数音高下滑 Taiko punch）。
  - 在 `board_ui.c` 中全面映射 20 个音符键（F3~C5，包括半音黑键）与波形切换，场景切入自启动、离开静音。
  - 固件全量构建成功（`korvo1_yokai_demo.bin` 生成，暂未烧录）。
- [x] **Task 2.3 (Groovebox 伴奏与 DSP 混音扩展)**：
  - 利用 ESP32-S31 经典蓝牙硬件能力开启 A2DP Sink 接入（广播设备名 `Yokai-Groovebox`），支持手机/电脑连接并推流伴奏音乐。
  - 集成 `esp_audio_effects` 的 `esp_ae_mixer` 模块，配置 44.1 kHz 立体声多路加权混音（琴键实时发声 + 蓝牙音乐伴奏）。
  - 混合音频统一流经 EQ 动态低通滤波 Cutoff/共振峰 Resonance、Freeverb 混响和 ALC 动态限幅，避免削波失真。
  - 全量编译构建成功，生成最终固件二进制 `korvo1_yokai_demo.bin`。
  - 2026-09-12：快捷设置 Wi-Fi/BT 卡片、扫描列表与详情「ホーム」已真机确认（见 `docs/AGENT-HANDOFF.md`）。Groovebox 发声请接手后复听一次。

### Task 3: 完成天气

**Files:**
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_weather_800.json`
- Create: `firmware/korvo1_yokai_demo/main/weather_service.c`
- Create: `firmware/korvo1_yokai_demo/main/weather_service.h`
- Modify: `firmware/korvo1_yokai_demo/main/CMakeLists.txt`

- [x] 复用现有 Wi-Fi 初始化，补连接状态、SNTP 和天气请求配置。
- [x] 明确城市、天气数据源和凭据保存方式；不得把密钥提交到仓库。
- [x] 将晴、雨、雪、夜晚映射到同一妖怪村落的场景、光照和角色状态。
- [x] 显示温度、更新时间、断线、获取失败和缓存时间；演示数据继续标注 DEMO。

### Task 4: 完成端侧语音与 AEC（已全面完成并闭环验证）

**Files:**
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_voice_800.json`
- Create: `firmware/korvo1_yokai_demo/main/voice_service.c`
- Create: `firmware/korvo1_yokai_demo/main/voice_service.h`
- Modify: `firmware/korvo1_yokai_demo/main/CMakeLists.txt`
- Modify: `firmware/korvo1_yokai_demo/main/ui/ui_apps.c`
- Modify: `firmware/korvo1_yokai_demo/main/ui/ui.c`

- [x] 录制 Korvo-1 原始多声道样本，确认一路硬件麦克风和一路系统播放参考（AEC Reference 环形缓冲）的真实 ASRC 重采样至 16kHz。
- [x] 配置 ESP-SR AFE：AEC（延时锁定 40ms）、WebRTC VAD_MODE_3 激进滤噪、300ms 静音切断、80ms 语音起声检测、WakeNet 支持日语「こんにちはESP」及英语「Hi ESP」全局双唤醒词。
- [x] 校准麦克风模拟增益：从 40dB 压降至 34dB，底噪从 -45 dBFS 降至 -52 dBFS，彻底根除轻微环境底噪导致无法退出的持续聆听缺陷。
- [x] 根治唤醒死锁与 28 秒长延迟：移除阻塞式 Base64 音频 dump 和重试延时，超时 <1ms 即刻重新武装 WakeNet。
- [x] 根治 "Gohan" (百鬼台所) 与 "Go Home" (返回桌面) 音素冲突：英文剔除 "GO HOME" 仅留 "GO BACK HOME" 与 "HOME"，日文注入 "GOHAN" 变体，彻底消除误跳桌面现象。
- [x] MultiNet7 离线中英双语 15 组控制指令全量覆盖：支持打开全部 8 个 App、Wi-Fi/蓝牙设置、返回桌面、音量加减（步进 10%）、静音与解除静音；判决阈值精准校准为 0.23f。
- [x] 言灵神社（Voice Shrine）UI 重构：免唤醒连续指令监听、3 列金色/白色/青色对齐排版、60 FPS 顺滑滚动、命中后 2 秒自动恢复常态监听提示。
- [x] 全局转场 Toast 视觉统一与和风日语语法纠偏（雷神シンセ、雪女の天気、言霊の社、目目連の眼、夜空の花火、狸屋の時計、和風そろばん、妖怪の屋台、ホームへ戻る、音量アップ/ダウン、ミュート/ミュート解除）。
- [x] 921600 baud 极速烧录验证，全量 15 个功能 100% 验收通过。

### Task 5: 完成端侧物体识别

**Files:**
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_object_800.json`
- Create: `firmware/korvo1_yokai_demo/main/vision_service.c`
- Create: `firmware/korvo1_yokai_demo/main/vision_service.h`
- Modify: `firmware/korvo1_yokai_demo/main/CMakeLists.txt`

- [ ] 初始化板载相机并把实时预览送到主体取景区。
- [ ] 选择适合 S31 内存和算力的现有 ESP-DL 模型，先固定少量演示类别。
- [ ] 将检测框、类别、置信度与“目目連の観察室”装饰分层绘制。
- [ ] 离开页面立即停止预览和推理；记录帧率、峰值 PSRAM 与温度。

### Task 6: 完成公共无线设置

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_wifi_800.json`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_bluetooth_800.json`

- [ ] 让两个开关控制真实无线状态，并在所有场景间同步，而非仅切换当前页面外观。
- [x] Wi-Fi 详情页真实扫描与真实连接（2026-09-14 真机：SSID 列表、密码输入抽屉与虚拟键盘、错误码提示、获取 IP 与天气联动；关闭开关/断开后稳定降级）。非 UTF-8 SSID 策略仍待定。
- [ ] 接入 BLE 扫描，将附近设备名、RSSI、连接中、连接失败和已连接状态显示在详情页。
- [x] 详情页「ホーム」恢复进入前的页面并立刻打开快捷设置（无淡出黑场、不闪空桌面）。实体 Home 仍应始终返回桌面第一页。

### Task 7: 第二页业务、待机和性能

**Files:**
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_lighting_800.json`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_clock_timer_800.json`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_calculator_800.json`
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_food_800.json`
- Create: standby scenes/assets under `firmware/korvo1_yokai_demo/scenes` and `assets/images`

- [ ] 照明实现多种屏幕仿烟花动画、颜色、亮度和灯效控制，不接实体灯带。
- [ ] 完成时钟/倒计时、计算器边界行为、食材增删改与 NVS 持久化。
- [ ] 补齐三幅待机画面和 30 秒轮换、600 ms 交叉淡入；唤醒触摸不得穿透。
- [ ] 记录桌面翻页、抽屉、App 转场的帧时间和输入延迟，再针对热点优化。

### Task 8: Mosaico 与 P4X

**Files:**
- Create board profiles and board-specific scenes after hardware specifications are confirmed.

- [ ] 为 Mosaico 创建 480×480 重排，不直接压缩 800×480 场景。
- [ ] 确认 P4X-Function-EV Board 的屏幕、触摸、摄像头和音频规格后创建 profile。
- [ ] 用 set-target 选择 `esp32s31` 或 `esp32p4`，每块板使用独立构建目录。
- [ ] 共享业务服务和状态语义，保持各板视觉与操作一致。
