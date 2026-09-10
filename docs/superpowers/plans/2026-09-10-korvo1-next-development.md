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

### Task 2: 完成 Synthesizer

**Files:**
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_synth_800.json`
- Modify/Create: `firmware/korvo1_yokai_demo/main/synth_service.c`
- Modify/Create: `firmware/korvo1_yokai_demo/main/synth_service.h`
- Modify: `firmware/korvo1_yokai_demo/main/CMakeLists.txt`

- [ ] 为白键和 C#、D#、F#、G#、A# 黑键加入独立 press/release callback。
- [ ] 用板载音频 codec 输出低延迟音符，离开页面时释放全部音符。
- [ ] 将琴键按压、波形、节拍和雷纹动画绑定到实际播放状态。
- [ ] 测量首次发声延迟、连续按键和返回 Home 后资源释放；真机验收后提交。

### Task 3: 完成天气

**Files:**
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_weather_800.json`
- Create: `firmware/korvo1_yokai_demo/main/weather_service.c`
- Create: `firmware/korvo1_yokai_demo/main/weather_service.h`
- Modify: `firmware/korvo1_yokai_demo/main/CMakeLists.txt`

- [ ] 复用现有 Wi-Fi 初始化，补连接状态、SNTP 和天气请求配置。
- [ ] 明确城市、天气数据源和凭据保存方式；不得把密钥提交到仓库。
- [ ] 将晴、雨、雪、夜晚映射到同一妖怪村落的场景、光照和角色状态。
- [ ] 显示温度、更新时间、断线、获取失败和缓存时间；演示数据继续标注 DEMO。

### Task 4: 完成端侧语音与 AEC

**Files:**
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_voice_800.json`
- Create: `firmware/korvo1_yokai_demo/main/voice_service.c`
- Create: `firmware/korvo1_yokai_demo/main/voice_service.h`
- Modify: `firmware/korvo1_yokai_demo/main/CMakeLists.txt`

- [ ] 录制 Korvo-1 原始多声道样本，确认两路麦克风和播放参考的真实 slot 顺序。
- [ ] 配置 ESP-SR AFE：AEC、降噪、VAD、WakeNet；唤醒词使用 `wn9l_ja_konnichihaesp_tts3`。
- [ ] 校准播放参考延迟，保留可调参数并记录安静/音乐播放/近场说话三种结果。
- [ ] 唤醒后进入语音页，用 `mn7_en` 识别经用户确认的英语命令词表。
- [ ] 将待唤醒、聆听、识别中、成功和失败映射到言灵神社角色姿态；Home 后 WakeNet 常驻。

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

- [ ] 让两个圆点控制真实无线状态，并在所有场景间同步，而非仅切换当前页面外观。
- [ ] 专项验证 Wi-Fi 详情页真实扫描、重复更新、关闭后再进入和非 UTF-8 SSID 的显示策略。
- [ ] 接入 BLE 扫描，将附近设备名、RSSI、连接中、连接失败和已连接状态显示在详情页。
- [ ] 详情页返回时恢复进入前的页面；实体 Home 始终返回桌面第一页。

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
