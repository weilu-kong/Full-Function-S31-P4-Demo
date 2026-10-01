# Yokai OS 质量与资源改良记录（2026-10-01）

按已批准的审计顺序实施：先异常路径与状态修复，再隐藏资源释放和可选测量，最后补有界 Food 与发布检查。保留用户确认的 400×300 PPA 预览，不引入额外识别模型或第三方依赖。

## 已实现

- 语音运行错误可靠送达 UI；失败实例停用并由生命周期所有者清理/重启。播放参考环形缓冲在同一锁内检查、访问、脱离，避免释放竞态。
- 蓝牙 OFF 与迟到的连接、音频状态、格式回调共用原 spinlock 发布状态和 epoch；GAP/断连调用在锁外。扫描模式异步调用被开关操作超越时重新提交当前状态。
- Wi-Fi 检查 SDK 返回值；连接前断开旧关联；取消扫描不提前释放任务所有权。32 字节 SSID/64 字节密码使用有界处理，天气启动复用相同连接接口。
- 天气校验 JSON 类型、数值范围与天气码；保留上次有效数据并区分刷新、失败和过期。断网时使旧请求失效。
- Calculator 修正输入运算符后切换正负号；Timer 的完成事件在全局 UI tick 消费一次并提醒。
- Vision 启动失败可见；删除/清空人员先完成持久化，UI 收到完成结果再刷新；队列满、写入失败保留旧数据。
- Food 支持最多 32 条、48 字节 UTF-8 名称和 2020–2099 到期日期，增加/编辑/确认删除、六行分页。两份 1936 字节 CRC/代次快照交替保存，损坏较新快照回退旧快照；两份均损坏时阻止覆盖。离开后释放列表和编辑器。
- 移除物体识别的虚构置信度，明确标为未支持。
- 新增统一主机检查、Flash 预留门槛与 GitHub Actions；SDK 锁定 ESP-IDF 6.1 提交 `fff9895c82d744c7237be8847347bdd1b07c6643`，保留原组件锁版本。

## 已验证及资源预算

独立代码审查发现并推动修复了两项实际问题：蓝牙状态发布与 OFF 的竞态、天气启动读取固定宽度 SSID 后越界记录。修复后的真实 C 函数交错/错误注入检查通过，音频修复经再次独立审查未发现新的阻塞问题。

`python3 test/run_host_checks.py` 全部通过，包括计算器、时钟、Fireworks、有界 Food、存储失败/损坏、语音生命周期、蓝牙回调交错、Wi-Fi SDK 错误/满长度字段、天气 JSON/状态/页面、Vision 持久化/管理 UI、相机 DMA、PPA 与缓冲所有权。Food/Wi-Fi/相关 UI 边界检查使用 ASan/UBSan；页面资源检查包含 20 次进入/退出。生产 ESP-IDF 6.1 构建与 `git diff --check` 通过。

| 项目 | 数据/限制 |
| --- | --- |
| 上版应用镜像 | 10,563,344 字节 |
| 本轮生产镜像（最终报告补充校验值） | 10,573,488 字节，增加 10,144 字节 |
| 应用分区 | 12,058,624 字节；余 1,485,136 字节，约 1.416 MiB |
| 项目应用预留政策 | 至少 1 MiB；正常检查通过，要求 2 MiB 的负向检查正确拒绝 |
| 语音模型镜像 | 3,052,231 / 3,670,016 字节，余 617,785 字节 |
| 天气背景 | 不再启动时解码，离开 Weather 后释放 768,000 字节（750 KiB） |
| Home 背景 | 两份仍常驻，共 1,536,000 字节 |
| Food 服务 | 两份静态快照共 3,872 字节内部 BSS，另有小量状态/UI 指针；无服务堆缓冲、新任务 |
| CPU 诊断 | 仅单独配置启用；固定最多 40 个任务，约 2.5 KiB 静态数据；生产没有这些缓冲 |
| 放大预览 | 400×300 PPA 写入已有 LCD draw buffer，三份 320×240 预览缓冲保持不变 |

750 KiB 是代码分配与主机生命周期检查确认的释放大小，不等于尚未测量的实时可用/最大连续块增长；LVGL、模型与其他分配会影响实板数字。旧版 Vision 最低 PSRAM 约 233 KiB、内部 RAM 约 32 KiB，因此新增代码仍须关注内部 RAM，不能只看总 PSRAM。

## 实板与集成记录

代码已推送到 `codex/vision-ai`；[集成 PR #2](https://github.com/weilu-kong/Full-Function-S31-P4-Demo/pull/2) 已创建，未合并。CI 作业级 env 的 runner 上下文已按 GitHub 规则改为 github.workspace；远端实际构建暴露 PPA 适配器引用弃用 LVGL 头文件，现有严格补丁流程已修正该引用，新 CI 已完成实际固件构建，随后暴露 host 测试中的 macOS 专用 `/private/tmp` 路径；现已改为跨平台 `/tmp`，本地主机回归再通过，最终远端检查待确认。

诊断固件已成功烧录、校验。但板上 HTTPS 请求在 `weather_worker` 触发 Stack protection fault：原 4096 字节任务栈耗尽。解码现场落在 ESP SHA/HMAC 与 TLS PRF 调用，尚未获得有效 Vision CPU 区间；不能宣称诊断版稳定或完成 CPU 余量测量。现已将天气任务栈改为 8192 字节，额外占用 4096 字节内部 RAM，本地构建/主机检查通过，**尚须实板复测**。设备恢复本轮前 `72d41b0` 稳定应用，保留 NVS、模型和用户存储；串口采集已停止。

当前本地产物 SHA256（改良版，非设备回滚版）：`9aa48c8b36a94080bf52ae2efee9a531cf0ee7cc59aa688b3cb6206b1e08a651`。重新编译时 Git 版本元数据可能改变校验值，应以交付产物实际哈希为准。主机错误注入与编译不能替代按键视觉、蓝牙听感和实际混合负载验证。

CPU 诊断使用现有 health 任务，每 5 秒读取原生 `uxTaskGetSystemState`。任务 CPU 百分比以双核总时间为分母；每核 idle/busy 单独显示。第一份快照、新任务/重置计数、超过 40 任务容量明确报告 unknown；不继续使用原 LVGL sysmon 的无效 100% 来推断余量。参考 [ESP-IDF FreeRTOS 文档](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s31/api-reference/system/freertos_idf.html) 与 [Heap 调试文档](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s31/api-reference/system/heap_debug.html)。

## 保留的界限

Food 内置名称键盘是拉丁输入；服务接受有效 UTF-8，但没有增加大型输入法。未校时不计算过期状态。新增大型物体模型仍延期；16 MiB Flash 中放入两份约 10 MiB 应用加现有模型/存储不成立，普通 A/B OTA 需另行调整分区、资产/模型或硬件。后续扩展必须重新测量峰值内部 RAM、PSRAM 最大连续块与 CPU 混合负载。


## 接续入口

工作目录 `.worktrees/vision-ai`；生产构建 `/tmp/yokai-audio-idf61`，配置 `/tmp/yokai-vision-profile.sdkconfig`；诊断构建 `/tmp/yokai-quality-diag`，配置 `/tmp/yokai-quality-diag.sdkconfig`。原稳定固件备份 `/tmp/yokai-quality-baseline`。

崩溃日志 `/tmp/yokai-quality-diag-board.log`；补充采集 `/tmp/yokai-quality-diag-vision.log`；回滚日志 `/tmp/yokai-quality-rollback.log`；本机回归 `/tmp/yokai-quality-host.log`；构建 `/tmp/yokai-quality-build.log`。所有采集均有限时，不留下常驻监控。

下一步先完成 8 KiB 天气栈的 HTTPS 实板复测并记录最小剩余栈/内部堆，验证无崩溃后再测 Vision、A2DP/Voice 混合负载与 Food 触控、持久化。确认远端 CI 通过后才考虑 main 整合。不能将本轮主机检查通过等同于设备稳定验收。


### TLS 修正后的生产配置实测

`/tmp/yokai-quality-production-board.log` 有限 90 秒记录：HTTPS 请求成功，`WEATHER_HEALTH http_ok=1 stack_min_bytes=4044`；17 份堆/服务快照，最后 `int_free=69239`、历史最低 `int_min=31108`、最大内部块 `31744`；PSRAM `5964744`、历史最低 `5948444`、最大连续块 `5898240` 字节。此段为 Home（vstate=0），没有 panic、栈保护或显示超时，不能代替 Vision 或混合负载结果。

生产镜像加入天气高水位日志后为 10,573,568 字节，应用分区余 1,485,056 字节。此前 Clang 专用 Food UI 检查选项改为 GCC/Clang 通用选项；本机统一检查再次通过。最终 CPU 采集、生产配置恢复及远端 CI 结果仍待更新。

### 冷启动 Vision 再现的崩溃与修复候选

用户冷启动恢复 Home/触摸后打开识别，`/tmp/yokai-quality-diag-vision-cold.log` 第 830 行捕获 Core 1 `Load access fault`（MEPC `0x404b2e80`、MTVAL `0`）。用匹配的 `/tmp/yokai-quality-diag/korvo1_yokai_demo.elf` 解码，故障为 RGB565 SIMD resize helper 的 `lh a5,0(a5)`；源指针本身有效，但保存源指针的栈参数地址为 `0x2e00377c`，属于 RTC RAM。前面的 SIMD broadcast 从该地址读取后计算出空地址。这支持 RTC 栈与 SIMD 访问不兼容的原因候选，仍须板上重测证实。

Vision 启动与天气 TLS 重叠时内部空闲 37,895 字节，包含 RTC 区域；模型构造后最低 7,384 字节。天气请求成功且余栈 4,008 字节，此次不是天气栈保护故障。现将推理任务的原 12 KiB 栈改用原生 `xTaskCreateWithCaps(INTERNAL|8BIT|DMA)`，排除 RTC 回退；分配不足沿已有错误路径提示。退出事件后任务挂起，由 owner 使用配对 `vTaskDeleteWithCaps` 回收，避免自删除另建清理任务。主机检查和生产构建通过；硬件验证待完成。

该生产镜像为 10,574,336 字节，应用分区余 1,484,288 字节，SHA256 `e46a53455f72fdf933a661688e035924eb1bc145ac7ca05abeb798f51511d5bf`。之前的 `b456681` [远端 CI](https://github.com/weilu-kong/Full-Function-S31-P4-Demo/actions/runs/36822533159) 已全部成功，但不包含这次推理栈改动。

### 推理栈修复后的生产实测（启动调度改良之前）

镜像全量写入后校验通过，恢复生产配置（运行时 CPU 统计关闭）。`/tmp/yokai-quality-vision-stack-board.log` 有限 120 秒采集已结束，无 panic、watchdog 或显示超时。用户确认第一次启动显示失败，返回桌面再进入可以打开；日志对应第一次与 HTTPS 并发时 DMA 栈分配失败并安全停止 capture，TLS 完成后再次启动成功。

第二次启动推理栈地址 `0x2f04421c`、`dma=1`，首次人脸检测 153 ms，最新已完成 578 次推理。稳定区间摄像头约 15.58 fps、预览消费约 14.84 fps；内部 RAM 历史最低 30,580 字节，PSRAM 历史最低 1,013,420 字节、最大块 999,424 字节，推理最小余栈 7,920 字节。不能使用本配置的 LVGL CPU 100% 推断 CPU 余量。

这段验证支持排除 RTC 推理栈能解决此次故障，尚不是长时间或混合负载验收。启动与天气 TLS 的资源竞争仍会产生一次可恢复的启动失败，后续完善启动调度；本轮保留该保护，未通过减栈、增加图像缓冲或让写 SPIFFS 的推理任务使用 PSRAM 栈来绕过限制。当前设备保持该生产固件，串口采集已停止。

### 启动调度改良（当前设备版本）

复用 UI periodic tick：天气 refreshing 时显示准备中，结束后自动启动；只对 ESP_ERR_NO_MEM 清理后重试，间隔至少 500 ms，总期限 30 秒。离开 Vision 取消请求；其他真实故障或超时保留错误提示。任务分配失败返回具体内存错误；capture 退出超时仍单独报超时。没有增加任务、队列、图像缓冲或栈容量，静态等待状态含对齐共 12 字节。

独立复查发现准备阶段可提交注册，失败启动的 stop 会清空队列并丢失已接受的命令。生产 begin/cancel/reregister 入口现要求 RUNNING，与 delete/clear 一致；UI 未准备好时保留输入框并给出提示。实际生产函数 harness 在修复前复现了 OFF 状态入队，修复后验证 OFF/STARTING/ERROR 不入队、RUNNING 正常入队。调度检查覆盖忙碌→就绪、取消、500 ms 节流、30 秒超时/回绕及真实错误；全部主机检查、生产/诊断构建、Flash gate 和复审通过。

生产镜像 10,574,736 字节，比前一版增加 400 字节，分区余 1,483,888 字节；SHA256 `be185e0f7b093b028bc1832b97585f20f34fd63b8d463a6369a629d96a709fe1`。烧录日志 `/tmp/yokai-vision-start-flash.log` 确认全量写入/hash 校验。有限 180 秒 `/tmp/yokai-vision-start-board.log` 为 Home：HTTPS 成功，余栈 4,024 字节；内部最小堆 30,084 字节，最后空闲 69,215 字节；PSRAM 最小 5,949,132 字节、最大块 5,898,240 字节，无 panic/watchdog/显示超时。此段未进入 Vision，不能作为首次启动/重进验收。

当前设备保留生产改良版，采集已结束。独立 CPU 诊断镜像已编译，未烧录；等待用户的首次进入/重进反馈后再继续有限 Vision 与混合负载测量。此前 `96f3aca` 和 `9c3eeed` 的远端 CI 均已成功，新的调度改动仍需远端 CI。
