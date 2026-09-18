# Korvo-1 视觉识别应用实施计划 (Task 5 Vision Agent)

> **目标**：在 ESP32-S31-Korvo-1 Yokai OS 上实现 Edge 视觉识别 App（`UI_SCREEN_VISION`），支持 Camera Preview、离线人脸检测/识别/录入以及 COCO 80 类物体检测。  
> **依据规范**：[`docs/superpowers/specs/yokai_task5_vision_agent_spec.md`](../specs/yokai_task5_vision_agent_spec.md)  
> **基准分支**：`codex/vision-ai` (基于 `refactor/korvo1-lvgl9-yokai` @ `af471b3`)

---

## 阶段规划概览

根据实施规范要求，本任务严格执行阶段门禁，在 Camera Preview 稳定并通过 20 次进出无内存泄漏之前，严禁引入 ESP-DL 人脸/物体模型。

| 阶段 | 目标 | 核心产物 | 门禁条件 |
| --- | --- | --- | --- |
| **Phase 1** | Vision Service 骨架与生命周期 | `vision_service.h/.c`、路由生命周期集成 | 编译无 warning，现有回归测试全绿 |
| **Phase 2** | Camera Preview 实时预览 | `vision_camera.h/.c`、V4L2 驱动适配、LVGL 取景框 | 连续进出 20 次无泄漏、预览 >= 15 FPS、语音/音频无回归 |
| **Phase 3** | Buffer 所有权与 Latest-Frame 解耦 | 2-slot 邮箱机制、DMA 保护、诊断统计 | 无 use-after-free、无 double release |
| **Phase 4** | Human Face Detect | ESP-DL 人脸检测、预创建 LVGL 8 个识别框、坐标变换 | 单/多人检测稳定、坐标映射无偏差 |
| **Phase 5** | Human Face Recognition | 人脸特征提取、Known / UNKNOWN 判定 | 录入前全 UNKNOWN，录入后稳定识别 |
| **Phase 6** | Face Enrollment & SPIFFS DB | 人员录入状态机（5次合格采样）、`/storage` 分区挂载、持久化 | 重启保持、Clear All 具备确认弹窗 |
| **Phase 7** | COCO Object Detection | YOLO11n-320 模型集成、80类日语标签字典与英文降级 | 常见物体准确框选、置信度显示 |
| **Phase 8** | FACE / OBJECT 模式切换 | 互斥模型卸载/加载、状态机安全切换 | 连续切换 50 次无崩溃、无内存残留 |
| **Phase 9** | 性能调优与全系统回归 | 30 分钟全功能并发压力测试 | 0 WDT、0 爆音、Voice 可用、Heap/PSRAM 零持续下降 |

---

## 硬件与子系统核查基准 (Checklist §4)

- **Camera 传感器**：OV3660（DVP 8-bit, 20MHz XCLK, SCCB via I2C）
- **驱动入口**：`bsp_camera_start(NULL)`，V4L2 设备路径 `ESP_VIDEO_DVP_DEVICE_NAME` (`/dev/video2`)
- **像素格式**：优先 `V4L2_PIX_FMT_RGB565`
- **取景器规格**：LVGL 800×480 屏幕，左侧 520×310 预览区域，右侧控制与结果面板
- **任务优先级梯队**：
  `Synth Audio (10)` > `Voice Feed (9)` > `Voice Fetch (7)` > `LVGL Adapter (6)` > `Camera Capture (5)` > `Vision Inference (3-4)`
- **存储与分区**：
  - Flash: 16 MB
  - `factory`: 11.5 MB（模型优先嵌入 rodata）
  - `model`: 3.5 MB（ESP-SR 语音专用，禁止触碰）
  - `storage`: 960 KB（SPIFFS 分区，挂载至 `/storage` 存放 `faces.db`）
