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
