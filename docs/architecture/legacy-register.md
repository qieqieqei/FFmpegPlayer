# Legacy / Technical Debt Register

> 依据《FFmpegPlayer 企业级架构重构计划》阶段 1 收尾规则建立。
> **本文件只登记，不删除。** 任何删除必须等到**阶段 6**，且经「源码引用 + 构建依赖 + 运行路径」三重确认后方可执行。
>
> - 判据 A（构建依赖）：是否登记在 `FFmpeg_text_claw.vcxproj`（即是否参与编译）
> - 判据 B（引用情况）：反向 include 图中是否存在**外部**引用者（自身 .cpp 不算）
> - 基线：`2af4d01`（tag：`architecture-audit-baseline-2af4d01`）

字段说明：**问题 / 当前状态 / 引用情况 / 构建依赖 / 运行时依赖 / 风险 / 计划处理阶段**

---

## LEGACY-001 — `Input.cpp` / `Input.h`（旧输入接口）

| 字段 | 内容 |
|---|---|
| 问题 | 独立的旧"打开输入"实现，被 `Input/InputSource` 体系取代 |
| 当前状态 | 存在于源码树；**未登记 vcxproj**（不编译）；无任何外部引用 |
| 引用情况 | 反向 include：`Input.h <- Input.cpp`（仅自身） |
| 构建依赖 | 无（不在 `FFmpeg_text_claw.vcxproj` 的 ClCompile/ClInclude 中） |
| 运行时依赖 | 无（不参与链接） |
| 风险 | 低。但若日后误加入工程，会与 `Input/InputSource` 形成**两套输入路径** |
| 计划处理阶段 | 阶段 6（确认后归档/删除） |
| 证据 | `Input.h`(11 行) 声明自由函数 `OpenInput(const char*, AVFormatContext*&, int&, int&, AVCodecParameters*&)`；`Input.cpp`(142 行)；vcxproj 无登记 |

---

## LEGACY-002 — `Decoder.cpp` / `Decoder.h`（旧独立解码器 + 线程）

| 字段 | 内容 |
|---|---|
| 问题 | 独立的 `Decoder` 类，自带 `demuxThread`/`decodeThread` 与 `DemuxLoop`/`DecodeLoop`/`DoSeek`/`Close`，并直接持有 `PacketQueue`/`FrameQueue`。功能已被 "Player 三线程 + `VideoDecoder`/`AudioDecoder`" 取代 |
| 当前状态 | 存在于源码树；**未登记 vcxproj**（不编译）；无任何外部引用 |
| 引用情况 | 反向 include：`Decoder.h <- Decoder.cpp`（仅自身） |
| 构建依赖 | 无（不在工程文件中） |
| 运行时依赖 | 无（不参与链接，不产生线程） |
| 风险 | **中。** 若被误引用，会引入**第二套解复用/解码链路 + 第二组线程**，与现有 `StartThreads` 三线程模型冲突（正是计划"停止并报告"的雷区） |
| 计划处理阶段 | 阶段 6（确认后归档/删除） |
| 证据 | `Decoder.h`(183 行) `Decoder.h:153/155` 两个 `std::thread`；`Decoder.h:103/105` 持有队列引用；`Decoder.cpp`(816 行) `:218/236/249/256` 线程启停 |

---

## LEGACY-003 — 重复模块（同一职责两套实现）

| 字段 | 内容 |
|---|---|
| 问题 | 多个职责存在"旧实现 + 在实现"两套，需明确以在实现为准 |
| 当前状态 | 并存于源码树（详见下表） |
| 引用情况 | 旧实现均无外部引用；在实现被 Player/其它模块引用 |
| 构建依赖 | 旧实现多数不编译（`Screenshot.*`、`Input.*`、`AudioMixer.*`）；`Decoder.*` 不编译 |
| 运行时依赖 | 旧实现均无运行时依赖 |
| 风险 | 中。重构期间若误改/误用旧实现，会引入分叉行为 |
| 计划处理阶段 | 阶段 6 |
| 明细 | ① `Screenshot.cpp/.h`（`SaveScreenshotBMP`，13/189）↔ `Screenshot/ScreenshotManager`（439 行，`Player.h` 持有）<br>② `Decoder.*`（816）↔ `VideoDecoder.cpp` + `AudioDecoder.cpp`<br>③ `Input.*`（142）↔ `Input/InputSource`(+`FileInput`/`NetworkInput`/`CameraInput`)<br>④ `Audio/AudioMixer.*`（未编译、无对应在实现） |

---

## LEGACY-004 — 循环依赖 / 反向耦合

| 字段 | 内容 |
|---|---|
| 问题 | 计划要求登记"循环依赖"。**实测：未发现 `*.h → *.h` 头文件级循环**（对全部 ~55 个头文件做可达性检测，无任何头可回到自身）。但存在**单向反向耦合**（非循环），违反目标分层方向 |
| 当前状态 | 头文件级无循环；存在 4 类反向引用 |
| 引用情况 | ① `Renderer.cpp:4`、`OSDManager.cpp:5`、`Event.cpp:3` → `#include "Player.h"` 并调用 20~25 处 Player 方法（**渲染/OSD/输入模块反向依赖核心 Player**）<br>② `SeekController.cpp` → `Demuxer.h` / `PacketQueue.h` / `FrameQueue.h`<br>③ `RTMPPublisher.h` → `FLVMuxer.h` |
| 构建依赖 | 正常参与编译 |
| 运行时依赖 | 正常参与运行（以上均为在用模块） |
| 风险 | 中。反向依赖**不构成循环**，但拆除时（把 Player 拆成 Pipeline/Sync/Output）极易"改出"真正的循环引用 —— 需按计划每步 MSBuild Debug 验证 |
| 计划处理阶段 | 阶段 2（确定依赖方向）+ 阶段 3~8（逐模块拆除），阶段 8 收口 |
| 证据 | 反向 include 图；`Renderer.h:13` / `OSDManager.h:7` 已用前置声明 `class Player;`（头文件干净），问题集中在 .cpp |

---

## LEGACY-005 — 无引用文件（已编译但从未接线）

| 字段 | 内容 |
|---|---|
| 问题 | 若干文件**在 vcxproj 中参与编译，但没有任何外部引用者**（既非入口、也未被调用），属"死重" |
| 当前状态 | 在工程中编译；反向 include 图显示外部引用数 = 0 |
| 引用情况 | `Sync/Clock.h`、`Filter/FilterGraph.h`、`Filter/AudioFilter.h`、`Filter/VideoFilter.h`、`Input/RTSPClient.h` 均仅被各自 `.cpp`（及 `Filter` 三件套内部互引）包含 |
| 构建依赖 | **是**（均在 vcxproj 的 ClCompile/ClInclude 中）→ 有编译时间与维护成本 |
| 运行时依赖 | 无（未被调用） |
| 风险 | 低~中。增加编译面、增加误用可能；`Filter/` 未接入意味着"滤镜能力"实际未对外提供 |
| 计划处理阶段 | 阶段 6（先确认，再决定接线 or 归档） |
| 明细 | ① `Sync/Clock.h/.cpp`（被 `VideoClock`/`AudioClock`/`MasterClock` 取代）<br>② `Filter/FilterGraph.*` + `Filter/AudioFilter.*` + `Filter/VideoFilter.*`（整个 Filter 模块未接入 Player）<br>③ `Input/RTSPClient.h/.cpp`<br>（另：`Audio/AudioMixer.*` 未编译且无引用，归 LEGACY-003④） |

---

## LEGACY-006 — 旧接口

| 字段 | 内容 |
|---|---|
| 问题 | 旧式公开接口仍然存在于源码树，易被误当作现行 API |
| 当前状态 | 无外部引用 |
| 引用情况 | 无 |
| 构建依赖 | `Clock`/`RTSPClient` 参与编译；`OpenInput`/`SaveScreenshotBMP`/`Decoder` 不编译 |
| 运行时依赖 | 无 |
| 风险 | 低。仅"命名/接口污染"风险 |
| 计划处理阶段 | 阶段 6 |
| 明细 | ① `OpenInput(...)`（`Input.h`）② `SaveScreenshotBMP(...)`（`Screenshot.h`）③ `class Decoder` 的 `Start/Stop/DoSeek/GetVideoPacketQueue/GetVideoFrameQueue`（`Decoder.h`）④ `class Clock`（`Clock.h`） |

---

## LEGACY-007 — 其它已发现但暂不适合处理的问题

| 字段 | 内容 |
|---|---|
| 问题 | 触碰即触发计划"停止并报告"条款的既有技术债，登记而不在本阶段处理 |
| 当前状态 | 均在用、均敏感 |
| 引用情况 | 深度耦合（见各条） |
| 构建依赖 | 全部参与编译 |
| 运行时依赖 | 全部参与运行 |
| 风险 | **高** |
| 计划处理阶段 | 阶段 2~8 主线（非"清理"，而是"重构"） |
| 明细 | ① `ConfigManager` 内嵌自研 JSON 解析器（占其非空行 ~64%）——非死代码，属技术债；读取行为有回归风险<br>② 4 个**裸 SDL 指针**（`window/renderer/texture/rgbTexture`），且**创建在 `Renderer::InitSDL`、销毁在 `Player::ReleaseMedia`**（跨模块所有权切分）<br>③ `Player` 上帝对象：4651 行 / 103 方法 / 聚合 26 个 `unique_ptr` + 5 队列 + 3 线程<br>④ 队列元素靠 `std::move` + 约定转移所有权（如回调用 `pkt.release()` 显式移交）——生命周期敏感，改动易出 double-free/悬垂 |

---

## 全局规则（务必遵守）

1. **"无明显调用者" ≠ "允许删除"。** 一切删除推迟到阶段 6，且必须同时满足：源码引用为空 **且** 不参与构建 **且** 无运行路径（或已确认迁移完毕）。
2. 本清单为**只读登记**；新增发现应**追加**条目，不覆盖历史。
3. 每个条目在阶段 6 处置后，应回填"最终处置"与对应 commit。

---

## 阶段 6 处置回填（2026-10-08，commit \`b87a6af\`）

**三重确认实测**（脚本 `%TEMP%` 侧 `.tmp_phase6_legacy_audit2.js`，扫描 105 个非 legacy 源文件）：

- **判据 B（源码引用）**：**无任何 `#include "legacy/…"`，亦无按 basename 命中的 legacy 头** → 9 个 legacy 头**外部引用全空**。
- **判据 A（构建依赖）**：`FFmpeg_text_claw.vcxproj` 中仍登记 **5 项**（`AudioFilter`/`FilterGraph`/`VideoFilter`/`RTSPClient`/`Clock` 各 .h+.cpp，共 10 条）；其余 4 项（`AudioMixer`/`Decoder`/`Input`/`Screenshot`）**未登记**。
- **判据 C（运行路径）**：9 项均非入口、无调用者，不参与运行链路（`Decoder` 已不在编，不会引入第二套解码线程）。

**最终处置**：

| 条目 | 处置 | commit |
|---|---|---|
| LEGACY-001 `Input.*` | 阶段 3 已移入 `legacy/`、不在编；确认零引用 → **保留归档（不删）** | `b87a6af` |
| LEGACY-002 `Decoder.*` | 不在编、零引用 → **保留归档** | `b87a6af` |
| LEGACY-003 重复模块（`Screenshot`/`AudioMixer`/`Input`/`Decoder` 旧实现） | 旧实现均零引用 → **保留归档** | `b87a6af` |
| LEGACY-005 无引用文件（`Clock` / `Filter` 三件套 / `RTSPClient`） | **从 `vcxproj` + `.vcxproj.filters` 摘除构建条目**（归档，不删源码） | `b87a6af` |
| LEGACY-006 旧接口 | 随上条目一并归档 | `b87a6af` |

> **说明**：按本文件规则 1，「源码引用为空 **且** 不参与构建 **且** 无运行路径」方可删除。本次**仅摘除构建条目**（即「归档」处置），
> **未删除任何源码文件**——`legacy/` 9 组文件全部原样保留备查。若日后确认长期无用，可另立**独立删除提交**（届时源码引用/构建/运行三判据均已长期为空）。
> **验证**：摘除后 Debug|x64 与 Release|x64（Rebuild）均 **0 error**；三样例 `--record` FLV 字节 **7280913 / 9666764 / 59694920** 逐位一致，0 ERROR/WARN。
