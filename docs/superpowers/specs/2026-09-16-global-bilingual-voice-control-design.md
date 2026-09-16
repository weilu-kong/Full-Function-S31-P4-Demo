# ESP32-S31 Korvo-1 全局双语语音控制设计

日期：2026-09-16

分支：`refactor/korvo1-lvgl9-yokai`

目标：Task 4 — 为 Yokai OS 接入全局 ESP-SR AFE、AEC、WakeNet 与离线中英双语命令控制。

## 1. 目标与边界

本阶段在不回退现有 ESP-IDF master、LVGL 9、ThorVG 和 44.1 kHz 音频架构的前提下，实现：

- 全局常驻 WakeNet，任意页面均可唤醒。
- 日语唤醒词“こんにちは ESP”。
- 英语短句与日语近似音素命令共存于同一 MultiNet7 English 模型。
- 打开 8 个 App、Wi-Fi 设置、蓝牙设置和桌面。
- 音量增加 10%、降低 10%、静音及恢复静音前音量。
- 言灵神社作为功能说明与识别结果页面；页面打开时持续接收命令，不要求重复唤醒。
- 使用扬声器最终输出作为 AEC 参考，使设备播放音乐时仍可识别命令。

本阶段不包含：

- 通用语音转文字。
- 云端语音服务。
- 自训练原生日语 ASR 模型。
- 任意自然语言理解或命令同义句自动扩展。
- Task 5 摄像头识别和 Task 6 Wi-Fi NVS 工作。

## 2. 已确认的用户体验

### 2.1 全局模式

启动后进入 `GLOBAL_WAKE`：

1. WakeNet 持续监听“こんにちは ESP”。
2. 唤醒成功后播放一个很短的提示音，并开放 5 秒命令窗口。
3. 识别成功后执行一条命令，随后回到 WakeNet。
4. 第一次未识别时显示“もう一度”，再开放一次 5 秒窗口。
5. 第二次失败或超时后安静退出，继续等待唤醒词。

### 2.2 言灵神社连续模式

进入 `UI_SCREEN_VOICE` 后切换到 `VOICE_CONTINUOUS`：

- 不要求唤醒词，持续运行离线命令识别。
- 每次识别完成后自动继续监听。
- 音量命令执行后留在当前页面。
- 打开其他 App、设置页或桌面后退出连续模式并恢复 `GLOBAL_WAKE`。
- 页面待机时列出全部中英文功能。
- 页面显示当前状态、匹配到的标准命令、对应功能和执行结果。

MultiNet 是命令分类器而非语音转写器，因此 UI 不显示伪造的逐字听写，只显示匹配到的规范命令名称、语言和置信度。

## 3. 命令表

每项仅保留一个标准英语短句与一个日语说法，避免无边界增加同义词造成相互混淆。

| 功能 | English command | 日语命令 | 动作 |
|---|---|---|---|
| 雷神合成器 | Open the synthesizer | 音楽 / Ongaku | `UI_SCREEN_SYNTH` |
| 雪女天气 | Show the weather | 天気 / Tenki | `UI_SCREEN_WEATHER` |
| 言灵神社 | Open voice control | 言霊 / Kotodama | `UI_SCREEN_VOICE` |
| 目目连视觉 | Open the camera | カメラ / Kamera | `UI_SCREEN_VISION` |
| 烟火应用 | Show the fireworks | 花火 / Hanabi | `UI_SCREEN_FIREWORKS` |
| 时钟 | Open the clock | 時計 / Tokei | `UI_SCREEN_CLOCK` |
| 阴阳计算器 | Open the calculator | 計算 / Keisan | `UI_SCREEN_CALCULATOR` |
| 食物应用 | Open the food app | 料理 / Ryouri | `UI_SCREEN_FOOD` |
| Wi-Fi 设置 | Open Wi-Fi settings | ワイファイ / Waifai | `UI_SCREEN_WIFI` |
| 蓝牙设置 | Open Bluetooth settings | ブルートゥース / Buruutuusu | `UI_SCREEN_BLUETOOTH` |
| 返回桌面 | Go back home | ホーム / Hoomu | `UI_SCREEN_HOME` |
| 音量 +10% | Turn the volume up | 上げて / Agete | 音量加 10 |
| 音量 -10% | Turn the volume down | 下げて / Sagete | 音量减 10 |
| 静音 | Mute the sound | ミュート / Myuuto | 保存当前非零音量并设为 0 |
| 恢复音量 | Restore the volume | 戻して / Modoshite | 恢复静音前音量 |

英语短句由 MultiNet7 English 的 G2P 生成音素。日语命令使用显式音素接口注册，初始音素来自英语近似发音，真机测试时只调整表内音素，不改变命令 ID、UI 或分发逻辑。

## 4. 方案选择

### 4.1 采用：44.1 kHz 主链路 + ESP32-S31 硬件 ASRC

保持现有 ES8389、合成器和蓝牙 A2DP 的 44.1 kHz、16-bit、双声道配置。ESP-SR 所需的 16 kHz 数据由 ESP32-S31 硬件 ASRC 生成：

```text
双麦 44.1 kHz ─→ ASRC0 ─→ 16 kHz 双麦 ─┐
                                        ├→ AFE MMNR → WakeNet / MultiNet7
扬声器最终 PCM ─→ ASRC1 ─→ 16 kHz 参考 ─┘
```

- 麦克风链路保留双声道，转换后提供两个 `M` 通道。
- AEC 参考取自效果器处理后、写入 Codec 前的最终立体声 PCM，混合为参考通道并转换至 16 kHz。
- AFE 输入排列使用 `MMNR`：双麦、空通道、播放参考。
- 没有播放数据时参考通道填零。
- 保留可调的 AEC 参考延迟，处理 Codec、DMA 和缓冲带来的实际时延。

这一路线不会降低已验证的合成器和蓝牙音频质量。

### 4.2 备用：ESP-DSP 软件重采样

若 S31 硬件 ASRC 驱动在当前 IDF master 上出现阻断性问题，可使用项目已有 ESP-DSP 重采样器作为回退方案。它保持 44.1 kHz 主链路，但 CPU 占用和时延调试成本更高，不作为首选。

### 4.3 不采用：全局切换为 16 kHz

将 Codec/I2S 全部切换到 16 kHz 虽可简化语音链路，但会降低合成器质量并影响 A2DP，违反现有真机基线。

## 5. 软件结构

### 5.1 新增语音服务

仅新增：

- `main/voice_service.c`
- `main/voice_service.h`

语音服务负责：

- 打开 BSP 麦克风输入。
- 管理硬件 ASRC、AFE、WakeNet 和 MultiNet 生命周期。
- 注册英语短句和日语近似音素命令。
- 维护 `GLOBAL_WAKE` / `VOICE_CONTINUOUS` 模式。
- 接收扬声器 PCM 参考数据。
- 将识别结果写入固定长度 FreeRTOS 队列。
- 暴露初始化状态和明确错误信息给 UI。

不新增第二套 App 路由、事件总线、接口层或工厂类。

### 5.2 现有文件改动

- `main/app_main.c`
  - 在合成器音频初始化后启动语音服务。
  - 语音初始化失败只记录错误，不阻止 UI 和其他服务启动。

- `main/synth_service.c/.h`
  - 在最终 PCM 写入 Codec 前，通过非阻塞入口复制 AEC 参考数据。
  - 提供短提示音请求，由现有音频输出任务生成，不新增音频资源。

- `main/ui/ui.c`
  - 在现有 `ui_tick_periodic()` 中非阻塞读取识别结果。
  - 复用 `ui_switch_screen()` 执行 App 与设置页跳转。
  - 复用 `synth_service_get_master_volume()` 和 `synth_service_set_master_volume()` 执行音量动作。
  - 根据当前页面切换全局或连续识别模式。

- `main/ui/ui_apps.c/.h`
  - 将当前模拟识别按钮页改为命令说明与实时结果页。
  - 提供状态更新函数，所有 LVGL 更新仍发生在 LVGL 任务中。

- `main/ui/ui_drawer.c/.h`
  - 增加不触发回调的音量滑块同步函数。

- `main/CMakeLists.txt`、`main/idf_component.yml`、`sdkconfig.defaults`
  - 启用 AFE、WakeNet、MultiNet7 English 和 S31 ASRC 所需配置。

现有 `app_state` 不扩展为另一套命令分发系统；UI 已有的屏幕状态和路由是唯一执行入口。

## 6. 并发与数据所有权

语音服务使用两个长期任务：

1. Feed 任务读取麦克风、完成重采样并向 AFE 喂入 `MMNR` 帧。
2. Fetch/recognition 任务读取 AFE 输出，执行 WakeNet/MultiNet 状态机并发布结果。

约束：

- 音频播放任务不等待语音任务。
- AEC 参考通过固定大小环形缓冲区传递；满时丢弃最旧数据，避免打断声音。
- 语音任务不调用 LVGL。
- 识别结果队列只保存小型结构：命令 ID、语言、置信度和状态。
- 现有 16 ms LVGL timer 负责消费结果和更新界面。
- 页面模式使用原子状态或短临界区切换，不引入额外锁层级。

## 7. 音量语义

- 音量范围始终限制为 0–100。
- 增减步进固定为 10。
- 静音时保存最近一个非零音量。
- 已静音时重复静音保持不变，不覆盖保存值。
- 恢复音量时使用保存值；若启动以来没有保存值，回到默认音量 80。
- 命令执行后同步 Codec 输出音量、快捷设置滑块和言灵神社结果文本。

## 8. 故障与降级

- 模型、AFE、麦克风或 ASRC 初始化失败时，语音功能标记为不可用；其他功能继续运行。
- 言灵神社显示真实错误原因，不显示“正在监听”。
- 麦克风或参考数据暂时缺帧时填零，不阻塞扬声器任务。
- 未知或低置信度结果不改变页面和音量。
- 命令窗口超时后回到 WakeNet，不无限占用 MultiNet 命令模式。
- 连续模式离开语音页面时必须恢复全局唤醒模式。
- 关键计数器记录输入欠载、参考溢出、AFE 错误和拒绝次数，供串口诊断。

## 9. 模型与烧录

现有分区表包含：

- `factory`：9 MB
- `model`：6 MB，起始地址 `0x910000`
- `storage`：960 KB

启用 WakeNet 与 MultiNet 后，构建必须验证模型镜像不超过 6 MB。首次启用或模型发生变化时需要烧录 bootloader、分区表、应用和模型镜像；模型稳定后，普通 UI/逻辑迭代仍可继续只烧录 `0x10000` 的 App 镜像。

## 10. 验证策略

### 10.1 最小自动测试

保留一个小型可运行测试，覆盖：

- 15 个命令 ID 到屏幕或音量动作的映射。
- 音量上下限。
- 静音与恢复语义。
- `GLOBAL_WAKE` 和 `VOICE_CONTINUOUS` 的模式切换。
- 未知命令不产生副作用。

### 10.2 构建检查

- 使用现有 ESP-IDF master 环境完成增量构建。
- 检查 App 与模型分区容量。
- 确认 WakeNet 和 MultiNet7 English 模型实际进入生成产物。
- 不接受新增编译警告。

### 10.3 真机验收

1. 桌面和任意 App 中说“こんにちは ESP”均能进入命令窗口。
2. 言灵神社内无需唤醒词即可连续执行命令。
3. 每个中英文标准命令在安静环境、约 0.5 米距离测试 5 次，至少成功 4 次。
4. 扬声器以 60% 音量播放合成器或蓝牙音乐时，仍能完成唤醒与音量控制。
5. 从说完命令到开始页面切换的目标延迟不超过约 1.5 秒。
6. 音量严格按 10% 步进，静音恢复正确，快捷设置滑块同步。
7. 连续运行 30 分钟，无看门狗复位、DMA 中断或持续内存下降。
8. LCD 保持现有 60 FPS；触控、合成器、天气、Wi-Fi 和蓝牙功能无回归。

## 11. 已知限制

日语命令由英语模型通过近似音素实现，不等同于原生日语识别。准确率受说话人、口音、噪声和音素写法影响，因此必须保留真机音素校准能力。只有当约定命令集无法达到验收标准时，才重新评估原生日语模型或自训练方案。
