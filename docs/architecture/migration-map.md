# 迁移映射表（阶段 2）

> **阶段 2 交付物 · 设计文档（未修改任何源码）**
> 基线：`2af4d01`。配套：`target-architecture.md`、`dependency.md`。
> 本表是**目标映射**，不是已完成的改动；物理执行在阶段 3+。

---

## 1. 文件迁移表（118 个源文件 → 目标目录）

> 全部 118 个 `.cpp/.h` 均在此表内，无遗漏。`Legacy` 项仅隔离（`git mv`），**不删除**。

### 1.1 `app/`（进程入口 + 输入/控制适配）

| 现路径 | 目标 | 动作 |
|---|---|---|
| `main.cpp` | `app/main.cpp` | 移动 |
| `Event.cpp` / `Event.h` | `app/EventController.{cpp,h}` | 移动 + 改名 |
| `Player.h :: ControlBarState` | `app/ControlBar.h` | **拆分**（从 Player.h 移出） |

### 1.2 `core/`（会话与门面）

| 现路径 | 目标 | 动作 |
|---|---|---|
| `Player.cpp` / `Player.h` | `core/PlayerFacade.*` + `core/PlaybackSession.*` + `core/MediaContext.*` | **拆分**（阶段 4~7，见 §3） |
| `PlayerState.h` | `core/PlayerState.h` | 移动 |

### 1.3 `pipeline/`

| 现路径 | 目标 |
|---|---|
| `Input/InputSource.*`, `Input/FileInput.*`, `Input/NetworkInput.*`, `Input/CameraInput.*` | `pipeline/input/` |
| `Demuxer.cpp` / `Demuxer.h` | `pipeline/demux/` |
| `VideoDecoder.cpp` / `VideoDecoder.h` | `pipeline/video/` |
| `AudioDecoder.*`, `AudioResampler.*`, `Audio/PCMQueue.*` | `pipeline/audio/` |
| `Audio/SpeedController.*`, `Audio/AudioSpeedController.*`, `Audio/VolumeController.*` | `pipeline/audio/` |
| `Queue/PacketQueue.*`, `Queue/FrameQueue.*` | `pipeline/queue/` |

### 1.4 `sync/`

| 现路径 | 目标 |
|---|---|
| `Sync/SyncController.*`, `Sync/MasterClock.*`, `Sync/VideoClock.*`, `Sync/AudioClock.*`, `Sync/LiveClock.*`, `Sync/FrameScheduler.*`, `Sync/DropController.*` | `sync/`（原样） |

### 1.5 `output/`

| 现路径 | 目标 |
|---|---|
| `Renderer.cpp` / `Renderer.h` | `output/video/`（去 `#include "Player.h"`） |
| `AudioDevice.cpp` / `AudioDevice.h` | `output/audio/` |
| `OSDManager.*`, `FontManager.*` | `output/osd/` |

### 1.6 `streaming/`

| 现路径 | 目标 |
|---|---|
| `Network/NetworkBuffer.*`, `Network/BufferController.*`, `Network/StreamMonitor.*`, `Network/NetworkStatistics.*` | `streaming/` |

### 1.7 `recording/`

| 现路径 | 目标 |
|---|---|
| `Encoder/VideoEncoder.*`, `Encoder/AudioEncoder.*` | `recording/` |
| `Muxer/Muxer.*`, `Muxer/FLVMuxer.*`, `Muxer/HLSMuxer.*` | `recording/` |
| `Network/RTMPPublisher.*` | `recording/` |

### 1.8 `features/`

| 现路径 | 目标 |
|---|---|
| `Seek/SeekController.*` | `features/seek/` |
| `Playlist/PlaylistManager.*` | `features/playlist/` |
| `Subtitle/SubtitleManager.*` | `features/subtitle/` |
| `Screenshot/ScreenshotManager.*` | `features/screenshot/` |
| `Statistics/PlayerStatistics.*` | `features/statistics/` |

### 1.9 `hardware/` / `config/` / `infra/`

| 现路径 | 目标 |
|---|---|
| `Hardware/CUDAContext.*`, `Hardware/HardwareDecoder.*` | `hardware/` |
| `Config/ConfigManager.*`, `Config/PlayerConfig.h`, `Config/StreamConfig.h` | `config/`（JSON 解析器拆分见 §4）|
| `Utils/Logger.*`, `Utils/ErrorHandler.*`, `Utils/FFmpegPtr.h`, `Utils/DecodeResult.h` | `infra/` |

### 1.10 `legacy/`（⚠️ 只隔离，不删除）

| 现路径 | 归属 |
|---|---|
| `Decoder.cpp` / `Decoder.h` | LEGACY-002 |
| `Input.cpp` / `Input.h` | LEGACY-001 |
| `Screenshot.cpp` / `Screenshot.h` | LEGACY-003 |
| `Audio/AudioMixer.*` | LEGACY-003/005 |
| `Sync/Clock.cpp` / `Sync/Clock.h` | LEGACY-005 |
| `Filter/FilterGraph.*`, `Filter/AudioFilter.*`, `Filter/VideoFilter.*` | LEGACY-005 |
| `Input/RTSPClient.*` | LEGACY-005 |

> 文件计数核对：1.1(3) + 1.2(3) + 1.3(28) + 1.4(14) + 1.5(8) + 1.6(8) + 1.7(12) + 1.8(10) + 1.9(14) + 1.10(18) = **118**，与原源文件总数一致（§4 的待新建文件不在原 118 内；`Player.*` 拆出的目标 `.h/.cpp` 计入 1.2，源侧仍算 2 个文件）。

---

## 2. 类迁移表（57 个类/结构体）

| 类/结构体 | 现所在 | 目标模块 |
|---|---|---|
| `Player`（`Player.h:88`） | `Player.h` | **拆分** → `PlayerFacade` / `PlaybackSession` / `MediaContext` |
| `Player::ControlBarState`（`Player.h:256`） | `Player.h` | `app/ControlBar.h` |
| `PlayerState` | `PlayerState.h` | `core/` |
| `ConfigManager`, `PlayerConfig`, `StreamConfig` | `Config/` | `config/` |
| `InputSource`, `FileInput`, `NetworkInput`, `CameraInput` | `Input/` | `pipeline/input/` |
| `Demuxer` | `Demuxer.h` | `pipeline/demux/` |
| `VideoDecoder` | `VideoDecoder.h` | `pipeline/video/` |
| `AudioDecoder`, `AudioResampler`, `PCMQueue` | 根 / `Audio/` | `pipeline/audio/` |
| `SpeedController`, `AudioSpeedController`, `VolumeController` | `Audio/` | `pipeline/audio/` |
| `PacketQueue`, `FrameQueue` | `Queue/` | `pipeline/queue/` |
| `SyncController`, `MasterClock`, `VideoClock`, `AudioClock`, `LiveClock`, `FrameScheduler`, `DropController` | `Sync/` | `sync/`（原样） |
| `Renderer` | `Renderer.h` | `output/video/` |
| `AudioDevice` | `AudioDevice.h` | `output/audio/` |
| `OSDManager`, `FontManager` | 根 | `output/osd/` |
| `NetworkBuffer`, `BufferController`, `StreamMonitor`, `NetworkStatistics` | `Network/` | `streaming/` |
| `VideoEncoder`, `AudioEncoder` | `Encoder/` | `recording/` |
| `Muxer`, `FLVMuxer`, `HLSMuxer` | `Muxer/` | `recording/` |
| `RTMPPublisher` | `Network/` | `recording/` |
| `SeekController` | `Seek/` | `features/seek/` |
| `PlaylistManager` | `Playlist/` | `features/playlist/` |
| `SubtitleManager` | `Subtitle/` | `features/subtitle/` |
| `ScreenshotManager` | `Screenshot/` | `features/screenshot/` |
| `PlayerStatistics` | `Statistics/` | `features/statistics/` |
| `CUDAContext`, `HardwareDecoder` | `Hardware/` | `hardware/` |
| `Logger`, `LogStream`, `ErrorHandler`, `FFmpegPtr<>` | `Utils/` | `infra/` |
| `Decoder` | `Decoder.h` | **legacy/**（LEGACY-002） |
| `AudioMixer` | `Audio/AudioMixer.h` | **legacy/** |
| `Clock` | `Sync/Clock.h` | **legacy/** |
| `FilterGraph`, `AudioFilter`, `VideoFilter` | `Filter/` | **legacy/** |
| `RTSPClient` | `Input/RTSPClient.h` | **legacy/** |

---

## 3. Player 方法迁移表（103 个 → 目标归属）

| 目标归属 | 迁入的方法（代表，括号为当前行号） |
|---|---|
| `PlayerFacade`（对外 API：查询+命令，转发为主） | `GetState`(1750) `StateToString`(1755) `GetCurrentTime`(1936) `GetDuration`(1941) `GetProgress`(1946) `GetTimeString`(1951) `GetDurationString`(1987) `GetPlaybackSpeed`(1816) `GetVolume`(1843) `IsFullScreen`(1893) `FullScreenToString`(1898) `IsSubtitleEnabled`(1648) `IsHardwareDecode`(2139) `GetStatistics`(2134) `GetNetworkStatistics`(2146) `GetVideoWidth`(2041) `GetVideoHeight`(2052) `HasAudio`(4647) `GetConfigManager`(114) ｜命令：`TogglePause`(1702) `Pause`(1714) `Resume`(1733) `SetPlaybackSpeed`(1791) `SetVolume`(1821) `RequestSeek`(1659) `RequestFrameStep`(1776) `ToggleSubtitle`(1631) `TakeScreenshot`(1848) `ToggleFullScreen`(1866) `PlayPrevious`(1560) `PlayNext`(1582) `AddToPlaylist`(1439) `ToggleRecording`(3131) `TogglePushing`(3295) `ToggleHLS`(3461) |
| `PlaybackSession`（生命周期/装配/协调） | `Player::Player`(21) `~Player`(25) `LoadConfig`(66) `SetLiveBufferOverride`(119) `Init`(129) `OpenMedia`(251) `Run`(740) `Close`(1390) `SwitchMedia`(4262) `ReleaseMedia`(4303) `StartThreads`(3494) `StopThreads`(3520) `ExpandPlaylistWithSiblings`(1453) `SetAutoQuitOnEof`(2035) |
| `MediaContext`（数据宿主，无逻辑） | 原 `Player.h:449–614` 的 26 个 `unique_ptr` + 4 个 SDL 裸指针 + 5 队列 + 时钟成员 |
| `pipeline/video` | `VideoDecodeLoop`(3882) `SendVideoPacket`(3731) `ReceiveVideoFrame`(3745) `FlushVideoDecoder`(3715) `TryInitHardwareDecoder`(3801) `GetFramePts`(4612) |
| `pipeline/audio` | `AudioDecodeLoop`(4116) `ProcessAudioFrame`(4461) `AudioSeekCleanup`(4563) |
| `pipeline/queue` | `PushVideoPacket`(2359) `PushAudioPacket`(2394) `PopVideoPacket`(2426) `PopAudioPacket`(2437) `IsVideoQueueInterrupted`(2444) `IsAudioQueueInterrupted`(2454) `GetVideoQueueSize`(2459) `GetAudioQueueSize`(2469) `GetVideoQueueCapacity`(2474) |
| `features/seek` | `HasSeekRequest`(1907) `GetSeekPosition`(1912) `IsSeekHandled`(1917) `ClearSeekHandled`(1924) `HasFrameStepRequest`(1781) `ClearFrameStepRequest`(1786) `SetCurrentTime`(2010) |
| `recording/*` | `EnsureOutEncoders`(2500) `FeedOutputVideo`(2685) `FeedOutputAudio`(2759) `DispatchVideoPacket`(2818) `DispatchAudioPacket`(2869) `FlushOutEncoders`(2875) `StopAllOutputs`(2939) `ReleaseOutEncoders`(2992) `StartRecording`(3023) `StopRecording`(3095) `StartPushing`(3166) `StopPushing`(3263) `StartHLS`(3309) `StopHLS`(3427) `IsRecording`(3475) `IsPushing`(3480) `IsHLSActive`(3485) |
| `features/statistics` | `UpdateStatistics`(2151) |
| `output/video`（Renderer 内化，去 getter） | `GetWindow`(2030) `GetSwsForFrame`(2063) `GetRGBData`(2109) `GetRGBLinesize`(2114) `GetRGBTexture`(2119) `GetFontManager`(2124) `GetOSDManager`(2129) |
| `app/control` | `GetControlBar`（`Player.h:275` 内联） |

> 说明：上表为**首轮归属**；阶段 4~7 执行时按"先易后难、每步编译"逐项迁移，允许微调并回填。
> 迁移后 `core/PlayerFacade` + `PlaybackSession` 合计应 ≈ 原 Player 的"协调骨架"，其余全部外移。

---

## 4. 待新建类/文件（原 118 之外）

| 新建 | 所在 | 用途 |
|---|---|---|
| `RenderContext`（数据结构） | `output/video/` | 替代 `Renderer → Player*`（帧/进度/状态快照） |
| `StatsSnapshot`（数据结构） | `output/osd/` | 替代 `OSDManager → Player*` |
| `JsonParser`（拆分） | `config/` | 从 `ConfigManager.cpp` 拆出自研解析器（约 479 非空行） |
| `RecordController` / `PushController` / `HlsController`（若需要） | `recording/` | 承接 Player 的输出编排方法 |
| `SeekController` 适配 | `features/seek/` | 接收 `SeekTarget` 而非直接包含 pipeline 类型（阶段 6 再定） |

> 新建类遵循"按需、有真实多调用点才抽"，避免空壳接口。

---

## 5. 阶段 2 完成标准（自检）

- [x] 目标目录树（`target-architecture.md §2`）
- [x] 模块职责（§3）
- [x] 依赖方向（`dependency.md §2` 矩阵 + 反向依赖拆解 §3）
- [x] 文件迁移表（§1，118/118 覆盖）
- [x] 类迁移表（§2，57 类）
- [x] Player 方法迁移表（§3，103 方法首轮归属）
- [ ] **待确认**：目录命名、Legacy 隔离、`Event/ControlBarState` 归属、拆分粒度
