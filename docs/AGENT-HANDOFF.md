# Full-Function-S31-P4-Demo 开发交接

更新时间：2026-09-10

当前硬件：ESP32-S31-Korvo-1，800×480 触摸屏

当前工作分支：`feature/synth-groovebox` (基于 `feature/second-page-apps` 新建，用于 Synthesizer 重构)

## 接手时先做什么

1. 阅读本文件、`docs/superpowers/specs/2026-09-09-showa-yokai-korvo1-design.md` 和 `docs/superpowers/plans/2026-09-10-korvo1-next-development.md`。
2. 从 GitHub 拉取 `feature/second-page-apps`，不要从旧的 `main` 猜测当前状态。
3. 使用 `/Users/kongweilu/esp/esp-idf-master`。当前实测版本为 ESP-IDF 6.2.0；不要静默切换 SDK。
4. 先运行本文件的“复现构建”，再改代码。当前真机检查点必须继续可用。
5. 用户要求由用户判断重要选择。不要自行选定语音命令词、天气服务、P4X 屏幕规格或硬件声道映射。

## 产品目标

制作一个可通过 set-target 适配 ESP32-S31-Korvo-1、ESP32-S31 Mosaico 和 ESP32-P4X-Function-EV Board 的客户演示。优先把 Korvo-1 做完整。技术基线是 ESP-IDF master 与 ESP-GSP；端侧语音必须包含 AEC。

Demo 使用两页应用桌面，包含：

| 页 | App |
| --- | --- |
| 第一页 | Synthesizer、天气、端侧语音控制、端侧物体识别 |
| 第二页 | 屏幕烟花、时间与计时器、计算器、食材保质期管理器 |

所有桌面和 App 页面都必须支持顶部下拉快捷设置。实体或页面 Home 一键返回桌面第一页。待机时轮换同世界观图片。

## 视觉基线

风格是精细 16-bit 昭和掌机像素画与日本神话妖怪。深靛蓝 `#171c35` 和暖米白 `#f4e6bc` 是统一底色，朱红 `#de6651`、青绿 `#70af9a`、金色 `#dfb85d` 为主要强调色；每个 App 另有独立强调色。日文只用于真实功能和状态，不堆砌无关文字。

正文目标 18–22 px，标题 24–28 px，触控目标至少 44×44 px。像素素材使用清晰边缘和整数倍缩放。场景装饰避开操作区，文字、取景框、琴键和按钮必须是真实控件，不能把效果图整张烘焙成 UI。

原始设计与效果图：

- `design/yokai-v1/DESIGN.md`：颜色、排版、动效和实现注意事项。
- `design/yokai-v1/APP-SCENES.md`：八个 App、待机和 Mosaico 重排规则。
- `design/yokai-v1/images/01-home.png`：桌面第一页。
- `design/yokai-v1/images/02-quick-settings.png`：下拉设置。
- `design/yokai-v1/images/03-synthesizer.png`：雷神太鼓与琴键。
- `design/yokai-v1/images/04-weather-sunny.png`：天气晴天。
- `design/yokai-v1/images/05-weather-rain.png`：天气雨夜。
- `design/yokai-v1/images/06-standby.png`：待机月夜鸟居。
- `design/yokai-v1/images/07-home-2.png`：桌面第二页。

App 场景语义：シンセ为雷神太鼓；天気为妖怪村落；音声操作为言灵神社；物体認識为「目目連の観察室」；照明已改为提灯祭街中的屏幕烟花；時計・タイマー为狸猫钟屋；電卓为算盘付丧神；食材管理为河童厨房。

## 当前已完成并经用户真机确认

- Korvo-1 屏幕和 GT1151 触摸可初始化并稳定显示。
- 两页四宫格桌面可左右翻页；字体不再显示为 `?`。
- 八个 App 都能打开基本页面；App 页面横滑不会切换到其他 App。
- App 的「ホーム」可回到桌面第一页；桌面底部多余「ホーム」及其空白条已移除。
- 桌面和八个 App 页面都能从顶部下拉快捷设置；面板不会点击穿透底层。
- 为流畅度启用三 RGB framebuffer、8 ms active tick、16 ms pointer poll。
- Wi-Fi 与 Bluetooth 在快捷设置中各有独立小框：左侧 36×36 圆点单击切换，小框其余区域单击进入对应详情页。长按方案已经取消。
- Wi-Fi 和 Bluetooth 详情页都可进入；这一版已于 2026-09-10 编译、烧录并由用户确认交互符合需求。

## 已编译但仍需专项真机验证

- `board_ui.c` 使用 `esp_wifi` 异步扫描附近热点，在 Wi-Fi 详情页最多显示五条 SSID 与 RSSI，并支持“更新”。
- Wi-Fi 初始化使用 NVS、默认 event loop 和 STA netif。需要验证开关关闭后的语义、反复进入/扫描，以及任意字节 SSID 的显示。
- 场景 bundle 统一使用 Noto Sans CJK JP 字符集，避免新增日文后发生 `GSPC-RS-FONT-ORDER-CONFLICT`。

## 当前只是页面或尚未实现

- 两个无线圆点目前只改变 UI 控件状态，没有控制真实 Wi-Fi/Bluetooth，也没有跨场景同步。
- Bluetooth 详情页是静态页面，BLE 扫描和连接未实现。
- Synthesizer 没有真实音频；天气没有联网；语音没有运行 AFE/WakeNet/MultiNet/AEC；物体识别没有相机预览或推理。
- 屏幕烟花、时钟/计时器、计算器和食材管理主要是静态演示页面。
- 待机轮播尚未进入固件；只有一张设计效果图。
- Mosaico 与 P4X 尚未适配。

## 2026-09-10 Synthesizer 进展

- `korvo_synth_800.json` 的 8 个可见琴键已从静态容器改为 `synth_c4` 至 `synth_c5` 的真实按钮 callback。
- ESP-IDF 6.2 完整构建通过，镜像已烧录至 `/dev/cu.usbserial-1140`，写入 Hash 校验通过；复位后未观察到立即 panic。
- 当前只完成琴键输入事件层，尚未在 `board_ui.c` 分发这些 action，也尚未接入 `bsp_audio_codec_speaker_init()` / `esp_codec_dev_write()`，因此本版琴键还不会发声。
- Korvo-1 当前缺少可用的软件背光调节路径，亮度滑条禁用并显示「明るさ　固定」。音量滑条也尚未连接 codec。

## 2026-09-10 Wi-Fi 页面切换 crash 修复

- 复现路径：从下拉菜单进入 Wi-Fi，返回ホーム后再次进入 Wi-Fi，随后切换 Bluetooth；曾出现 Core 0 `Instruction access fault`（`MEPC/RA=0x0000000a`），同时历史日志出现 `canvas begin failed: -15`。
- 原因：Wi-Fi 扫描运行在异步任务中；离开 Wi-Fi 场景后任务仍可能更新已经切换的 UI，和场景渲染并发，破坏 UI 状态。
- 修复：为 Wi-Fi 扫描增加 generation；离开 Wi-Fi 场景即使当前扫描失效，扫描任务在启动扫描及写回 UI 前检查 generation 和当前页面，过期任务只退出、不再更新 UI。
- 验证：ESP-IDF 6.2 完整构建通过，镜像约 2.71 MiB、app partition 剩余约 55%；已烧录 `/dev/cu.usbserial-1140` 且 Hash 校验通过。真机连续密集切换 Wi-Fi、Bluetooth、ホーム约 70 秒，未再出现 panic 或 `canvas begin failed: -15`。

## 关键代码地图

| 路径 | 职责 |
| --- | --- |
| `firmware/korvo1_yokai_demo/main/app_state.h/.c` | 页面、Home 和语音状态模型 |
| `firmware/korvo1_yokai_demo/main/board_ui.c` | BSP 初始化、ESP-GSP 事件路由、Wi-Fi 扫描 |
| `firmware/korvo1_yokai_demo/main/app_main.c` | NVS 与 UI 启动 |
| `firmware/korvo1_yokai_demo/main/CMakeLists.txt` | 组件依赖、11 个 GSP 场景和局部编译兼容修复 |
| `firmware/korvo1_yokai_demo/scenes/korvo_home_800.json` | 两页桌面和快捷设置 |
| `firmware/korvo1_yokai_demo/scenes/korvo_*_800.json` | 八个 App、Wi-Fi、Bluetooth 场景 |
| `firmware/korvo1_yokai_demo/test/test_app_state.c` | 可在宿主机运行的导航状态测试 |
| `firmware/korvo1_yokai_demo/sdkconfig.defaults*` | PSRAM、三缓冲和 GSP 调度参数 |

ESP-GSP bundle 要求所有场景使用相同的有序字体包。新增日文字符时，必须同步更新全部场景顶层 `font_charset`；否则 pack 会报 `GSPC-RS-FONT-ORDER-CONFLICT`。当前隐藏的多字号 font palette 是为 15/16/18/20/22/24/28/30 px 保持字体顺序，不要只在一个场景删除。

## 复现构建

工作树当前位于：

```text
/Users/kongweilu/Development/Full Demo/.worktrees/second-page-apps
```

宿主状态测试：

```bash
cc -std=c11 -Wall -Wextra -Werror \
  -I firmware/korvo1_yokai_demo/main \
  firmware/korvo1_yokai_demo/main/app_state.c \
  firmware/korvo1_yokai_demo/test/test_app_state.c \
  -o /tmp/korvo1_app_state_test && /tmp/korvo1_app_state_test
```

固件构建：

```bash
source /Users/kongweilu/esp/esp-idf-master/export.sh >/dev/null
ninja --quiet -C build-korvo1-s31-second-page
```

当前构建产物：

```text
build-korvo1-s31-second-page/korvo1_yokai_demo.bin
```

已验证烧录端口为 `/dev/cu.usbserial-1140`，但端口号可能在重新插拔后变化。烧录布局：bootloader `0x2000`、partition table `0x8000`、app `0x10000`、srmodels `0x610000`。最近一次主镜像约 2.71 MiB，6 MiB app partition 仍有约 55% 空间。

## 开发顺序

用户要求先着重完成第一页四个 App。顺序建议：Synthesizer → 天气 → 语音/AEC → 物体识别。公共 Wi-Fi/Bluetooth 真实状态可随天气和音频接入一并收口；之后完成第二页业务、待机、Mosaico 和 P4X。完整任务与验收见 `docs/superpowers/plans/2026-09-10-korvo1-next-development.md`。

每次修改遵循：最小可运行检查 → ESP-GSP pack/完整构建 → 真机烧录 → 用户确认。不要把构建成功写成真机功能已确认。用户此前要求不要每次自动推送 GitHub；只有收到“同步/推送”指示时再推送。

## 已知历史问题，避免回归

- 缺少 CJK 字形会整屏显示 `?`。
- 错误的触摸/父级布局曾导致桌面滑动卡死、App 无法打开和设置面板点击穿透。
- App 场景开启 swipe 会导致第二页 App 之间横滑串页。
- RGB panel 初始化参数改动曾导致黑屏；保留当前 BSP 初始化路径。
- 桌面底部 Home 被删除后必须同时回收布局高度，不能留下空白条。
- Wi-Fi/Bluetooth 长按事件在真机无法可靠进入详情页；当前确认方案是小框单击。
