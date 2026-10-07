# 阶段 4 报告：Player 三角色拆分（Facade / PlaybackSession / MediaContext）

> 目标：把 `core/Player.{h,cpp}`（673 / 4651 行，103 个成员函数）从"上帝对象"分解为
> **Player（Facade）+ PlaybackSession（会话/线程）+ MediaContext（媒体管线聚合）**。
> 硬约束沿用 `refactor-rules.md`：先审计后改、禁止一次性大重构、禁止机械拆文件、不改行为、不顺手修 Bug、不确定不删。

## 1. 提交链（分支 `feature/live-buffer`，均已推送 origin）

| 提交 | 内容 |
|---|---|
| `21753ec` | 阶段 4 计划文档 `docs/architecture/phase4-plan.md` |
| `e2afee2` | **4.1** 建 `core/MediaContext.h`，搬解复用/解码对象 |
| `0d94ae8` | **4.2** 搬音频链（decoder/resampler/speed/device） |
| `f460ca3` | **4.3** 搬队列 + 媒体派生状态 |
| `ab0660e` | **4.4** 建 `core/PlaybackSession.{h,cpp}`，搬会话编排与三线程 |

## 2. 结果指标

| 文件 | 阶段前 | 阶段后 | 非空行 |
|---|---|---|---|
| `core/Player.h` | 673 | **533** | 349 |
| `core/Player.cpp` | 4651 | **3060** | 2405 |
| `core/MediaContext.h` | — | 70 | 47 |
| `core/PlaybackSession.h` | — | 113 | 71 |
| `core/PlaybackSession.cpp` | — | 1626 | 1274 |

> **口径说明（重要偏差）**：重构计划里"Player.cpp 最终 200~500 行"是整个重构（阶段 4~8）的**终点**指标，
> 不是阶段 4 的验收条件。阶段 4 把 Player 从 4651 行降到 3060 行，并**切走职责**（媒体管线、会话线程、解码循环）；
> 剩余体量主要是 `Run()`（渲染主循环 650 行）、输出链（编码/录制/推流/HLS ≈1300 行）与 UI/状态访问器，
> 这些分别由**阶段 5~7**（pipeline / sync / streaming / recording 抽取）继续消化。未做任何"为凑行数的机械搬运"。

## 3. 职责归属（以 `ReleaseMedia` 边界为切分线）

- **MediaContext（随媒体重建）**：`demuxer`、`videoDecoder`、`hwDecoder`、`hwTransferFrame`、
  `audioDecoder`、`audioResampler`、`speedController`、`audioDevice`、
  `videoPacketQueue`、`audioPacketQueue`、`videoNetBuffer`、`useNetBuffer`、`videoFrameQueue`、
  `duration`、`videoFrameDuration`、`hasAudioStream`。
- **PlaybackSession（会话级）**：三线程 + `DemuxLoop/VideoDecodeLoop/AudioDecodeLoop`、
  `OpenMedia/ReleaseMedia/SwitchMedia/StartThreads/StopThreads` 及解码辅助
  （`FlushVideoDecoder/SendVideoPacket/ReceiveVideoFrame/TryInitHardwareDecoder/ProcessAudioFrame/AudioSeekCleanup`）；
  状态 `quit/demuxEof/videoEof/audioEof/audioAbort`、`seekPosition/seekPending/dropAudioUntil`、
  `autoAdvancing`、`reconnectRequested/reconnectAttempts/currentMediaPath`、`switchRequested/switchPath`。
- **Player（保留）**：SDL 会话资源、输出链、平台/全局对象、UI 与高层状态、`Run/Close`、`GetFramePts/HasAudio`。

### 依赖方向（避免循环与双所有权）
- `Player` 持有 `std::unique_ptr<MediaContext> media;` 与 `std::unique_ptr<PlaybackSession> session;`，
  **声明顺序 `media` 在前**（析构逆序 → session 先销毁，media 后销毁，无悬垂）。
- `PlaybackSession` 持 **非拥有引用** `MediaContext& media` + 回指 `Player& owner`；`Player` 以
  `friend class PlaybackSession;` 开放内部。Player 侧需要会话状态时用 `session->xxx`（`PlaybackSession.h` 以
  `friend class Player;` 反向开放）。
- 设计取舍：**不把 MediaContext 的所有权交给 Session**，从而避免对 Player 侧 ~283 处 `media->` 的机械改写；
  会话内因 `media` 是引用，改用 `media.`。这是本阶段"最小扰动"的关键决策。

## 4. 验证证据（每一步都重跑）

- **构建**：`MSBuild FFmpeg_text_claw.vcxproj /p:Configuration=Debug /p:Platform=x64` 与 `Release/x64` 均
  **exit 0 / error 0**；Debug 32 warning、Release 32 warning（阶段前基线 33）。
  其中 30 条 `C4828` 全部来自既有 `FontManager.h`、2 条 `C4244` 来自既有 `Player.cpp`，**无新增告警**。
- **功能回归**（`x64\Release` 就地运行，`--record` + `autoQuitOnEof`）：

  | 样例 | 退出码 | 录制 FLV 时长 |
  |---|---|---|
  | `124662f108eca04d9189d0efae3829c7.mp4` | 0 | 12.833 s |
  | `21e1626495c5d9174868eebab99e437c.mp4` | 0 | 22.655 s |
  | `a4c277.mp4` | 0 | 141.800 s |

  日志无 ERROR/WARN，尾部一致：`Threads Stopped → State : Stopped → All outputs stopped → Closed → [Main] Exit`。
  （尾部另有第二条 `[Player] Closed`：`main.cpp:297 player.Close()` 与 `~Player()` 各调用一次，
  `Close()` 无幂等守卫，属阶段前既有行为，非本次回归。）
- **静态交叉检查**：脚本比对 Player 成员名，确认 `PlaybackSession.cpp` 内**无漏加 `owner.` 的引用**（3 条报错均为注释/字符串误报）。

## 5. 记录的坑（规则 3：禁止凭假设）

1. **局部变量遮蔽成员**：`Player::Run()` 内有局部 `bool quit`，与线程退出标志同名 →
   停留侧**不得**把 `quit` 改写为 `session->quit`（否则 `bool session->quit = false;` 语法错误）。
2. **引用 vs 指针**：会话内 `media` 是引用，搬移块必须 `media->` → `media.`。
3. **源文件编码混合（UTF-8 + GBK）**：本仓库 `core/Player.h` 是 UTF-8，`core/Player.cpp` 是 GBK。
   脚本一律以 **latin1（裸字节）** 读写、只在"代码区"（跳过注释/字符串）替换标识符；
   **作者自撰注释必须 ASCII**——用 latin1 写中文会被截成低字节，MSVC 报 `C1071 在注释中遇到意外的文件结束`。
   搬移过来的源码行保持原 GBK 字节。
4. **CRLF 正则陷阱**：`split('\n')` 后行尾带 `\r`，无 `m` 标志时 `.` 不匹配 `\r`、`$` 只匹配串尾 →
   形如 `/….*$/` 的行锚点断言会失配；行锚点需先剥 `\r`。

## 6. 子步 4.5 结论（合并说明）

4.5 原计划"删转发样板"。本阶段采用**整体搬迁方法体**而非"留转发壳"的做法，
因此核查后**不存在**形如 `Player::StartThreads(){ session->StartThreads(); }` 的转发层：
`Player.cpp` 中均为直接调用 `session->X(...)`。故 4.5 无删除动作，其"行数收敛"目标按 §2 口径
顺延到阶段 5~7，本报告即 4.6 阶段收尾。

## 7. 后续（阶段 5~8）

- 阶段 5：`VideoPipeline/AudioPipeline`（解码→重采样→输出）与 `Queue` 侧收敛；
- 阶段 6：`Demux`/`Sync` 抽取，并处置 `legacy/` 登记项（源码引用 + 构建依赖 + 运行路径三重确认后才删）；
- 阶段 7：`Streaming`（network buffer / monitor）与 `Recording`（encoder / muxer / RTMP / HLS）抽取；
- 阶段 8：反向依赖拆除收口（`Renderer/OSDManager/Event` 对 `Player.h` 的依赖）+ 最终 Review，Player 收敛至 200~500 行。
