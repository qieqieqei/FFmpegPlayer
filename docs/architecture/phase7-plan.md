# 阶段 7 计划：输出链外移（Streaming / Recording / Features）

> 基线：HEAD `625b05c`（阶段 6 收口）；`core/Player.cpp` **1955** 行 / 79 方法、`core/Player.h` **473** 行。
> 硬约束沿用 `refactor-rules.md`：先审计后改；禁止一次性大重构（按子步）；禁止机械拆文件；不改行为；不顺手修 Bug；不确定不删；
> 触及「编译错误无法定位 / 死锁 / double-free」即**停止并报告**。

---

## 1. 阶段目标

把 `Player` 中剩余体量最大的**输出链**（录制 / 推流 / HLS + 编码器编排 + 帧注入）外移为一个独立类，
使 `Player.cpp` 从 **1955 → 800~1300**（`target-architecture.md §6` 的阶段 7 指标），并把「Phase 8 反向依赖收口」要处理的壳进一步减薄。

---

## 2. 只读审计结论（本阶段落笔前实测）

### 2.1 输出链方法清单（`Player.cpp`，实测行号）

| 方法 | 行 | 类别 |
|---|---|---|
| `EnsureOutEncoders` | 112 | 编码器装配 |
| `ToYuv420p` | 71 | 帧转换 |
| `FeedOutputVideo` | 73 | 帧注入（解码线程） |
| `FeedOutputAudio` | 58 | 帧注入（解码线程） |
| `DispatchVideoPacket` / `DispatchAudioPacket` | 50 / 5 | 编码包分发 |
| `FlushOutEncoders` | 63 | 尾帧冲刷 |
| `StopAllOutputs` | 52 | 停止全部输出 |
| `ReleaseOutEncoders` | 28 | 资源释放 |
| `StartRecording` / `StopRecording` / `ToggleRecording` | 71 / 35 / 32 | 录制 |
| `StartPushing` / `StopPushing` / `TogglePushing` | 96 / 31 / 11 | 推流 |
| `StartHLS` / `StopHLS` / `ToggleHLS` | 117 / 33 / 11 | HLS |
| `IsRecording` / `IsPushing` / `IsHLSActive` | 4×3 | 状态查询 |

> 合计 ≈ **971 行**（含空行），占 `Player.cpp` 一半以上。

### 2.2 成员耦合实测（脚本逐方法统计）

21 个方法**只触达**以下成员（并集）：

```
outMutex, outVideoEncoder, outAudioEncoder, recordMuxer, rtmpPublisher, hlsMuxer,
outSws, outYuvFrame, outVideoPts, outAudioPts, outVideoPktIdx, outAudioPktIdx,
recording, pushing, hlsActive,      ← 这 15 个即 Player.h「成员：输出链（7.4–7.6）」整块
configManager,                      ← 仅 EnsureOutEncoders(2) / StartPushing(2) / StartHLS(2)
media                               ← 仅 EnsureOutEncoders(7)（读解码器上下文 / 帧时长 / 音频流参数）
```

**结论**：15 个输出链成员天然成簇，可整体外移；仅 `media`（core 内部）与 `configManager`（config 层）需要解耦处理。

### 2.3 调用点实测

- `EnsureOutEncoders`：`StartRecording`(1443) / `StartPushing`(1586) / `StartHLS`(1729) —— **均在输出链自身内**，不进入解码线程。
- `FeedOutputVideo/Audio`：`PlaybackSession.cpp` 982 / 1068 / 1462（**Video/Audio 解码线程**）。
- `StopAllOutputs`：`PlaybackSession.cpp:1299`（`ReleaseMedia`）。
- `FlushOutEncoders` / `ReleaseOutEncoders` / `Dispatch*`：全部在输出链自身内。
- `Toggle*`：`app/Event.cpp`、`app/main.cpp`（公共 API）。
- 其余 `Start*/Stop*/Is*`：公共 API（保留薄转发）。

---

## 3. 设计

### 3.1 新类：`recording/OutputPipeline.{h,cpp}`

沿用目标目录树：`recording/` 归「编码、封装、推流、HLS」（L4），已含 `VideoEncoder/AudioEncoder/Muxer/FLVMuxer/HLSMuxer/RTMPPublisher`。

- **持有**：`outMutex`、`outVideoEncoder`、`outAudioEncoder`、`recordMuxer`、`rtmpPublisher`、`hlsMuxer`、`outSws`、`outYuvFrame`、
  `outVideoPts`、`outAudioPts`、`outVideoPktIdx`、`outAudioPktIdx`、`recording`、`pushing`、`hlsActive`（自 `Player` 迁入，`Player` 不再持有）。
- **公共 API**：
  - `void SetSourceInfo(const SourceInfo&)`：媒体打开时由会话注入源参数（**解耦 core**）。
  - `bool StartRecording(const std::string&)` / `void StopRecording()` / `void ToggleRecording()`
  - `bool StartPushing(const std::string&)` / `void StopPushing()` / `void TogglePushing()`
  - `bool StartHLS(const std::string&)` / `void StopHLS()` / `void ToggleHLS()`
  - `bool IsRecording()/IsPushing()/IsHLSActive() const`
  - `void FeedOutputVideo(AVFrame*)` / `void FeedOutputAudio(AVFrame*)`（解码线程调用，内部 `outMutex` 保护）
  - `void StopAllOutputs()`、`void ReleaseOutEncoders()`
  - `bool EnsureOutEncoders()`（公共，供测试/收口；实现内不再读 `media`）
- **私有**：`ToYuv420p`、`DispatchVideoPacket`、`DispatchAudioPacket`、`FlushOutEncoders`。
- **构造**：`explicit OutputPipeline(ConfigManager* config)`（非拥有；`Config/` L0，允许被 L4 依赖）。
- **`SourceInfo`（解耦 `media`）**：
  ```cpp
  struct SourceInfo {
      int  width = 0, height = 0;
      int  fps = 25;
      bool hasAudio = false;
      int  sampleRate = 48000;
      int  channels = 2;
  };
  ```
  `EnsureOutEncoders()` 用 `srcInfo` 取代原 `media->videoDecoder->GetContext()` / `media->videoFrameDuration` / `media->demuxer->GetAudioStream()`。

### 3.2 依赖方向校验（对照 `dependency.md`）

```
recording/OutputPipeline  →  recording/{VideoEncoder,AudioEncoder,FLVMuxer,HLSMuxer,RTMPPublisher}   (同级/L4)
                          →  Config/{ConfigManager,StreamConfig}   (L0)
                          →  infra/{Logger,ErrorHandler,FFmpegPtr} (L0)
                          ✗  绝不 include core/*、Player.h、PlaybackSession.h
```

`Player`（L5 core）→ `OutputPipeline`（L4）为**向下依赖**，合规。`PlaybackSession` 通过 `owner.output` 访问亦合规。
（`Player.h` 仍 `#include "recording/OutputPipeline.h"` 以持有 `unique_ptr`，方向 core→recording，允许。）

### 3.3 落点：`Player` 持有 `std::unique_ptr<OutputPipeline> output;`

- 与 `statistics` / `networkStatistics` 等会话级 `unique_ptr` 同级（输出链跨媒体切换存在，不随 `MediaContext` 生命周期）。
- `ConfigManager` 仍在 `Player` 持有，构造 `OutputPipeline(configManager.get())`（放在 `configManager` 创建之后；`configManager` 现于 `Init`/ctor 创建 —— 若顺序不便，则在 `Init` 内创建 `output`）。

---

## 4. 子步设计

| 子步 | 内容 | 风险 |
|---|---|---|
| **7.1** | 新建 `recording/OutputPipeline.{h,cpp}`；**整体外移** 15 个成员 + 21 个方法；`Player` 保留 12 个公共薄转发；`PlaybackSession` 的 `Feed*`/`StopAllOutputs` 改走 `owner.output->`；`OpenMedia` 注入 `SetSourceInfo`；`media`/`configManager` 解耦（`srcInfo`/`this->config`）；登记工程 | **中高**（≈970 行搬移，但成员成簇、调用点少） |
| **7.2** | 阶段报告 `docs/architecture/phase7-report.md` + 收口 | 低 |

> 若 7.1 编译错误难以一次收敛，可再切 7.1a（编码器核：`EnsureOutEncoders`/`ToYuv420p`/`Feed*`/`Dispatch*`/`Flush`/`Release`/`StopAllOutputs`）
> 与 7.1b（`Start/Stop/Toggle/Is*`），但**成员随 7.1a 一次迁出**（避免中间态双份持有）。

### 通用手法（承阶段 5/6 脚本）
- latin1 裸字节读写；`codeOnly()` 剥字符串/注释后按 `{}` 配对切块；先全部断言、再按行号**降序** splice；只在代码区替换；自撰文本一律 ASCII。
- 本步特殊性：**成员整体迁出 → 方法体几乎无需加前缀**（`outVideoEncoder` 等仍是不限定名，只是不再是 `Player` 成员）。
  只需特判 `media->…`（→ `srcInfo.…`，`EnsureOutEncoders` 一处）与 `configManager`（→ `this->config`，3 处）。

---

## 5. 风险与停止条件

- **7.1**：若出现「`outSws`/`outYuvFrame` 类型未定义」→ 补 `infra/FFmpegPtr.h`；若出现循环依赖或未解析符号 → 停止并报告。
- 全程**不改行为**：日志文案、FLV 字节、播放行为必须与基线一致。
- 出现单步编不过且无法定位 → **停止并报告**（`refactor-rules.md §4`）。

## 6. 验证口径（每子步）

1. `MSBuild /p:Configuration=Debug /p:Platform=x64` → **0 error**（warning 基线 32）。
2. 三样例 `--record` 回归（cwd `x64\Release`）：FLV 字节 **7280913 / 9666764 / 59694920** 逐位一致，时长 12.833 / 22.655 / 141.800 s，无 ERROR/WARN。
3. 阶段末 `Release|x64` 亦须 0 error。
4. 每子步**独立提交**（源码 `refactor(...)`、文档 `docs(architecture): …`）并 `git push origin feature/live-buffer`。

## 7. 执行记录（滚动回填）

### 7.1 完成 —— 输出链整体外移（提交 `123d238`，已推送 `9228c15..123d238`）

**结果**：`core/Player.cpp` 1955 → **1016**（−939，进入 800~1300 目标区间）；`core/Player.h` 473 → **410**；
`core/PlaybackSession.cpp` 2756 → **2812**；`core/PlaybackSession.h` 208 → **209**；
新增 `recording/OutputPipeline.h` 142 / `.cpp` 1027；vcxproj 104 → 106 条目（filters 未登记，依 `RTMPPublisher.*` 先例）。

**做法**（与设计一致）：
- 21 个方法中 **20 个纯改名自动搬移**（`Player::X` → `OutputPipeline::X`，成员随之迁出故方法体几乎无需前缀）；
  `EnsureOutEncoders` **手写**（`media->…` → `srcInfo.…`；`configManager` → 成员 `config`）。
- `configManager` 出现于 `EnsureOutEncoders`/`StartPushing`/`StartHLS`，脚本统一改写为 `config`。
- 15 个成员 + `CopyCodecPar` 静态函数一并外移；`Player.h` 删成员块 H404–H435 与私有声明块 H326–H355。
- `Player` 保留 12 个公共薄转发；`PlaybackSession` 4 处调用点改走 `owner.output->`；`OpenMedia` 注入 `SetSourceInfo`。
- `output` 于 `LoadConfig()` 内 `configManager` 之后创建（早于 `Init`→`OpenMedia`）。

**验证**：Debug/Release 均 **0 error**、无新增 warning；三样例 `--record` 回归 FLV **7280913 / 9666764 / 59694920** B、
时长 **12.833 / 22.655 / 141.800** s，与阶段 6 基线**逐位一致**，ERROR/WARN = 0。

**踩坑（详见 `phase7-report.md §5`）**：
1. 注入块若落在 `return true;` **之后** → 死代码（编译/运行皆正常，仅录制静默不启动、无 `.flv`）→ 必须插在**最后一条 `return` 之前**；回归须核对产物而非只看 exit code。
2. 文件级 `static CopyCodecPar` 须随调用者迁入 `OutputPipeline.cpp`（否则 C3861 ×6；留在原地则变未引用静态函数告警）。
3. 单行方法定义（`bool Player::IsRecording() const { … }`）的转发需按 `{` 切分签名行，否则原方法体残留在签名里。

### 7.2 完成 —— 阶段报告与收口

- 新建 `docs/architecture/phase7-report.md`（含设计、指标、验证、三条踩坑、阶段 8 待办）。
- 回填本 §7 执行记录。

### 阶段 7 结论

输出链外移完成；`Player.cpp` 已进入 800~1300 区间。剩余 ≈1016 行 = `Init`/`Close`/`LoadConfig` 装配 + `Run()` 主循环壳 + UI 访问器，
连同 `core → app` 反向依赖收口一并交**阶段 8**（目标 `Player` → 200~500 行）。
