# 阶段 6 报告：Legacy 处置 + Features / 统计编排收敛

> 起点 `419d768`（阶段 5 收口）→ 本阶段收口提交见 `git log`。
> 硬约束沿用 `refactor-rules.md`：先审计后改；禁止一次性大重构；禁止机械拆文件；不改行为；不顺手修 Bug；不确定不删。

---

## 1. 阶段目标与结果

| 目标 | 结果 |
|---|---|
| 1. Legacy 三重确认处置（源码引用 / 构建依赖 / 运行路径） | **完成**：9 项 legacy 外部引用实测为空；5 项在编死重从构建摘除（不删源码），4 项维持不在编 |
| 2. Features / 统计编排收敛（Seek / Playlist / Statistics） | **完成**：`UpdateStatistics` / Seek 编排 / Playlist 导航 全部外移 |
| 3. 为阶段 7（输出链）清场 | **完成**：`Player.cpp` 由 2348 → **1955**，剩余体量集中为输出链 + 装配 |

`Player.cpp` **未达**计划书里「800~1300」的目标区间 —— 原因见 §3（剩余几乎全属阶段 7/8）。

---

## 2. 子步执行明细

### 6.1 Legacy 三重确认 + 归档（`b87a6af` + 登记回填 `d8945b3`）
- **判据 B（源码引用）**：扫描全部 105 个非 legacy 源文件 → **无任何 `#include "legacy/…"`**，9 个 legacy 头外部引用全为空。
- **判据 A（构建依赖）**：`FFmpeg_text_claw.vcxproj` 中在编 5 项（AudioFilter / FilterGraph / VideoFilter / RTSPClient / Clock），不在编 4 项（AudioMixer / Decoder / Input / Screenshot）。
- **判据 C（运行路径）**：均非入口、无调用者；在编 5 项仅被自身/彼此引用；`Decoder` 已不在编（无第二套解码线程风险）。
- **处置**：对 5 项在编死重**摘除构建条目**（vcxproj 114→104 条，filters 104→94 块），**不删任何源码文件**；回填 `legacy-register.md` 最终处置。
- **验证**：Debug|x64 增量 exit 0；Release|x64 **Rebuild** exit 0 / **0 error / 43 warning**（40×C4828 来自既有 `output/osd/FontManager.h`，3×C4244 为既有；Release Rebuild 的 warning 基线即 43，早前多次出现的 32 是**增量**构建计数）；三样例回归字节一致。

### 6.2 `UpdateStatistics` 外移（`59457ee`）
- `Player::UpdateStatistics`（198 行）→ `PlaybackSession::UpdateStatistics`；2 处调用点已是 `owner.UpdateStatistics()`，改为直接调用。
- 前缀改写：`statistics/networkStatistics/bufferController/streamMonitor/syncController` → `owner.*`；`session->Get{Video,Audio}QueueSize|GetVideoQueueCapacity` 去前缀；`media->` → `media.`；只在代码区替换。
- `Player.cpp` 2348 → 2150。

### 6.3 Seek 编排外移（`20fe1a1`）
- 移入 `RequestSeek(double)`、`HasSeekRequest()`、`GetSeekPosition()`、`IsSeekHandled()`、`ClearSeekHandled()`。
- **方案**：Player **原位薄转发**（`session->RequestSeek(seconds);` 等），`seekController` 仍由 Player 持有，session 用 `owner.seekController`；`session->seekPosition/seekPending/audioAbort` 去前缀，`media->` → `media.`。`app/Event` 调用点不变。
- `Player.cpp` 2150 → 2108。

### 6.4 Playlist 导航外移（`c7128ef`）
- 移入 `AddToPlaylist` / `ExpandPlaylistWithSiblings` / `PlayPrevious` / `PlayNext` / `GetPlaylistIndex` / `GetPlaylistCount` / `GetCurrentPath`。
- **方案**：`playlistManager` 仍由 Player 持有，session 用 `owner.playlistManager`；Player 保留 7 个薄转发，`app/main`、`app/Event` 调用点不变；`session->switchPath/switchRequested` 去前缀。
- `PlaybackSession.cpp` 增加 `#include "features/playlist/PlaylistManager.h"`（显式化）。
- `Player.cpp` 2108 → **1955**。

### 6.5 报告 + 收口
- 本文件 + `phase6-plan.md` §6 回填（6.1~6.4 记录 + 结果表 + 坑）。

---

## 3. 结果指标

| 文件 | 阶段 6 前 | 阶段 6 后 |
|---|---|---|
| `core/Player.h` | 476 | 473 |
| `core/Player.cpp` | 2348 | **1955** |
| `core/PlaybackSession.h` | 175 | 208 |
| `core/PlaybackSession.cpp` | 2301 | 2756 |
| `FFmpeg_text_claw.vcxproj` 条目 | 114 | 104 |

**剩余 `Player` 体量的归属**（按 `target-architecture.md §6`）：
- 输出链（`EnsureOutEncoders` / `StartHLS` / `StartPushing` / `StartRecording` / `FlushOutEncoders` /
  `FeedOutputVideo` / `FeedOutputAudio` / `ToYuv420p` / `StopAllOutputs` / `DispatchVideoPacket` 等）≈ **800 行** → **阶段 7**；
- `Init`(117) / `Close`(44) / `LoadConfig`(47) 装配协调 → **阶段 8**；
- UI 时间/时长格式化与访问器 → 可留。

故「800~1300」实为输出链外移**之后**的中间态目标，将在阶段 7 达成。

---

## 4. 设计决策与偏差

1. **编排落点统一为 `core/PlaybackSession`**：Seek / Playlist / 统计都要读 core 内部状态；落 `features/*` 或 `output/*` 会新增
   `features/output → core` 反向依赖（`dependency.md` 禁，阶段 8 才拆），且 `target-architecture.md §3` 把主循环/协调划给 `PlaybackSession`。
2. **Player 保留「薄转发」**：6.3/6.4 采用「逻辑入 session + Player 原位转发」，而非移动 `unique_ptr` 成员本身（后者要动 `Init`/`Close`，更侵入）。
   与阶段 5「VideoPresenter getter 薄转发」一致；阶段 8 反向依赖收口时再统一清理。
3. **Legacy 只解编不删源码**：满足「不确定不删 / 可回收」，登记表记录最终处置，便于回接。
4. **无行为改变**：全程仅搬运 + 前缀改写；三样例 FLV 逐位一致作证。

---

## 5. 验证记录

每个子步：`MSBuild /p:Configuration=Debug|Release /p:Platform=x64` → **0 error / 32 warning**（增量基线；6.1 的 Release Rebuild 为 43）。
三样例 `--record` 回归（cwd `x64\Release`）：`124662f1…` 12.833 s / 7280913 B、`21e16264…` 22.655 s / 9666764 B、`a4c277.mp4` 141.800 s / 59694920 B，
**全部 exit 0、字节逐位一致、无 ERROR/WARN、无 dropFrame/lateDrop 异常**。

---

## 6. 复现要点 / 坑

1. **vcxproj 摘条目不删源码**：`legacy/` 外部引用实测为空后仅解编，随时可回接。
2. **薄转发替换漏 `{` 行**：把方法体替换为转发时，若切片未含签名末尾的 `{` 行，会生成无 `{` 的函数体 →
   MSVC `error C3646 'session' unknown override specifier`。修法：切到 `b0 + 1` 再拼 `[fwd, '}']`；改前先从 `%TEMP%/phase64_backup` 恢复。
3. **同名成员只在代码区加前缀**：`playlistManager` → `owner.playlistManager`；字符串/注释里的同名标识符不能动（`codeOnly()` 掩码）。
4. **`session->` 与 `media->` 的会话内改写**：会话里 `media` 是引用 → `media->` 必须改 `media.`；会话自己的成员（`seekPosition`/`seekPending`/`audioAbort`/`switchPath`/`switchRequested`）要去 `session->` 前缀。
5. **编码**：仓库混合编码，脚本一律 **latin1 裸字节**读写、只改代码区、保留 CRLF；**脚本自撰文本一律 ASCII**（写中文会被截字节 → MSVC `C1071`）。

---

## 7. 下一步（阶段 7）

- 抽**输出链**（编码 / 复用 / 推流 / HLS / 录制）到 `output/` 与 `src/streaming|recording` 侧，目标 `Player.cpp` → 800~1300；
- 先出 `docs/architecture/phase7-plan.md`（只读审计 + 设计 + 子步 + 风险 + 停止条件），再分子步执行；节奏同前（改→Debug→Release→三样例回归→独立提交→push）。
