# 下一阶段推进顺序

## 已完成与当前限制
用户已接受首轮视觉方向，现有七张图覆盖两页桌面、下拉、合成器、两种天气和一个待机画面。Korvo-1 固件工程已经建立，两页桌面、八个基础 App 页面、全局下拉设置、Home 和无线详情入口已完成真机检查。当前限制和下一步以 `docs/AGENT-HANDOFF.md` 为准。

## 1. 完成视觉交付
按 APP-SCENES.md 生成语音、识别、灯光、计时器、计算器、食材页面，再补状态变化、两幅待机和 Mosaico 480×480 重排。以现有原创图作为风格参考。先修正黑键、装饰字和滑条几何问题，避免直接复制生成图的错误。

## 2. 工程基线验证（可先准备）
当前已由用户选择 `/Users/kongweilu/esp/esp-idf-master`，实测版本 ESP-IDF 6.2.0。后续 Agent 不得静默切换 SDK。
ESP-GSP 候选版本为正式 Registry espressif/esp-gsp 1.2.0；先下载并验证 hello_world 后锁版本，同时锁组件指定的 GSPC 与 simulator 版本。不继承旧 staging 0.2.7。
先验证主机模拟器最小示例，再验证 Korvo-1 显示和触摸；通过后才制作自定义 bundle。

## 3. 第一个固件里程碑（已完成）
800×480 Korvo-1 UI 外壳：两页桌面、八个入口、Back/Home、下拉亮度、待机与唤醒。未接后端的开关明确不可用。真实文字/控件与背景资产分离。导航状态与板级驱动分离。
验收：编译成功、场景可模拟、真机显示触控正常、快捷面板不穿透、Home 回第一页、待机触摸不误触、连续切换无崩溃。记录帧耗时、内存和输入延迟，不由静态图推断性能。

## 4. 业务接入顺序
用户要求优先完成第一页：合成器与音量 → Wi-Fi/SNTP/天气 → 端侧语音与 AEC → 物体识别。之后完成屏幕烟花、计时器/计算器和食材持久化。每项验收后再进入下一项；音频、相机和推理必须验证后台资源释放。

## 5. 多板适配
set-target 选择 esp32s31 或 esp32p4；独立 board profile 区分 Korvo-1、Mosaico 和 P4X。每板独立构建目录，共享业务逻辑，布局与驱动分开。Mosaico 的摄像头扩展、外接灯带及 P4X 屏幕/板卡版本在相应适配开始前确认，不阻塞 Korvo-1。

## 官方检索依据（2026-09-08，尚未构建验证）
- https://components.espressif.com/components/espressif/esp-gsp
- https://github.com/espressif/esp-idf/releases/tag/v6.1
- https://esp32-s31.espressif.com/en/faq#faq-heading-idf
- https://github.com/espressif/esp-idf/blob/master/COMPATIBILITY.md
