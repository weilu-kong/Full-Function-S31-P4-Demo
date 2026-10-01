# Yokai OS — Task 5「目目连视觉」Agent 实施规范

> **用途**：本文件可直接交给 Coding Agent / Software Agent 作为 Task 5 的实施规范。  
> **目标平台**：ESP32-S31 / Yokai OS / ESP-IDF master / LVGL 9  
> **目标 App**：`UI_SCREEN_VISION`（目目连视觉）  
> **功能范围**：Camera Preview + 人脸检测/识别/录入 + 物体检测  
> **不包含**：语音识别实现、Task 6 Wi-Fi NVS、云端视觉、通用图像分类训练、活体检测  
> **基准分支**：`refactor/korvo1-lvgl9-yokai`

---

## 0. Agent 执行总则

Agent 必须按本文件的阶段顺序实施，不允许一次性把 Camera、Face Recognition、Object Detection、UI Overlay、Database 全部混合开发。

必须遵守以下原则：

1. **现有语音识别功能视为已经完成，不重构、不替换、不降级。**
2. **现有 Yokai OS 路由继续作为唯一页面路由。**
3. Vision App 使用已有 `UI_SCREEN_VISION`，不得另建第二套 App Router / Event Bus / Framework。
4. 所有 LVGL API 只能在现有 LVGL/UI task 上调用。
5. Camera task / AI inference task 禁止直接调用 `lv_*`。
6. Camera Preview 与 AI inference 必须解耦。
7. AI inference 永远处理“最新可用帧”，禁止积压历史图像队列。
8. Vision inference 的实时优先级低于 Audio / Voice / LVGL。
9. Vision 初始化或模型失败不得阻止 Yokai OS 其他功能启动。
10. 未经仓库代码确认，不得臆测 Camera sensor、GPIO、MIPI/DVP 总线、像素格式或 BSP API。
11. 如果当前 BSP 已经提供 Camera service / frame API，优先复用，禁止重复实现 Camera driver。
12. 第一版优先保证稳定、可调试、可回退，而不是追求最高 FPS。

---

# 1. 已知项目边界

现有项目已经存在：

```text
UI_SCREEN_VISION
```

“目目连视觉”已经作为 Yokai OS 的一个 App 路由目标。

当前 Task 5 要实现：

```text
UI_SCREEN_VISION
    ↓
Camera Preview
    ↓
[ FACE ] / [ OBJECT ]
```

其中：

### FACE 模式

实现：

```text
人脸检测
↓
人脸识别
↓
Face ID / UNKNOWN
↓
Enroll
↓
Delete Last / Clear All
```

### OBJECT 模式

实现：

```text
COCO Object Detection
↓
Bounding Boxes
↓
Label
↓
Confidence
```

---

# 2. 明确不做的内容

本 Task 不实现：

```text
通用 OCR
云端视觉 API
大语言模型视觉理解
视频录像
相册
人脸活体检测
防照片攻击
年龄/性别推断
情绪识别
人脸属性推断
自训练 YOLO
自动下载模型
Task 6 Wi-Fi NVS
语音识别重构
```

如果 Agent 发现上述需求，应保持接口可扩展，但不得自行扩大本 Task 范围。

---

# 3. 技术架构

采用：

```text
Camera BSP / Existing Camera Driver
                │
                ▼
        vision_camera adapter
                │
                ▼
         vision_service
        ┌───────┼──────────────┐
        │       │              │
        ▼       ▼              ▼
 HumanFace  HumanFace      COCODetect
 Detect     Recognizer     YOLO11n-320
        │       │              │
        └───────┴──────┬───────┘
                       ▼
             vision_result_queue
                       │
                       ▼
               existing LVGL task
                       │
                       ▼
               UI_SCREEN_VISION
```

不得直接引入完整 ESP-WHO App Framework。

允许参考 ESP-WHO 的：

```text
camera buffering
frame ownership
pipeline scheduling
```

但不允许把其 application architecture 整体搬入 Yokai OS。

模型层优先使用 ESP-DL Model Zoo：

```text
HumanFaceDetect
HumanFaceRecognizer
COCODetect
```

---

# 4. Agent 开工前必须完成的仓库检查

在修改任何代码之前，Agent 必须先检查仓库并记录以下信息。

## 4.1 Camera

必须确认：

```text
当前 Camera sensor 型号
Camera driver 来源
Camera init 所在文件
Camera framebuffer API
输出 pixel format
输出分辨率
frame ownership / release API
是否已有 PSRAM framebuffer
是否已有 camera preview demo
```

如果仓库中已有 Camera API，应复用。

只有在完全不存在 Camera abstraction 时，才新增：

```text
main/vision_camera.cpp
main/vision_camera.h
```

---

## 4.2 UI

检查：

```text
UI_SCREEN_VISION 当前定义位置
ui_switch_screen()
ui_tick_periodic()
ui_apps.c/.h 中现有 Vision 页面
LVGL display 分辨率
screen rotation
camera preview 可用区域
```

不得修改现有 screen router 的基本结构。

---

## 4.3 RTOS

检查：

```text
Audio task priority
Voice task priority
LVGL task priority
CPU affinity
PSRAM 当前剩余
internal SRAM 当前剩余
现有 watchdog 配置
```

然后确保：

```text
Vision inference priority
<
Audio / Voice realtime priority
```

---

## 4.4 Storage

检查：

```text
现有 filesystem
SPIFFS / LittleFS / FATFS
/storage 实际 mount point
storage partition 剩余容量
```

不要假定 `/spiffs` 一定存在。

Face DB 路径必须跟随项目实际 filesystem。

---

# 5. 文件结构

目标结构：

```text
main/
│
├── app_main.c
├── voice_service.*              # 已完成，原则上不修改
├── synth_service.*
│
├── vision_service.cpp
├── vision_service.h
│
├── vision_camera.cpp            # 若已有 Camera service，则不新增
├── vision_camera.h
│
├── ui/
│   ├── ui.c
│   ├── ui_apps.c
│   └── ui_apps.h
│
├── CMakeLists.txt
└── idf_component.yml
```

---

# 6. Vision Service API

Vision 内部允许使用 C++。

对 Yokai 其他模块只暴露 C API。

`vision_service.h` 建议：

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VISION_MODE_FACE = 0,
    VISION_MODE_OBJECT,
} vision_mode_t;

typedef enum {
    VISION_STATE_OFF = 0,
    VISION_STATE_STARTING,
    VISION_STATE_RUNNING,
    VISION_STATE_ERROR,
} vision_state_t;

esp_err_t vision_service_init(void);

esp_err_t vision_service_start(void);

void vision_service_stop(void);

esp_err_t vision_service_set_mode(vision_mode_t mode);

vision_mode_t vision_service_get_mode(void);

vision_state_t vision_service_get_state(void);

bool vision_service_poll_result(void *result);

esp_err_t vision_service_enroll_face(void);

esp_err_t vision_service_delete_last_face(void);

esp_err_t vision_service_clear_faces(void);

#ifdef __cplusplus
}
#endif
```

正式实现时 `void *result` 应替换为明确的 `vision_result_t *`。

---

# 7. Result 数据结构

禁止把：

```text
std::vector
std::list
ESP-DL object
model result object
```

直接放入 FreeRTOS Queue。

使用固定大小 POD 结构。

建议：

```c
#define VISION_MAX_DETECTIONS 8
#define VISION_LABEL_SIZE     24

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;

    float confidence;

    int16_t class_id;

    char label[VISION_LABEL_SIZE];
} vision_box_t;
```

结果：

```c
typedef struct {
    uint32_t frame_id;

    vision_mode_t mode;

    uint8_t count;

    vision_box_t boxes[VISION_MAX_DETECTIONS];

    int16_t face_id;

    float face_similarity;

    bool face_known;

    uint32_t inference_ms;

} vision_result_t;
```

---

# 8. Result Queue 策略

建立：

```text
vision_result_queue
```

建议：

```text
length = 2
```

规则：

```text
queue empty:
    push

queue full:
    discard oldest
    push latest
```

视觉结果具有实时性：

```text
旧结果 < 最新结果
```

绝对禁止为了保证每一帧结果都被消费而阻塞 inference task。

---

# 9. Camera Adapter

如果项目没有现成 Camera service，则实现统一适配层。

建议 frame：

```c
typedef enum {
    VISION_PIXFMT_RGB565 = 0,
    VISION_PIXFMT_RGB888,
    VISION_PIXFMT_YUV422,
    VISION_PIXFMT_JPEG,
} vision_pixel_format_t;

typedef struct {
    void *data;

    uint16_t width;
    uint16_t height;

    uint32_t frame_id;

    vision_pixel_format_t format;

    void *driver_private;
} vision_camera_frame_t;
```

API：

```c
esp_err_t vision_camera_init(void);

esp_err_t vision_camera_start(void);

esp_err_t vision_camera_acquire(
    vision_camera_frame_t *frame,
    TickType_t timeout
);

void vision_camera_release(
    vision_camera_frame_t *frame
);

void vision_camera_stop(void);
```

必须遵守：

```text
acquire frame
↓
use frame
↓
release frame
```

不得：

```text
free(camera_dma_buffer)
```

也不得让 UI、AI task 同时无所有权地长期持有 Camera driver buffer。

---

# 10. Pixel Format

第一优先：

```text
RGB565
```

因为：

```text
Camera
↓
Preview
↓
ESP-DL preprocessing
```

路径最简单。

如果现有 Camera 只能输出：

```text
JPEG
YUV422
```

则转换逻辑放在：

```text
vision_camera adapter
或
vision_service preprocessing
```

模型层禁止直接依赖 sensor-specific format。

---

# 11. Buffer 策略

禁止逐帧：

```text
malloc
free
```

第一版建议 Camera framebuffer 数量：

```text
3–4
```

目标：

```text
Buffer A:
Camera 写入

Buffer B:
Preview

Buffer C:
Inference

Buffer D:
Spare
```

具体数量必须根据：

```text
PSRAM free
Camera driver behavior
frame resolution
```

实测决定。

如果 PSRAM 不足，可退回：

```text
3 buffers
```

不得为了保留 4 buffers 导致其他服务缺内存。

---

# 12. FreeRTOS Task 设计

Vision 最多两个长期任务：

```text
vision_capture_task
vision_inference_task
```

禁止额外拆：

```text
face_task
object_task
overlay_task
database_task
```

---

# 13. Capture Task

职责：

```text
Camera capture
↓
publish latest preview frame
↓
决定是否提交 inference
```

伪代码：

```cpp
while (running) {
    vision_camera_frame_t frame;

    if (vision_camera_acquire(&frame, timeout) != ESP_OK) {
        record_camera_error();
        continue;
    }

    publish_latest_preview(frame);

    if (should_submit_inference()) {
        publish_latest_inference_frame(frame);
    }

    vision_camera_release(&frame);
}
```

实际实现必须解决 frame ownership。

不得简单保存已被 `release()` 的 raw pointer。

如果 inference 不能在 release 之前完成，则必须：

```text
reference-count / hold driver buffer
```

或：

```text
copy inference input
```

具体方案根据现有 Camera driver 能力决定。

---

# 14. Latest Frame 策略

禁止：

```text
Frame 100
Frame 101
Frame 102
Frame 103
↓
全部进入 inference queue
```

正确策略：

```text
Camera
↓
latest frame slot
↓
Inference
```

Inference 每次结束后只获取：

```text
当前最新 frame
```

中间未处理的 frame 自动丢弃。

这样 AI 很慢时不会导致：

```text
延迟不断增长
PSRAM queue 不断增长
UI 显示几秒前结果
```

---

# 15. Inference Task

伪代码：

```cpp
while (running) {

    if (!wait_for_latest_frame()) {
        continue;
    }

    uint32_t start = now_ms();

    switch (active_mode) {

    case VISION_MODE_FACE:
        run_face_pipeline();
        break;

    case VISION_MODE_OBJECT:
        run_object_pipeline();
        break;
    }

    result.inference_ms = now_ms() - start;

    publish_latest_result(result);
}
```

模式切换时禁止同时运行两条 pipeline。

---

# 16. FACE Pipeline

逻辑：

```text
Image
↓
HumanFaceDetect
↓
0 face
    → NO FACE

1+ faces
    ↓
HumanFaceRecognizer
    ↓
known
    → Face ID

unknown
    → UNKNOWN
```

推荐模型组合：

```text
HumanFaceDetect:
MSR + MNP

Face Feature:
MFN S8
```

第一版优先使用较轻量的人脸特征模型。

不要一开始切换到最大模型。

---

# 17. FACE 运行逻辑

概念：

```cpp
dl::image::img_t img = make_dl_image(frame);

auto &faces = face_detect->run(img);

vision_result_t result = {};

for (auto &face : faces) {
    add_face_box(result, face);
}

if (!faces.empty()) {

    auto recognized =
        face_recognizer->recognize(img, faces);

    map_face_recognition_result(
        recognized,
        &result
    );
}
```

必须对：

```text
0 faces
1 face
multiple faces
```

都有安全处理。

---

# 18. Face Enrollment

UI 按下：

```text
登録 / Enroll
```

不能立即用当前随机帧 enroll。

流程：

```text
Button pressed
↓
ENROLL_PENDING
↓
等待下一次合格人脸
↓
exactly one face
↓
face size 足够
↓
检测 confidence 足够
↓
执行 enroll
↓
ENROLL_OK
```

如果存在：

```text
0 faces
multiple faces
```

则不 enroll。

UI 显示：

```text
Please show one face
```

或项目对应的中/日文案。

---

# 19. Face ID

第一版身份：

```text
Face 01
Face 02
Face 03
...
```

不要实现屏幕键盘。

未来增加 nickname 时，仅建立：

```text
Face ID
↓
Display Name
```

映射，不修改底层 embedding DB 结构。

---

# 20. Face Database

优先使用已有 filesystem。

目标路径概念：

```text
<existing-storage-mount>/vision/faces.db
```

不得硬编码：

```text
/spiffs
```

除非当前项目确实使用该路径。

Face DB 与模型必须分开。

```text
AI model:
read-only

Face embeddings:
persistent writable storage
```

---

# 21. Delete Face

实现：

```text
Delete Last
Clear All
```

`Clear All` 必须经过二次确认。

例如：

```text
Delete all registered faces?

[Cancel] [Delete]
```

---

# 22. OBJECT Pipeline

第一版采用：

```text
COCODetect
YOLO11n-320
int8
```

不要第一版使用：

```text
YOLO 640
```

目标流程：

```text
Image
↓
COCODetect
↓
results
↓
sort by confidence
↓
Top N
↓
UI boxes
```

N：

```text
VISION_MAX_DETECTIONS = 8
```

---

# 23. Object Result

每个结果转换为：

```text
class_id
label
confidence
x
y
w
h
```

UI 示例：

```text
person 94%
bottle 81%
chair 76%
```

第一版不要求逐项验证全部 80 类。

---

# 24. Confidence Filter

必须实现可调阈值。

建议定义：

```c
#define VISION_OBJECT_CONFIDENCE_DEFAULT  0.50f
#define VISION_FACE_CONFIDENCE_DEFAULT    ...
```

Face threshold 不得在不了解 ESP-DL 当前模型返回含义时自行拍脑袋写死。

Agent 必须先检查当前 ESP-DL API / model example 的 score 定义。

所有 threshold 统一放到：

```text
vision_service.cpp
config section
```

或项目现有 config header。

禁止散落在 UI。

---

# 25. Bounding Box 坐标系统

必须集中实现：

```text
vision_transform_box()
```

输入：

```text
camera width
camera height

preview x
preview y
preview width
preview height

camera rotation
mirror
flip

model box
```

输出：

```text
LVGL coordinates
```

禁止在 UI 各处分别计算缩放。

基本：

```text
scaleX = preview_width  / camera_width
scaleY = preview_height / camera_height

screenX = preview_x + model_x * scaleX
screenY = preview_y + model_y * scaleY
```

如存在：

```text
letterbox
crop
rotation
mirror
```

必须统一在 transform 中处理。

---

# 26. UI 结构

推荐：

```text
┌─────────────────────────────────┐
│ 目目連 · VISION                 │
│                                 │
│ ┌─────────────────────────────┐ │
│ │                             │ │
│ │       CAMERA PREVIEW        │ │
│ │                             │ │
│ │   ┌──────────────┐          │ │
│ │   │ PERSON 91%   │          │ │
│ │   └──────────────┘          │ │
│ │                             │ │
│ └─────────────────────────────┘ │
│                                 │
│    [ FACE ]      [ OBJECT ]     │
│                                 │
│ FACE:                           │
│ Face 01 / UNKNOWN               │
│ Confidence 0.87                 │
│                                 │
│ [Enroll] [Delete]               │
│                                 │
│ OBJECT:                         │
│ person 92% · bottle 81%         │
└─────────────────────────────────┘
```

Camera preview 必须是页面主体。

---

# 27. UI Overlay

预创建固定数量：

```text
VISION_MAX_DETECTIONS
```

的 overlay objects。

例如：

```text
box[0..7]
label[0..7]
```

禁止每帧：

```text
lv_obj_create()
lv_obj_del()
```

正确方式：

```text
对象预创建
↓
每次更新 position / size / text
↓
多余对象 hidden
```

降低 LVGL heap fragmentation。

---

# 28. LVGL Thread Rule

只有 LVGL task 可以执行：

```text
lv_image_set_src
lv_obj_set_pos
lv_obj_set_size
lv_label_set_text
lv_obj_add_flag
lv_obj_clear_flag
```

Inference task 只发布：

```text
vision_result_t
```

Camera task 也不得直接操作 LVGL。

---

# 29. UI Polling

在现有：

```text
ui_tick_periodic()
```

中增加非阻塞处理：

```c
vision_result_t result;

while (vision_service_poll_result(&result)) {
    ui_vision_apply_result(&result);
}
```

如果有 frame event，也采用非阻塞 latest-frame 机制。

不得让现有 16 ms UI tick 因 Vision 等待超过一个短临界区。

---

# 30. 页面生命周期

进入：

```text
ui_switch_screen(UI_SCREEN_VISION)
↓
ui_vision_on_enter()
↓
vision_service_start()
```

退出：

```text
ui_vision_on_exit()
↓
vision_service_stop()
```

---

# 31. Start 流程

建议：

```text
VISION_STATE_STARTING
↓
camera start
↓
buffer ready
↓
inference service ready
↓
VISION_STATE_RUNNING
```

UI 在 STARTING：

```text
Starting camera…
```

不得显示错误的 live preview 状态。

---

# 32. Stop 流程

必须按顺序：

```text
1. stop accepting new inference work
2. let current inference reach safe boundary
3. stop capture
4. return outstanding camera buffers
5. stop camera
6. clear result queue
7. release temporary inference buffers
8. state = OFF
```

禁止：

```text
delete model
```

时 inference task 还在运行。

---

# 33. 模型生命周期

第一版建议：

```text
vision_service_init
    ↓
轻量初始化

first start
    ↓
load needed model

leave Vision App
    ↓
stop Camera
release temp buffers
保留 model object
```

如果实测 PSRAM 紧张，再改成：

```text
leave Vision
↓
destroy active AI model
```

这个优化必须基于实际内存数据，不得预先假设。

---

# 34. FACE / OBJECT 模式切换

切换：

```text
FACE
↓
stop submitting new frames
↓
finish current inference
↓
clear stale results
↓
active mode = OBJECT
↓
resume
```

反向同理。

禁止：

```text
FACE model inference
+
OBJECT model inference
```

同时处理同一 camera stream。

---

# 35. Camera Preview FPS 与 AI FPS 解耦

目标：

```text
Preview:
15–30 FPS

Face detection:
目标 >= 5 FPS

Object detection:
目标 >= 1 FPS
期望 2–5 FPS
```

注意：

上述 AI FPS 是工程目标，不是对 S31 性能的既定事实。

必须真机 benchmark。

---

# 36. Inference 调度

FACE：

```text
Camera 20 FPS
↓
例如每 2–4 frame 提交一次 detection
```

OBJECT：

```text
inference done
↓
立即取得 latest frame
↓
下一轮 inference
```

不要使用：

```text
delay(200ms)
```

作为主要调度逻辑。

应该由：

```text
model inference speed
+
latest frame availability
```

自然限速。

---

# 37. 模型依赖

Agent 必须先检查当前 ESP-IDF / ESP-DL 组件兼容版本。

依赖目标：

```text
human_face_recognition
coco_detect
esp-dl
```

可以在：

```text
idf_component.yml
```

声明。

不要无条件复制某个旧示例的版本号。

实施时必须：

```text
idf.py reconfigure
idf.py build
```

确认 dependency graph。

最终使用 lock file 保持可重复构建。

---

# 38. 模型存储

当前 Yokai 已有：

```text
factory app partition
voice model partition
storage partition
```

Vision 模型不得覆盖现有语音模型 partition。

第一版优先：

```text
ESP-DL model embedded in app / rodata
```

然后检查：

```text
app size
<
factory partition
```

如果超出，再为视觉模型创建独立 partition。

禁止直接重用语音：

```text
model
```

partition，除非明确重新设计整个模型分区结构。

---

# 39. 错误降级

任何 Vision 错误都不得：

```text
abort()
ESP_ERROR_CHECK()
reboot whole system
```

除非确属项目其他地方统一约定的 fatal hardware fault。

Camera fail：

```text
VISION UNAVAILABLE
Camera initialization failed
```

Face model fail：

```text
FACE disabled
OBJECT may remain available
```

Object model fail：

```text
OBJECT disabled
FACE may remain available
```

Face DB corrupt：

```text
report DB error
do not crash
allow explicit reset DB
```

---

# 40. Diagnostics

建议：

```c
typedef struct {
    uint32_t frames_captured;
    uint32_t frames_displayed;
    uint32_t frames_dropped;

    uint32_t face_inferences;
    uint32_t object_inferences;

    uint32_t face_detected;
    uint32_t face_recognized;
    uint32_t face_unknown;

    uint32_t camera_errors;
    uint32_t inference_errors;

    uint32_t result_drops;

    uint32_t avg_face_ms;
    uint32_t avg_object_ms;

} vision_diag_t;
```

Debug 日志：

```text
每 5–10 秒输出一次
```

例如：

```text
VISION:
preview=19.8fps
face=7.9fps
face_ms=112
obj_ms=384
drop=14
heap=...
psram=...
```

禁止 per-frame log。

---

# 41. 内存诊断

每阶段必须记录：

```text
free heap
minimum free heap
free PSRAM
largest free block
```

至少测试：

```text
before Vision
Vision preview
FACE mode
OBJECT mode
leave Vision
```

退出后若 heap 持续下降：

```text
FAIL
```

必须先解决 leak，再进入下一阶段。

---

# 42. Task Priority 原则

优先级：

```text
Audio realtime
Voice realtime
LVGL / system critical
Camera capture
Vision inference
```

如果 Vision 造成：

```text
audio underrun
wake word miss
voice command latency spike
LVGL watchdog
```

首先处理 Vision。

禁止通过降低 Voice / Audio priority 解决 Vision 性能问题。

---

# 43. 性能回退顺序

当系统资源不足时，严格按以下顺序优化：

### 第一优先

降低：

```text
Vision inference frequency
```

### 第二优先

换更轻量：

```text
AI model
```

### 第三优先

降低：

```text
Camera preview FPS
```

### 第四优先

降低：

```text
Camera / inference resolution
```

禁止第一步就牺牲：

```text
Voice
Audio quality
LVGL responsiveness
```

---

# 44. 开发阶段

必须按以下阶段实现。

---

## Phase 1 — Vision Service Skeleton

修改：

```text
vision_service.cpp
vision_service.h

app_main.c
CMakeLists.txt
idf_component.yml
```

完成：

```text
vision_service_init()
vision_service_start()
vision_service_stop()
vision_service_set_mode()
```

此阶段：

```text
不启动真实 Camera
不加载 AI
```

验收：

```text
build success
no new warning
enter / exit Vision screen
other apps unaffected
```

---

## Phase 2 — Camera Preview

实现：

```text
Camera init
Camera start
frame acquire
frame release
LVGL preview
```

不接 AI。

验收：

```text
Preview >= 15 FPS target
连续 30 min
无 crash
无 DMA error
无 heap leak
离开页面 Camera stop
重复 enter/exit 20 次稳定
```

**在 Phase 2 稳定前禁止开始 Face / Object 模型。**

---

## Phase 3 — Frame Ownership / Buffer Stability

明确：

```text
Camera
Preview
Inference
```

之间的 buffer ownership。

测试：

```text
高 FPS
页面快速 enter / exit
快速 FACE / OBJECT 模式切换 mock
```

验收：

```text
无 use-after-free
无 double release
无 framebuffer leak
```

---

## Phase 4 — Human Face Detect

只接：

```text
HumanFaceDetect
```

不接 Recognition。

输出：

```text
face boxes
confidence
```

验收：

```text
单脸稳定检测
多人可返回多 box
没人时不持续产生错误框
box 坐标方向正确
mirror / rotation 正确
```

---

## Phase 5 — Human Face Recognition

加入：

```text
HumanFaceRecognizer
```

实现：

```text
Known face
Unknown
Face ID
Similarity / confidence
```

验收：

```text
录入前所有人 UNKNOWN
录入后本人可识别
陌生人保持 UNKNOWN
```

---

## Phase 6 — Face Enrollment / Database

增加：

```text
Enroll
Delete Last
Clear All
persistent DB
```

验收：

```text
Face 01
Face 02
Face 03
```

可以保存。

重启设备后：

```text
DB 仍存在
```

Clear All：

```text
必须二次确认
```

---

## Phase 7 — COCO Object Detection

加入：

```text
COCODetect
YOLO11n-320
```

UI：

```text
Bounding Box
Label
Confidence
```

验收至少：

```text
person
bottle
cup
chair
cell phone
book
```

选择常见物体测试。

不要求所有 COCO 类全部逐项验证。

---

## Phase 8 — FACE / OBJECT Switch

加入实际切换逻辑。

验证：

```text
FACE → OBJECT
OBJECT → FACE
```

重复：

```text
50 次
```

无：

```text
crash
memory leak
stale labels
old boxes
model race
```

---

## Phase 9 — Performance Optimization

测量：

```text
preview FPS
face inference ms
object inference ms
PSRAM
heap
CPU load
voice recognition behavior
audio underrun
```

只在有数据后优化。

---

## Phase 10 — Error Handling / Final UI

处理：

```text
Camera missing
Model allocation failed
DB error
PSRAM low
Inference failure
```

UI 不允许永久卡：

```text
Loading...
```

必须显示真实状态。

---

## Phase 11 — System Regression

同时运行：

```text
Vision App
Voice recognition
Synth
Bluetooth
LVGL touch
```

测试：

```text
30 min
```

验收：

```text
无 WDT
无音频爆音
无 Bluetooth dropout
Voice 保持可用
LVGL touch 正常
无持续内存下降
```

---

# 45. 推荐 Commit 划分

Agent 建议每个阶段独立 commit：

```text
Commit 1
vision service skeleton

Commit 2
camera adapter + raw preview

Commit 3
camera buffering + lifetime fixes

Commit 4
human face detection

Commit 5
human face recognition

Commit 6
face enrollment + persistent DB

Commit 7
COCO YOLO11n-320 detection

Commit 8
FACE / OBJECT switching

Commit 9
latest-frame scheduling + performance tuning

Commit 10
error handling + diagnostics

Commit 11
system regression fixes
```

每个 commit 必须：

```text
buildable
flashable
previous behavior preserved
```

禁止提交中间不可编译状态。

---

# 46. Camera Gate

AI 开发前必须满足：

```text
进入 Vision
→ Camera Preview

离开 Vision
→ Camera stopped

重复进入 20 次
→ no leak

连续 30 min
→ no crash

Voice
→ still works

Synth / BT
→ no regression
```

如果 Gate 未通过：

```text
STOP
```

不得继续 Face / Object AI。

---

# 47. Face Gate

必须达到：

```text
单人：
稳定 bounding box

多人：
multiple boxes

没人：
无持续 false box

移动：
box 可合理跟随

Enroll：
成功创建 Face ID

Restart：
Face DB 保留
```

---

# 48. Object Gate

至少识别多类常见 COCO 对象。

必须正确显示：

```text
box
label
confidence
```

不允许：

```text
box 和 label 属于不同 object
```

也不允许坐标镜像错误。

---

# 49. 系统 Gate

Task 完成前：

```text
Camera preview >= 15 FPS target

FACE detection >= 5 FPS target

Face identity latency <= 500 ms target

OBJECT >= 1 FPS
preferred 2–5 FPS

Vision App continuous >= 30 min

heap leak = 0

WDT reset = 0
```

以上 FPS / latency 均为工程目标。

如果 ESP32-S31 真机不能达到，则记录实际 benchmark，并按本规范的“性能回退顺序”优化。

不得伪造性能数据。

---

# 50. 测试矩阵

至少测试：

### Camera

```text
normal light
low light
moving scene
enter / exit
```

### Face

```text
1 face
2+ faces
no face
different distance
left/right orientation
registered
unregistered
```

### Object

```text
single object
multiple objects
object entering/leaving frame
large object
small object
```

### System

```text
Vision + Voice
Vision + Synth
Vision + Bluetooth
Vision + Touch
Vision + Drawer
```

---

# 51. 禁止事项

Agent 明确禁止以下实现：

```text
❌ inference task 直接调用 LVGL

❌ Camera ISR 调用 LVGL

❌ 每帧 malloc/free

❌ 无限长度 frame queue

❌ 同时执行 Face + Object 两套 inference

❌ Vision 使用语音 model partition

❌ 为 Vision 新建第二套 App Router

❌ Camera failure 导致整个 OS abort

❌ 用降低 Voice task priority 换取 AI FPS

❌ 未检查 BSP 就硬编码 camera GPIO

❌ 未检查 sensor 就假定 RGB565

❌ 把 ESP-WHO 整套 App 架构复制进 Yokai

❌ 每帧 create/delete LVGL overlay object

❌ 保存已 release 的 Camera buffer pointer

❌ UI 显示与实际 model result 不一致的虚构识别信息
```

---

# 52. 最终建议

整个 Task 最重要的实施建议是：

> **不要从“AI 模型”开始。先把 Camera Preview 做成一个稳定的 Yokai OS App，然后逐层加入 AI。**

严格按：

```text
Camera
↓
Preview
↓
Buffer Ownership
↓
Face Detect
↓
Face Recognition
↓
Face Enrollment / DB
↓
Object Detection
↓
Performance Tuning
↓
Full System Regression
```

推进。

第一版最重要的可运行里程碑不是“人脸 + YOLO 一次完成”，而是：

```text
打开「目目连视觉」
↓
Camera 实时画面稳定
↓
离开页面后 Camera 正确释放
↓
Voice / Audio / LVGL 不受影响
```

只有这个基线稳定后才加入 ESP-DL。

第二个里程碑：

```text
FACE
↓
能稳定框出人脸
↓
显示 Face ID / UNKNOWN
↓
支持 Enroll
```

第三个里程碑：

```text
OBJECT
↓
YOLO11n-320
↓
person / bottle / chair ...
↓
Bounding Box + Confidence
```

这样开发时每一层都有明确的故障边界。

如果直接同时加入：

```text
Camera
Face model
Face DB
YOLO
LVGL overlay
```

一旦出现：

```text
PSRAM crash
frame corruption
watchdog
wrong bounding box
camera DMA error
AI latency spike
```

将很难区分是哪一层导致。

---

# 53. Agent 最终交付要求

Agent 完成后必须输出：

```text
1. 修改文件列表

2. 新增依赖列表

3. Camera 实际硬件 / BSP 信息

4. 使用的 ESP-DL 模型名称

5. 模型存储方式

6. Face DB 路径

7. Task priority / affinity

8. Preview FPS 实测

9. Face inference latency 实测

10. Object inference latency 实测

11. Peak PSRAM usage

12. Minimum free heap

13. 30-minute stress test result

14. Voice / Audio regression result

15. 已知限制

16. 所有未完成项 / TODO
```

不得只回复：

```text
Implemented successfully
```

必须给出实际数据和改动摘要。

---

# 54. Agent 开始实施时的第一步

Agent 收到本规范后，第一步不是修改代码。

必须先：

```text
A. 检查仓库 Camera/BSP
B. 检查 UI_SCREEN_VISION
C. 检查 RTOS priority
D. 检查 PSRAM / partition / filesystem
E. 输出简短 implementation assessment
```

然后直接进入：

```text
Phase 1
Vision Service Skeleton

Phase 2
Camera Preview
```

**在 Camera Preview Gate 通过前，不允许实现 HumanFaceDetect / HumanFaceRecognizer / COCODetect。**

---

# 55. Definition of Done

只有以下全部满足，Task 5 才视为完成：

```text
[ ] Vision 页面稳定 Camera Preview

[ ] FACE 模式完成 Face Detect

[ ] FACE 模式完成 Face Recognition

[ ] Face Enroll 可用

[ ] Face DB 重启保持

[ ] Delete / Clear 可用

[ ] OBJECT 模式完成 COCO object detection

[ ] Bounding boxes 坐标正确

[ ] FACE / OBJECT 可重复切换

[ ] Vision exit 正确释放 Camera

[ ] 30 min stress test pass

[ ] 无持续 heap leak

[ ] 无 WDT reset

[ ] Voice recognition 无回归

[ ] Synth / Bluetooth audio 无回归

[ ] LVGL touch / UI 无回归

[ ] Build 无新增 warning

[ ] Agent 提供实际性能和内存测试数据
```

---

## 结束

本 Task 的优先顺序始终是：

```text
System Stability
>
Audio / Voice Realtime
>
UI Responsiveness
>
Camera Preview
>
AI Inference FPS
```

Vision 是 Yokai OS 中的一个 App，而不是新的系统核心框架。

所有设计和实现都应遵循这一原则。
