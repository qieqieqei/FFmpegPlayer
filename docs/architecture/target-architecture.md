# 目标架构设计（阶段 2）

> **阶段 2 交付物 · 设计文档（未修改任何源码）**
> 基线：`2af4d01`（tag `architecture-audit-baseline-2af4d01`）；阶段 1 文档提交：`ad4aaed`
> 本文只定义**目标目录 / 模块职责 / 依赖方向**；**类迁移表 / 文件迁移表**见 `migration-map.md`，**依赖规则与拆解步骤**见 `dependency.md`。
> ⚠️ 本阶段**不重写 Player**。所有改动在阶段 3 起逐步进行。

---

## 1. 设计原则

1. **行为不变**：目录/类重排不改变任何可观察播放行为；每步 MSBuild Debug 验证。
2. **单一职责 + 单向依赖**：上层依赖下层；**禁止任何下层反向依赖 Player/Core**。
3. **依赖倒置而非为抽象而抽象**：只在"确有多实现或多调用者"处引入接口（如 `InputSource` 已是抽象基类）；不新增空壳 interface。
4. **Player 收敛为协调者**：最终只保留"对外 API / 会话生命周期 / 高层状态 / 模块协调"。
5. **Legacy 先隔离后处置**：死代码/未接线代码移入 `legacy/`（仅物理隔离，**不删除**，见 `legacy-register.md`）。

---

## 2. 目标目录树

```text
FFmpeg_text_claw/
├─ app/                          # 最外层：进程入口 + 输入/控制适配（唯一允许"知道全部"的壳）
│   ├─ main.cpp
│   ├─ EventController.{h,cpp}   # ← 原 Event.cpp（SDL 事件 → Facade 命令）
│   └─ ControlBar.h              # ← 原 Player::ControlBarState（UI 状态移出 Player）
│
├─ core/                         # 会话与门面（收敛后的 Player）
│   ├─ PlayerFacade.{h,cpp}      # 对外稳定 API（门面）
│   ├─ PlaybackSession.{h,cpp}   # 会话生命周期 / 装配 / 协调 / 主循环
│   ├─ MediaContext.{h,cpp}      # 持有 demux/decoders/queues/clocks 等子系统（原 Player 的 26 个 unique_ptr）
│   └─ PlayerState.h             # 高层状态枚举（沿用）
│
├─ pipeline/                     # 数据流水线（自下而上：input → demux → decode → queue）
│   ├─ input/                    # InputSource / FileInput / NetworkInput / CameraInput
│   ├─ demux/                    # Demuxer
│   ├─ video/                    # VideoDecoder
│   ├─ audio/                    # AudioDecoder / AudioResampler / PCMQueue
│   │                            #   + SpeedController / AudioSpeedController / VolumeController
│   └─ queue/                    # PacketQueue / FrameQueue
│
├─ sync/                         # A/V 同步（沿用，已是独立层）
│   └─ SyncController / MasterClock / VideoClock / AudioClock / LiveClock
│      / FrameScheduler / DropController
│
├─ output/                       # 输出侧（消费 pipeline，产出画面/声音/OSD）
│   ├─ video/                    # Renderer（去掉 #include "Player.h"）
│   ├─ audio/                    # AudioDevice
│   └─ osd/                      # OSDManager / FontManager
│
├─ streaming/                    # 直播网络层
│   └─ NetworkBuffer / BufferController / StreamMonitor / NetworkStatistics
│
├─ recording/                    # 输出侧编/封/推
│   └─ VideoEncoder / AudioEncoder / Muxer / FLVMuxer / HLSMuxer / RTMPPublisher
│
├─ features/                     # 可选功能（全部通过接口/上下文与 core 交互，不反向依赖 Player）
│   ├─ seek/                     # SeekController
│   ├─ playlist/                 # PlaylistManager
│   ├─ subtitle/                 # SubtitleManager
│   ├─ screenshot/               # ScreenshotManager
│   └─ statistics/               # PlayerStatistics
│
├─ hardware/                     # CUDAContext / HardwareDecoder
│
├─ config/                       # ConfigManager / PlayerConfig / StreamConfig / JsonParser(拆分)
│
├─ infra/                        # Utils：Logger / ErrorHandler / FFmpegPtr / DecodeResult
│
└─ legacy/                       # ⚠️ 隔离区（只搬不删，阶段 6 再定去留）
    └─ Decoder.* / Input.* / Screenshot.* / AudioMixer.*
       / Clock.* / Filter* / RTSPClient.*
```

> 物理移动文件（`git mv`）属阶段 3+ 动作。本阶段仅定义映射（见 `migration-map.md`）。

---

## 3. 模块职责表（目标）

| 目标模块 | 职责（只做这些） | **不得**包含 |
|---|---|---|
| `app/` | 进程入口、CLI、SDL 事件/快捷键/鼠标 → 转成 Facade 调用 | 任何播放实现 |
| `core/PlayerFacade` | 对外 API 门面（薄）；参数校验与转发 | 线程实现、队列搬运、解码、同步算法 |
| `core/PlaybackSession` | 会话生命周期、子系统装配/释放、主循环、切媒体、模块协调 | Demux/Decode/Queue/Sync/Render/编码的**实现** |
| `core/MediaContext` | 持有并暴露子系统实例（数据宿主） | 行为逻辑 |
| `pipeline/*` | 拉流→解复用→解码→入队（纯数据流） | 渲染、同步决策、UI |
| `sync/*` | 音视频时钟、丢帧、帧调度、直播时钟 | 渲染、解码 |
| `output/*` | 渲染、音频播放、OSD/字体 | 拉流、解码 |
| `streaming/*` | 直播缓冲/水位/健康巡检/统计 | 编解码 |
| `recording/*` | 编码、封装、推流、HLS | 输入侧解码 |
| `features/*` | 独立功能（Seek/Playlist/Subtitle/Screenshot/Statistics） | 线程/渲染 |
| `hardware/*` | 硬解上下文与解码器 | 业务逻辑 |
| `config/*` | 配置读取（含 JSON 解析器拆分） | 播放逻辑 |
| `infra/*` | 日志、错误标签、FFmpeg 智能指针 | 业务逻辑 |

---

## 4. 分层与依赖方向（目标）

分层（数字越小越底层）：

```text
L6 app
L5 core (PlayerFacade → PlaybackSession → MediaContext)
L4 streaming / recording / features
L3 output (video/audio/osd)
L2 sync
L1 pipeline (input/demux/video/audio/queue)
L0 infra  +  config  +  hardware
```

**允许**：`L(n) → L(m), m < n`（严格向下）。
**禁止**（当前违反项，须在阶段 3~8 拆除）：

```text
output/video  (Renderer)    → core/Player        ❌ 现状存在
output/osd    (OSDManager)  → core/Player        ❌ 现状存在
app           (Event)       → core/Player        ❌ 现状存在（应只依赖 PlayerFacade）
infra / pipeline / sync     → core/Player        ❌ 需持续守住
```

> 现状反向依赖证据与拆解办法见 `dependency.md`。

---

## 5. Player 拆分目标

| 目标类 | 从 Player 迁入的职责 | 预估占比 |
|---|---|---|
| `PlayerFacade` | 对外 API：Play/Pause/Seek/Speed/Volume/Time/State/Screenshot/Recording 开关等（转发为主） | ~10% |
| `PlaybackSession` | `Init/OpenMedia/Run/Close/SwitchMedia/ReleaseMedia`、线程启停、播放列表导航、协调 | ~25% |
| `MediaContext` | 26 个 `unique_ptr` + 5 队列 + 时钟等**数据成员宿主** | ~15%（纯持有） |
| `pipeline/video`+`pipeline/audio` | `VideoDecodeLoop/AudioDecodeLoop/ProcessAudioFrame/Send*/Receive*/Flush*` 等解码循环 | ~25% |
| `pipeline/queue` | `Push/Pop*Packet`、`Is*QueueInterrupted`、`Get*QueueSize` 等队列搬运 | ~8% |
| `features/*` | Seek/Playlist/Subtitle/Screenshot/Statistics 相关方法 | ~10% |
| `recording/*` | `EnsureOutEncoders/Feed*/Dispatch*/Start*/Stop*/Toggle*`（录制/推流/HLS） | ~7% |

> 目标：Player.cpp 由 **4651 → 200~500 行**，且**职责收敛**（非机械搬运）。渐进里程碑见 `refactor-rules.md §3.4`。

---

## 6. 阶段化迁移顺序（每步独立 commit + Debug 编译）

```text
阶段 3  建立目标目录骨架 + legacy/ 隔离（仅 git mv 死代码，不删）
阶段 4  抽 PlaybackSession + MediaContext（Player 4651 → 3000~3500）
阶段 5  抽 VideoPipeline / AudioPipeline / Queue（→ 1500~2000）
阶段 6  Legacy 三重确认处置 + 抽 Demux / Sync 接入（→ 800~1300）
阶段 7  抽 Streaming / Recording / Features（→ 300~600）
阶段 8  反向依赖拆除收口 + 最终 Review（→ 200~500）
```

> 每步：修改 → `MSBuild Debug/x64` → 运行/针对性测试 → 通过才继续；每阶段末跑 `MSBuild Release/x64`。

---

## 7. 待确认项

- 目标目录是否采用上述 `src/` 式分目录（app/core/pipeline/...）？
- 是否同意把 Legacy 7 项先 `git mv` 到 `legacy/`（不删）？
- `Event` + `ControlBarState` 归入 `app/`（输入/控制适配层）是否可接受？
- Player 拆分粒度是否按 §5 / §6（Facade + Session + Context 三件套）？
