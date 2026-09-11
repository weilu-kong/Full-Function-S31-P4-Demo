# Korvo-1 Wi-Fi / Bluetooth 下拉快捷设置 Bug 调查与交接文档

> **文档目的**：供后续接手开发或调试的 AI Agent / 工程师直接理解当前系统现状、三个关键 Bug 的现象、底层技术原理、前序已尝试的修复方案及踩坑原因（避坑指南），并提供明确的排查切入点。

---

## 1. 项目基础与环境上下文 (Context & Environment)

| 项 | 说明 |
| :--- | :--- |
| **项目路径** | `/Users/kongweilu/Development/Full Demo/.worktrees/synth-groovebox/firmware/korvo1_yokai_demo` |
| **Git 分支** | `feature/synth-groovebox` |
| **目标硬件** | ESP32-S31-Korvo-1 开发板，800×480 RGB LCD 屏幕，GT911/GT1151 电容触摸屏 |
| **烧录端口** | `/dev/cu.usbserial-1140`（波特率 460800） |
| **ESP-IDF** | ESP-IDF master (v6.2.0)，路径 `/Users/kongweilu/esp/esp-idf-master` |
| **Python 环境** | `/Users/kongweilu/.espressif/python_env/idf6.2_py3.14_env` |
| **UI 渲染框架** | 乐鑫自研 `esp-gsp`（组件路径：`managed_components/espressif__esp-gsp`） |
| **编译输出目录** | `build-korvo1-s31-synth` |

### 核心构建与烧录命令

```bash
# 1. 编译环境加载与构建
source /Users/kongweilu/esp/esp-idf-master/export.sh 2>/dev/null
cd "/Users/kongweilu/Development/Full Demo/.worktrees/synth-groovebox/firmware/korvo1_yokai_demo"
idf.py -B build-korvo1-s31-synth build

# 2. 固件烧录 (请确认端口 /dev/cu.usbserial-1140)
BUILD="/Users/kongweilu/Development/Full Demo/.worktrees/synth-groovebox/firmware/korvo1_yokai_demo/build-korvo1-s31-synth"
/Users/kongweilu/.espressif/python_env/idf6.2_py3.14_env/bin/python -m esptool \
  --chip esp32s31 -p /dev/cu.usbserial-1140 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  0x2000 "$BUILD/bootloader/bootloader.bin" \
  0x8000 "$BUILD/partition_table/partition-table.bin" \
  0x10000 "$BUILD/korvo1_yokai_demo.bin"
```

---

## 2. 涉及的 UI 架构与组件设计

### 2.1 快捷设置抽屉（`quick_settings_drawer`）
在当前固件中，屏幕顶部向下滑动可拉出快捷设置抽屉（`quick_settings_drawer`，对象 key 为 `GSP_OBJ_KEY_QUICK_SETTINGS_DRAWER`）。
该抽屉存在于所有标准业务场景中（共 10 个场景 JSON，从 `korvo_home_800.json` 到 `korvo_synth_800.json` 等）。

抽屉内部关于 Wi-Fi 与 Bluetooth 的 UI 组成：
1. **Wi-Fi 开关控件**：
   - 控件类型：`toggle`
   - 组件名称：`name: "wifi_enabled"`
   - 触发回调：`callback: "wifi_toggle"`
2. **Wi-Fi 按钮方框（卡片）**：
   - 控件类型：`button`
   - 组件名称：`name: "wifi_card"`
   - 标签文本：`text: "Wi-Fi 通信"`
   - 触发回调：`callback: "wifi_details"`
3. **Bluetooth 开关控件**：
   - 控件类型：`toggle`
   - 组件名称：`name: "bluetooth_enabled"`
   - 触发回调：`callback: "bluetooth_toggle"`
4. **Bluetooth 按钮方框（卡片）**：
   - 控件类型：`button`
   - 组件名称：`name: "bluetooth_card"`
   - 标签文本：`text: "Bluetooth"`
   - 触发回调：`callback: "bluetooth_details"`

### 2.2 详细设置页面（场景 Scene）
- Wi-Fi 详细列表页：`GSP_BUNDLE_SCENE_KORVO_WIFI`（scene 9，对应 `scenes/korvo_wifi_800.json`，可异步扫描周围 Wi-Fi 列表）
- Bluetooth 详细页：`GSP_BUNDLE_SCENE_KORVO_BLUETOOTH`（scene 10，对应 `scenes/korvo_bluetooth_800.json`）

---

## 3. 当前三个 Bug 的准确描述与预期行为

### Bug 1：Wi-Fi 和 蓝牙的选择方框“常亮”【核心 Bug】
- **实际现象**：
  在快捷设置抽屉中，右侧的“Wi-Fi 通信”和“Bluetooth”两个选择方框，无论开关处于打开（ON）还是关闭（OFF），在真机屏幕上始终呈现**高亮浅蓝色的外框**（`#3B82F6`），视觉上永远是“亮的”。
- **预期行为**：
  1. 当左侧开关为 **OFF（关闭）** 时：方框应当整体**变暗**（Dimmed/Disabled），不可呈现高亮色；**变暗时不能被点击/选中**（触摸无响应）。
  2. 当左侧开关为 **ON（打开）** 时：方框应当**变亮**（Active/Highlighted，呈现醒目的蓝色背景或激活高亮），**亮起时允许点击**。

### Bug 2：点击方框无法调出详细设置页面【核心 Bug】
- **实际现象**：
  无论方框处于亮起还是变暗状态，在真机触摸屏上点击“Wi-Fi 通信”或“Bluetooth”方框，**都无法跳转到对应的详细设置页面**（界面无任何反应，抽屉不关闭也不切场景）。
- **预期行为**：
  当方框处于亮起（ON）状态时，点击“Wi-Fi 通信”应关闭抽屉并平滑跳转至 Wi-Fi 列表页（`korvo_wifi`）；点击“Bluetooth”应跳转至蓝牙设置页（`korvo_bluetooth`）。返回时能正确回到原场景。

### Bug 3：开关切到左边偶尔仍为绿色【偶发状态不同步】
- **实际现象**：
  拨动或点击开关切到左侧（OFF）时，偶尔开关内部滑块移到了左边，但开关槽位（轨道）依然显示绿色；或者物理显示与 C 语言内部保存的状态变量发生反转。

---

## 4. 历次修复尝试及踩坑记录（Avoid Past Mistakes）

在最近的几次提交和修复中，已经进行了多次调试尝试。后续 Agent **切勿重复踩入以下已知陷阱**：

### 踩坑 1：直接调用 `esp_gsp_component_set_color` 传入 24 位颜色引发断言失败
- **尝试动作**：
  在 `board_ui.c` 中直接使用 `esp_gsp_component_set_color(ui, GSP_OBJ_KEY_WIFI_CARD, 0x002563EB)` 试图修改方框背景色。
- **踩坑后果**：
  串口直接打印 `RGB565 update color exceeds 16 bits` 并丢弃操作。
- **根本原因**：
  GSP 渲染管线在 `sdkconfig` 中配置为 `CONFIG_ESP_GSP_PIXEL_FORMAT_RGB565=y`。GSP 的底层 API 强制检查传入颜色值必须 `<= 0xFFFF`。若传入 24-bit RGB888（例如 `0x2563EB`），会被底层断言拒绝。

### 踩坑 2：利用 GSP `bind` 机制 (`bind_target: "color"`) 导致“方框依然常亮”
- **尝试动作**：
  在所有 10 个场景 JSON 中为 `wifi_card` 添加 `"bind": "wifi_card_bg", "bind_target": "color"`，并在 C 代码中调用 `esp_gsp_set_color(ui, GSP_BIND_WIFI_CARD_IDX, 0x231D / 0x10E5)`（RGB565 颜色）。
- **踩坑后果**：
  用户真机测试反馈：方框依然是常亮的！
- **根本原因**：
  1. 仔细观察 `scenes/korvo_home_800.json` 中 `wifi_card` 的定义：
     ```json
     "border_color": "#3B82F6",
     "border_width": 2,
     ```
     `bind_target: "color"` 在 GSP 内部**只驱动 `bg_color`（填充色），根本不会改变 `border_color`（边框描边颜色）**！
  2. 哪怕背景色变成了暗色 `0x10E5`（`#141C2B`），外围那圈宽达 2px 的 `#3B82F6` 亮蓝边框依然原封不动地绘制在屏幕上！这就是为什么用户看到的“方框”（外框）永远是常亮蓝色的！

### 踩坑 3：状态门禁 `if (!s_wifi_enabled) return false;` 导致“详细页无法调出”
- **尝试动作**：
  在 `is_wifi_details_event(const esp_gsp_event_t *event)` 中添加了判断：
  ```c
  if (!s_wifi_enabled) {
      return false;  /* OFF: card tap is a no-op */
  }
  ```
- **踩坑后果**：
  用户点击卡片完全无响应，无法打开设置页面。
- **根本原因**：
  1. 全局静态变量初始为 `static bool s_wifi_enabled = false;`。
  2. GSP 的 `toggle` 控件在用户点击时，会触发带有动画过程的回调。之前的 Agent 尝试通过 `event->arg > 50` 读取状态，但实际上 GSP 的 `event->arg` 在动画完成时传递的通常都是动画进度 `100`，而不是开关的真假值！
  3. 当改成 `s_wifi_enabled = !s_wifi_enabled` 后，由于用户点击开关时事件可能在按下和抬起触发，或者在初次同步前状态反向，导致在用户眼中开关“打开”了，但在 C 逻辑里 `s_wifi_enabled` 依然是 `false`！
  4. 于是用户点击卡片时，被 `if (!s_wifi_enabled) return false;` 一票否决，根本无法进入 `board_ui_open_scene(..., GSP_BUNDLE_SCENE_KORVO_WIFI)`！

### 踩坑 4：在事件回调中调用 `esp_gsp_component_set_checked` 诱发二次伪事件
- **尝试动作**：
  在收到 `wifi_toggle` 事件或在 `update_drawer_quick_controls()` 中主动调用 `esp_gsp_component_set_checked(ui, GSP_OBJ_KEY_WIFI_ENABLED, s_wifi_enabled)`。
- **踩坑后果**：
  调用 `set_checked` 可能会再次触发 `toggle` 的 `callback`，形成死循环或状态抖动，导致开关偶尔拨到左边仍然显示为绿色。

---

## 5. 关键代码与定义定位 (Code Map)

### 5.1 C 源码文件
- `firmware/korvo1_yokai_demo/main/board_ui.c`
  - `s_wifi_enabled` / `s_bluetooth_enabled`：布尔状态
  - `is_wifi_details_event()` / `is_bluetooth_details_event()`：判断是否点击了详情卡片
  - `is_wifi_toggle_event()` / `is_bluetooth_toggle_event()`：判断是否拨动了开关
  - `sync_wifi_controls()` / `sync_bluetooth_controls()`：同步 UI 视觉状态
  - `board_ui_open_scene()`：关闭抽屉并切场景（`esp_gsp_goto_scene`）
  - `board_ui_event()`：主事件分发入口，处理 `ESP_GSP_EVENT_CALL` 与 `ESP_GSP_EVENT_SCENE_CHANGED`

### 5.2 场景 JSON 文件（共 10 个场景内嵌抽屉）
- `firmware/korvo1_yokai_demo/scenes/korvo_home_800.json`
- `firmware/korvo1_yokai_demo/scenes/korvo_synth_800.json`
- `firmware/korvo1_yokai_demo/scenes/korvo_weather_800.json`
- `firmware/korvo1_yokai_demo/scenes/korvo_voice_800.json`
- `firmware/korvo1_yokai_demo/scenes/korvo_object_800.json`
- `firmware/korvo1_yokai_demo/scenes/korvo_lighting_800.json`
- `firmware/korvo1_yokai_demo/scenes/korvo_clock_timer_800.json`
- `firmware/korvo1_yokai_demo/scenes/korvo_calculator_800.json`
- `firmware/korvo1_yokai_demo/scenes/korvo_food_800.json`

### 5.3 GSP 生成头文件
- `firmware/korvo1_yokai_demo/build-korvo1-s31-synth/esp-idf/main/gsp_gen_bundle/bundle_gsp.h`
  - 定义了各个场景的 Action ID（如 `GSP_KORVO_HOME_ACT_ID_WIFI_DETAILS`、`GSP_KORVO_HOME_ACT_ID_WIFI_TOGGLE`）
  - 定义了各个组件的 Key（如 `GSP_OBJ_KEY_WIFI_CARD`、`GSP_OBJ_KEY_WIFI_ENABLED`）
  - 定义了场景枚举（`GSP_BUNDLE_SCENE_KORVO_HOME=0`, `GSP_BUNDLE_SCENE_KORVO_WIFI=9`, `GSP_BUNDLE_SCENE_KORVO_BLUETOOTH=10`）

---

## 6. 给后续 Agent 的权威解决思路建议

根据 `managed_components/espressif__esp-gsp/docs/zh-Hans/reference/widget-inventory.md` 中的官方规范，推荐使用以下体系化方案解决根本问题：

### 建议 1：全面启用 GSP 原生 `enabled` 与禁用态（Disabled State）
GSP 原生对 `button` 控件支持以下属性：
```json
{
  "type": "button",
  "name": "wifi_card",
  "enabled": false,
  "disabled_opacity": 90,
  "disabled_color": "#1A2035",
  "border_color": "#283446",
  "border_width": 1
}
```
**原生机制的巨大优势**：
1. **自动变暗**：当在 C 语言中调用 `esp_gsp_component_set_enabled(ui, GSP_OBJ_KEY_WIFI_CARD, false)` 时，GSP 会自动对该按钮（**包括其边框、背景、文字**）应用 `disabled_opacity` 和 `disabled_color`，实现彻底变暗，绝不会留下亮蓝刺眼的边框！
2. **自动禁用点击**：GSP 底层对 `enabled == false` 的控件**直接阻断触摸命中测试（Hit-testing）**！在禁用状态下，用户点击方框根本不会产生任何事件。这意味着 C 代码中根本不需要脆弱的手工门禁判断，框架天然保证“变暗时不可被选中”。
3. **边框色设计**：如果希望亮态有高光边框，也可以在激活态通过动态样式或保持中性边框；重点是暗态在 `disabled_opacity: 90` 遮罩下会自动融入背景暗色。

### 建议 2：开关与卡片状态的精准单向联动
1. 开关点击事件：
   - 当用户点击 toggle 时，GSP 内部完成从 OFF 到 ON 或 ON 到 OFF 的切换。
   - 在 C 代码响应 `is_wifi_toggle_event` 时，可通过官方 API `esp_gsp_component_get_checked(ui, GSP_OBJ_KEY_WIFI_ENABLED, &checked)` 获取当前真实状态（如果动画进行中读取有滞后，可验证在回调时的确切读数，或者保持由单一状态源管理）。
   - 确认状态后，**只单向调用**：
     `esp_gsp_component_set_enabled(ui, GSP_OBJ_KEY_WIFI_CARD, s_wifi_enabled);`
     **切勿在事件处理中反向调用 `set_checked`**，防止打乱 GSP 的 toggle 动画状态机！

### 建议 3：诊断点击跳转详细页的步骤
后续 Agent 排查 Bug 2 时，请遵循以下诊断路径：
1. 烧录后打开串口监视器，观察用户点击 `wifi_card` 时，终端是否输出了 `board_ui_event` 的日志：
   ```c
   ESP_LOGI(TAG, "EVT: scene=%d, action=%d, type=%d, arg=%u", event->scene_id, event->action_id, event->type, (unsigned)event->arg);
   ```
2. 确认事件是否被识别：
   - 检查 `is_wifi_details_event(event)` 是否返回 true；
   - 确保没有被旧的 `if (!s_wifi_enabled)` 误杀；
   - 检查 `event->scene_id` 与各场景宏定义是否匹配（注意当前打开抽屉时所在的场景可能是 Home、Synth 或 Weather，需确认各个场景中的 `wifi_details` action ID 是否均在 switch-case 范围内）。
3. 检查场景切换逻辑：
   - `board_ui_open_scene` 中必须先关闭抽屉：
     `esp_gsp_drawer_close(ui, GSP_OBJ_KEY_QUICK_SETTINGS_DRAWER, false);`
   - 然后调用 `esp_gsp_goto_scene(ui, GSP_BUNDLE_SCENE_KORVO_WIFI, ESP_GSP_FADE_THROUGH_BLACK);`，检查返回值是否为 `ESP_GSP_OK`。
