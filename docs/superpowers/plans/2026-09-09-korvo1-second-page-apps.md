# Korvo-1 第二页应用场景 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 让桌面第二页的照明、时钟／计时器、电卓和食材管理入口进入各自的 GSP 页面，并能返回桌面。

**Architecture:** 为现有 bundle 增加四个 800×480 场景；第二页 callback 在 `board_ui.c` 映射到新场景和 `app_state` 事件。页面仅展示静态数据与轻量视觉元素。

**Tech Stack:** ESP-IDF master、ESP-GSP 1.2、C11、Noto Sans CJK JP。

---

### Task 1: 扩展应用状态和单元测试

**Files:**
- Modify: `firmware/korvo1_yokai_demo/main/app_state.h`
- Modify: `firmware/korvo1_yokai_demo/main/app_state.c`
- Modify: `firmware/korvo1_yokai_demo/test/test_app_state.c`

- [x] **Step 1: 写入失败用例**

```c
const app_event_t events[] = {
    APP_EVENT_OPEN_LIGHTING, APP_EVENT_OPEN_CLOCK_TIMER,
    APP_EVENT_OPEN_CALCULATOR, APP_EVENT_OPEN_FOOD,
};
const app_screen_t screens[] = {
    APP_SCREEN_LIGHTING, APP_SCREEN_CLOCK_TIMER,
    APP_SCREEN_CALCULATOR, APP_SCREEN_FOOD,
};
```

循环 dispatch 每个 event，断言 screen 与 screens 对应；再 dispatch `APP_EVENT_HOME`，断言 `APP_SCREEN_HOME`。

- [x] **Step 2: 运行失败测试**

Run: `cc -std=c11 -Wall -Wextra -Werror -I firmware/korvo1_yokai_demo/main firmware/korvo1_yokai_demo/test/test_app_state.c firmware/korvo1_yokai_demo/main/app_state.c -o /tmp/korvo1_app_state_test && /tmp/korvo1_app_state_test`

Expected: 因四个 `APP_EVENT_OPEN_*` 尚未定义而失败。

- [x] **Step 3: 最小实现**

在 `app_event_t` 追加 `APP_EVENT_OPEN_LIGHTING`、`APP_EVENT_OPEN_CLOCK_TIMER`、`APP_EVENT_OPEN_CALCULATOR`、`APP_EVENT_OPEN_FOOD`。在 `app_state_dispatch()` 分别设为对应 `APP_SCREEN_*` 并关闭 `quick_settings_open`。

- [x] **Step 4: 运行通过测试**

Run: 同 Step 2。

Expected: exit code 0。

- [x] **Step 5: 提交**

```bash
git add firmware/korvo1_yokai_demo/main/app_state.c firmware/korvo1_yokai_demo/main/app_state.h firmware/korvo1_yokai_demo/test/test_app_state.c
git commit -m "feat: add second-page app state events"
```

### Task 2: 创建四个主题场景

**Files:**
- Create: `firmware/korvo1_yokai_demo/scenes/korvo_lighting_800.json`
- Create: `firmware/korvo1_yokai_demo/scenes/korvo_clock_timer_800.json`
- Create: `firmware/korvo1_yokai_demo/scenes/korvo_calculator_800.json`
- Create: `firmware/korvo1_yokai_demo/scenes/korvo_food_800.json`
- Modify: `firmware/korvo1_yokai_demo/main/CMakeLists.txt`

- [x] **Step 1: 建立公共场景骨架**

每个文件使用 800×480、已有 font palette、24px 标题、18px `ホーム` 按钮、深靛蓝背景和黑场淡入淡出所需的既有导航 callback。

- [x] **Step 2: 写入特有内容**

| 文件 | 标题 | 可见内容 |
| --- | --- | --- |
| lighting | `照明　提灯の祭り通り` | `金の花火　青の花火　紫の花火`、三组提灯色卡和烟花点阵 |
| clock_timer | `時計・タイマー　狸の時計屋` | `09:41`、`残り 12:00`、`次の知らせ　10:00` |
| calculator | `電卓　算盤の付喪神` | `1,280` 与 `7 8 9 ÷ / 4 5 6 × / 1 2 3 − / 0 C ＝ ＋` |
| food | `食材管理　河童の台所` | `みそ　あと 18日`、`豆腐　あと 2日`、`牛乳　期限切れ` |

优先使用 label、container 边框、渐变和已有 `●` 字形；不加入全屏 JPEG/PNG。

- [x] **Step 3: 注册场景**

在 `gsp_add_bundle` 追加：

```cmake
../scenes/korvo_lighting_800.json
../scenes/korvo_clock_timer_800.json
../scenes/korvo_calculator_800.json
../scenes/korvo_food_800.json
```

- [x] **Step 4: 校验**

Run: `python3 -m json.tool firmware/korvo1_yokai_demo/scenes/korvo_lighting_800.json >/dev/null`，对四个文件重复。

Expected: 四个 JSON 均有效。

- [x] **Step 5: 提交**

```bash
git add firmware/korvo1_yokai_demo/main/CMakeLists.txt firmware/korvo1_yokai_demo/scenes
git commit -m "feat: add four Yokai second-page scenes"
```

### Task 3: 连接第二页导航

**Files:**
- Modify: `firmware/korvo1_yokai_demo/scenes/korvo_home_800.json`
- Modify: `firmware/korvo1_yokai_demo/main/board_ui.c`

- [x] **Step 1: 添加桌面 callback**

按四个第二页按钮顺序添加 `open_lighting`、`open_clock_timer`、`open_calculator`、`open_food` callback。

- [x] **Step 2: 添加场景映射**

在 `board_ui_event()` 的 home switch 中添加四个 case，模式为：

```c
board_ui_open_scene(ui, state, APP_EVENT_OPEN_LIGHTING,
                    GSP_BUNDLE_SCENE_KORVO_LIGHTING);
```

其余三项替换事件和生成的 GSP scene enum。

- [x] **Step 3: 添加返回映射**

将四个新场景生成的 `GSP_KORVO_*_ACTION_HOME` 纳入现有 Home 条件，目标为 `GSP_BUNDLE_SCENE_KORVO_HOME`。

- [x] **Step 4: 构建**

Run: `source /Users/kongweilu/esp/esp-idf-master/export.sh >/dev/null && idf.py --preview -C firmware/korvo1_yokai_demo -B build-korvo1-s31 build`

Expected: 生成 `korvo1_yokai_demo.bin`，无 action/scene 未定义错误。

- [x] **Step 5: 提交**

```bash
git add firmware/korvo1_yokai_demo/main/board_ui.c firmware/korvo1_yokai_demo/scenes/korvo_home_800.json
git commit -m "feat: connect second-page app navigation"
```

### Task 4: 真机验证与同步（已完成）

**Files:**
- Modify: `docs/superpowers/plans/2026-09-09-korvo1-second-page-apps.md`

- [x] **Step 1: 烧录**

使用已验证的 921600 波特率 Stub 烧录方式写入 `build-korvo1-s31/korvo1_yokai_demo.bin`，并确认主镜像输出 `Hash of data verified`。

- [x] **Step 2: 真机检查**

从桌面滑至第二页，依次进入四页，确认标题、主题区域、日文文本和 `ホーム` 返回。照明页只显示屏幕烟花，不驱动外设。

- [x] **Step 3: 同步**

将完成的 checkbox 标记为 `[x]`，然后执行：

```bash
git add docs/superpowers/plans/2026-09-09-korvo1-second-page-apps.md
git commit -m "docs: record second-page app validation"
git push
```

