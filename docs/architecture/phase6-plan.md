# 阶段 6 计划：Legacy 处置 + Features / 统计编排收敛

> 基线：HEAD `419d768`（阶段 5 收口）；`core/Player.cpp` **2348** 行 / **79** 个方法、`core/Player.h` **476** 行。
> 硬约束沿用 `refactor-rules.md`：先审计后改；禁止一次性大重构；禁止机械拆文件；不改行为；不顺手修 Bug；不确定不删；
> 触及「编译错误无法定位 / 死锁 / double-free / Legacy 不确定」即**停止并报告**。

---

## 1. 阶段目标

1. **Legacy 三重确认处置**（`legacy-register.md` 规则 1）：源码引用 / 构建依赖 / 运行路径三判据齐备后，对已确认的死重**归档**（不删源码）。
2. **Features / 统计编排收敛**：把 `Player` 中剩余的 Seek / 播放列表 / 统计**编排方法**外移，`Player` 只保留对外薄 API。
3. 为阶段 7（Streaming / Recording 输出链）清场。

> 口径沿用阶段 5 结论：**需要访问 core 内部状态的编排落到 `core/PlaybackSession`**（`target-architecture.md §3` 把主循环/协调划给 `PlaybackSession`），
> 以避免 `features/* → core` 或 `output/* → core` 反向依赖（`dependency.md` 禁止，阶段 8 才拆）。

---

## 2. 只读审计结论（本阶段落笔前实测）

### 2.1 Legacy 三重确认（`legacy/` 共 9 个 .h + 9 个 .cpp）

**判据 B（源码引用）实测**：扫描全部 105 个非 legacy 源文件（排除 `x64/`、`.git/`、`build*/`），
**无任何 `#include "legacy/…"`，亦无任何按 basename 命中的 legacy 头** → 9 个 legacy 头**外部引用全为空**。

**判据 A（构建依赖）实测**（`FFmpeg_text_claw.vcxproj`，共 114 条 ClCompile/ClInclude）：

| legacy 文件 | 在 vcxproj | 外部引用 | 结论 |
|---|---|---|---|
| `legacy\AudioFilter.{h,cpp}` | **是** | 无 | 死重（在编） |
| `legacy\FilterGraph.{h,cpp}` | **是** | 无 | 死重（在编） |
| `legacy\VideoFilter.{h,cpp}` | **是** | 无 | 死重（在编） |
| `legacy\RTSPClient.{h,cpp}` | **是** | 无 | 死重（在编） |
| `legacy\Clock.{h,cpp}` | **是** | 无 | 死重（在编） |
| `legacy\AudioMixer.{h,cpp}` | 否 | 无 | 死重（不在编） |
| `legacy\Decoder.{h,cpp}` | 否 | 无 | 死重（不在编） |
| `legacy\Input.{h,cpp}` | 否 | 无 | 死重（不在编） |
| `legacy\Screenshot.{h,cpp}` | 否 | 无 | 死重（不在编） |

**判据 C（运行路径）**：9 项均非入口、无调用者；其中在编 5 项仅被自身/彼此引用 → 不产生跨线程、不参与运行链路。
`Decoder`（LEGACY-002，风险中）已不在编，不会引入第二套解码线程。

> **处置决定（阶段 6.1）**：按 `legacy-register.md`「先确认，再决定**接线** or **归档**」——本阶段选**归档**：
> 把 5 个「在编但零引用」的死重从 `vcxproj`/`.vcxproj.filters` **摘除编译条目**（**不删任何源码文件**，文件留在 `legacy/`）。
> 4 个本就不在编的保持原状。回填 `legacy-register.md` 各项「最终处置」。

### 2.2 `Player` 剩余方法体量（top，行数含空行）

| 方法 | 行 | 归属阶段 |
|---|---|---|
| `UpdateStatistics` | 198 | **6.2** |
| `Init` | 117 | 8（装配协调，与 `Close` 对称） |
| `StartHLS` | 117 | 7 |
| `EnsureOutEncoders` | 112 | 7 |
| `Run` | 110 | 5 已完成（保留骨架） |
| `ExpandPlaylistWithSiblings` | 106 | **6.4** |
| `StartPushing` | 96 | 7 |
| `FeedOutputVideo` | 73 | 7 |
| `ToYuv420p` | 71 | 7 |
| `StartRecording` | 71 | 7 |
| `FlushOutEncoders` | 63 | 7 |
| `FeedOutputAudio` | 58 | 7 |
| `StopAllOutputs` | 52 | 7 |
| `DispatchVideoPacket` | 50 | 7 |
| `LoadConfig` | 47 | 8 |
| `Close` | 44 | 8 |
| `RequestSeek` | 42 | **6.3** |
| `GetTimeString` / `GetDurationString` | 35 / 22 | UI 格式化（可留） |
| `StopRecording` / `ToggleRecording` / `StopPushing` / `StopHLS` / `ReleaseOutEncoders` | 35/32/31/33/28 | 7 |
| `PlayPrevious` / `PlayNext` | 21 / 21 | **6.4** |
| `AddToPlaylist` | 13 | **6.4** |

> 输出链（编码/复用/推流/HLS）合计 ≈ **800 行**，按 `target-architecture.md §6` 属**阶段 7**；`Init`/`Close`/`LoadConfig` 属装配（阶段 8）。

### 2.3 `features/*` 现状（已独立成模块，非本阶段新建）

`features/seek/SeekController`、`features/playlist/PlaylistManager`、`features/statistics/PlayerStatistics`、
`features/screenshot/ScreenshotManager`、`features/subtitle/SubtitleManager` **均已存在**，`Player` 以 `unique_ptr` 持有。
故本阶段的「features 收敛」= 把 `Player` 中**包装这些模块的编排方法**外移，而非新建模块。

---

## 3. 子步设计

| 子步 | 内容 | 落点 | 风险 |
|---|---|---|---|
| **6.1** | Legacy 三重确认 + 归档（5 条在编死重摘除 vcxproj/filters，**不删文件**）；回填 `legacy-register.md` | `FFmpeg_text_claw.vcxproj(.filters)`、`docs/architecture/legacy-register.md` | 低 |
| **6.2** | `UpdateStatistics`(198) 外移 | `PlaybackSession::UpdateStatistics()`；`Player` 保留 `GetStatistics/GetNetworkStatistics` 薄转发 | 中 |
| **6.3** | Seek 请求编排外移（`RequestSeek` + `HasSeekRequest/GetSeekPosition/IsSeekHandled/ClearSeekHandled`） | `PlaybackSession`（`seekController` 为会话级） | 中 |
| **6.4** | 播放列表导航外移（`ExpandPlaylistWithSiblings`/`PlayPrevious`/`PlayNext`/`AddToPlaylist`/`GetPlaylistIndex/Count`） | `PlaybackSession`；`Player` 保留对外薄 API（`app/main`、`app/Event` 调用点不变） | 中 |
| **6.5** | 阶段报告 `docs/architecture/phase6-report.md` + 收口 | docs | 低 |

**通用手法（承阶段 5 脚本）**：latin1 裸字节读写；`codeOnly()` 剥字符串/注释后按 `{}` 配对切块；先全部断言再按行号**降序** splice；
只在代码区替换标识符；`media->`→`media.`、Player 成员→`owner.`、`session->` 去前缀；自撰文本一律 ASCII。

---

## 4. 风险与停止条件

- **6.1**：摘除 vcxproj 条目后若 Debug/Release 链接报「未解析的外部符号」→ 说明存在未察觉的引用，**立即回退摘除**并在登记中改判为「保留」，停止该子步。
- **6.2/6.3/6.4**：若搬运触发「局部变量/成员同名误伤」「引用 vs 指针」「友元方向」等（阶段 4.4 已踩），按既有补丁手法逐一收敛；
  若单步编不过且无法定位 → 停止并报告（规则 §4）。
- 全程**不删源码**；`legacy/` 仅摘除构建条目。

## 5. 验证口径（每子步）

1. `MSBuild … /p:Configuration=Debug /p:Platform=x64 /m /v:minimal` → **0 error**（warning 基线 32）。
2. 三样例 `--record` 回归（cwd `x64\Release`）：`124662f1…`(12.833s) / `21e16264…`(22.655s) / `a4c277.mp4`(141.800s)，
   FLV 字节 **7280913 / 9666764 / 59694920** 逐位一致，无 ERROR/WARN。
3. 每阶段末 `Release|x64` 亦须 0 error；回归无 `dropFrame/lateDrop` 异常。
4. 每子步**独立提交**（源码 `refactor(...)`、文档 `docs(architecture): …`）并 `git push origin feature/live-buffer`。

## 6. 执行记录（滚动回填）

- **6.1 …**（待填）
- **6.2 …**（待填）
- **6.3 …**（待填）
- **6.4 …**（待填）
- **6.5 …**（待填）
