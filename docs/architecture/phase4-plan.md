# 阶段 4 执行计划：抽 PlaybackSession + MediaContext

> 目标：把 `core/Player.{h,cpp}`（673 / 4651 行，103 个成员函数）从"上帝对象"收敛为
> **Facade + Session + Context** 三角色，Player 最终 **200~500 行**。
> 硬约束（沿用 refactor-rules.md）：**禁止机械拆文件**（不许 PlayerHelper/PlayerUtils/PlayerManager 式搬运）；
> 每子步 `改 → MSBuild Debug|x64 → 运行 → 回归 → 确认`；不改行为；不顺手修 Bug；不确定不删。

## 1. 角色划分（本阶段锁定）

| 角色 | 职责 | 生死周期 |
|---|---|---|
| **Player**（Facade，暂仍名 `Player`，改名留到阶段 8） | 对外 API、会话生命周期、高层状态、模块协调 | 进程级（含 SDL 会话） |
| **PlaybackSession** | 一次播放会话：open→三线程→render→close 的编排与线程/退出控制 | 每"当前媒体会话" |
| **MediaContext** | 被会话持有的**管线对象集合 + 媒体派生状态**（纯聚合，无逻辑） | 随 OpenMedia/ReleaseMedia 生死 |

## 2. 成员归属（按"随媒体生死"切）

### MediaContext（随媒体重建）
- 管线对象：`demuxer`、`videoDecoder`、`hwDecoder`、`hwTransferFrame`、
  `audioDecoder`、`audioResampler`、`speedController`、`syncController`、`seekController`
- 队列：`videoPacketQueue`、`audioPacketQueue`、`videoNetBuffer`、`videoFrameQueue`
- 媒体派生状态：`hasAudioStream`、`duration`、`videoFrameDuration`、`currentMediaPath`、
  `lastVideoDropped/lastAudioDropped`

### PlaybackSession（会话级）
- 三线程 `demuxThread/videoThread/audioThread` + 入口 `DemuxLoop/VideoDecodeLoop/AudioDecodeLoop`
- `StartThreads/StopThreads/OpenMedia/ReleaseMedia/SwitchMedia`
- 线程间标志 `quit / demuxEof / videoEof / audioEof / audioAbort`
- seek 状态 `seekPosition/seekPending/dropAudioUntil`、重连 `reconnectRequested/Attempts`

### Player（保留）
- SDL 会话：`window/renderer/texture/rgbTexture/swsCtx/rgbData`
- 输出链：`outVideoEncoder/outAudioEncoder/recordMuxer/rtmpPublisher/hlsMuxer/outMutex/...`
- 平台/全局：`configManager`、`playlistManager`、`subtitleManager`、`fontManager/OSDManager`、
  `statistics/networkStatistics/bufferController/streamMonitor/cudaContext`
- UI/高层状态：`state/playbackSpeed/volume/currentTime/progress/fullscreen/frameStepRequest`、`uiBar`

> 归属以"随媒体重建 vs 会话保留"为准；`ReleaseMedia` 边界即为切分线。

## 3. 子步（每步独立编译+运行+提交前验证）

| 步 | 内容 | 风险 | 验证 |
|---|---|---|---|
| 4.1 | 建 `core/MediaContext.h`（纯聚合 struct），先搬**解码/解复用一小组**：`demuxer/videoDecoder/hwDecoder/hwTransferFrame` | 低 | Debug|x64 0 error + 跑样例 |
| 4.2 | 搬音频侧：`audioDecoder/audioResampler/speedController/syncController/seekController` | 低 | 同 |
| 4.3 | 搬队列：`videoPacketQueue/audioPacketQueue/videoNetBuffer/videoFrameQueue` 及媒体派生状态 | 中 | 同 |
| 4.4 | 建 `core/PlaybackSession.{h,cpp}`：搬 `OpenMedia/ReleaseMedia/SwitchMedia/StartThreads/StopThreads/三 Loop` + 线程/退出标志；`Player` 转发 | 高 | Debug+Release 0 error + 全功能回归 |
| 4.5 | 收尾：删转发样板，Player 收敛至 **200~500 行**（超 500 需解释职责收敛） | 中 | 行数统计 + 回归 |
| 4.6 | 阶段提交：`refactor: phase 4 ...` + 本阶段报告 docs | — | Debug+Release + 提交 |

## 4. 回归方式（GUI 项目的"运行"口径）

本项目为 SDL GUI，无 probe 脚本。用 **CLI 输出模式**做自动化回归：
- `FFmpeg_text_claw.exe --record out.flv <sample>`（`autoQuitOnEof` ），检查退出码 + 产物可解析；
- `--hls` / `--push` 同理；
- 需要一份本地样例媒体文件作为固定输入。

## 5. 停止并报告的条件（沿用）

编译错误无法定位 / 死锁 / join 卡死 / double-free / 长稳恶化 / Legacy 行为不确定 → **立即停止并报告**，不继续下一步。
