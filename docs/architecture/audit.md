# FFmpegPlayer 架构审计报告（阶段 1 · 只读）

> 本文档是《FFmpegPlayer 企业级架构重构计划》**阶段 1（只读架构审计）**的唯一交付物。
> 阶段 1 规则：**只读、不修改任何源码**。本文件为新建文档，源码零改动。

---

## 1. 审计范围、方法与总览

### 1.1 审计对象（锁定基线）

| 项 | 值 |
|---|---|
| 项目根目录 | `D:\application\visual studio\product\FFmpeg_text_claw` |
| 仓库 | `FFmpegPlayer`（`https://github.com/qieqieqei/FFmpegPlayer.git`） |
| 当前分支 | `feature/live-buffer` |
| HEAD | `2af4d01`（feat: 直播缓冲 v2 —— stable/low_latency 双模式） |
| 同仓 worktree | `regress\golden_src`(dev-85)、`regress\old83_src`(detached)、`regress\q84_src`(detached) —— **本次不审计、不触碰** |
| 审计时工作树状态 | 干净（`git status --short` 无输出）→ 本文件写入后 `docs/` 为新未跟踪目录 |

### 1.2 审计方法

- 只读扫描全部 `.cpp/.h/.hpp/.c`（排除 `x64/Debug/Release/build/vcpkg/packages/.git`）。
- 统计口径：`find /c /v ""`（**含空行**的总行数），另以 PowerShell `Measure-Object -Line`（非空行）交叉验证。
- 依赖关系由脚本解析每个文件的 `#include "..."` 生成**反向 include 图**（"谁包含了它"）。
- 行号证据统一格式 `文件:行`，与仓库当前 HEAD 对应。
- 大文件（`Player.cpp` 4651 行）采用"函数定义清单 + 关键区段精读"而非全文通读，避免遗漏又不至于失真。

### 1.3 ⚠️ 与任务单假设的偏差（重要，先看这条）

计划书多处按"常见 C++ 项目"做了假设，实测与实情有出入，按"禁止凭假设判断"原则在此纠正：

| 任务单假设 | 实测事实 | 影响 |
|---|---|---|
| 构建系统为 **CMake**（`CMakeLists.txt`） | **实为 Visual Studio / MSBuild**：`FFmpeg_text_claw.sln` + `FFmpeg_text_claw.vcxproj` + `.vcxproj.filters`，**无任何 `CMakeLists.txt`** | 后续"每步编译验证"必须用 `MSBuild.exe`，不能用 `cmake --build` |
| 目标是 `Player.cpp < 500 行` | 现状 **4651 行**（非空 ~3649） | 目标差距大，需按计划"达不到须解释"处理 |
| 文档产出路径 `docs/architecture/*` | 仓库**原本不存在 `docs/`**（已按计划新建） | 无冲突 |

> 编译命令基线（供后续阶段用，来自既存记忆与工程结构，本阶段未执行编译）：
> `"D:\application\visual studio\IDE\MSBuild\Current\Bin\MSBuild.exe" FFmpeg_text_claw.vcxproj /p:Configuration=Release /p:Platform=x64 /m`

---

## 2. 项目概况与构建系统

### 2.1 规模

| 指标 | 值 |
|---|---|
| 源文件总数（.cpp/.h/.hpp/.c） | **118** |
| 源码总行数 | **25,429** |
| 构建系统 | MSBuild（VS2022, x64；配置 Debug/Release） |
| 语言标准 | C++17（据既有工程知识；本阶段未验证编译开关） |
| 三方库 | FFmpeg（libavformat/libavcodec/libavutil/libswscale/libswresample）、SDL2 |

### 2.2 单文件规模 Top 20（.cpp，含空行）

| 行数 | 文件 | 备注 |
|---:|---|---|
| 4651 | `Player.cpp` | **上帝对象核心** |
| 994 | `Config/ConfigManager.cpp` | 内含自研 JSON 解析器 |
| 816 | `Decoder.cpp` | ⚠️ **未编译**（Legacy，见 §9） |
| 705 | `Renderer.cpp` | 自由函数集 |
| 632 | `Subtitle/SubtitleManager.cpp` | |
| 557 | `Filter/FilterGraph.cpp` | ⚠️ 已编译但未被引用（见 §9） |
| 521 | `Hardware/HardwareDecoder.cpp` | |
| 479 | `Network/BufferController.cpp` | |
| 449 | `Encoder/AudioEncoder.cpp` | |
| 444 | `OSDManager.cpp` | |
| 439 | `Screenshot/ScreenshotManager.cpp` | |
| 431 | `FontManager.cpp` | |
| 427 | `Network/NetworkBuffer.cpp` | |
| 382 | `Demuxer.cpp` | |
| 368 | `Event.cpp` | |
| 317 | `Statistics/PlayerStatistics.cpp` | |
| 314 | `Network/StreamMonitor.cpp` | |
| 304 | `main.cpp` | |
| 296 | `Queue/PacketQueue.cpp` | |
| 287 | `Muxer/Muxer.cpp` | |

### 2.3 目录/模块划分

`Player`(顶层) · `Event` · `Renderer` · `OSDManager` · `FontManager` · `Config/` · `Demuxer` · 编解码顶层(`VideoDecoder/AudioDecoder/AudioResampler/AudioDevice`) · `Hardware/` · `Queue/` · `Audio/` · `Sync/` · `Network/` · `Filter/` · `Subtitle/` · `Playlist/` · `Seek/` · `Screenshot/` · `Statistics/` · `Encoder/` · `Muxer/` · `Input/` · `Utils/`。

---

## 3. 模块职责表（证据化）

> "被谁依赖"列来自反向 include 图（仅列**项目内**引用者）。**粗体**表示被 `Player.h` 直接聚合（即 Player 持有其实例）。

| 模块 / 关键文件 | 职责 | 被谁依赖 | 备注 |
|---|---|---|---|
| **Core: `Player.h/.cpp`, `PlayerState.h`** | 总控：持有全部子系统、三线程主循环、控制 API、切换媒体 | `main.cpp`, `Event.cpp`, `OSDManager.cpp`, `Renderer.cpp` | 上帝对象，见 §4 |
| `main.cpp` | 入口：解析 CLI、加载配置、建播放列表、`Init/Run/Close` | — | 304 行 |
| `Event.h/.cpp` | SDL 事件/快捷键/鼠标（控制栏交互） | `Player.cpp` | 反向依赖 Player |
| `Renderer.h/.cpp` | `RenderFrame/InitSDL/UpdateWindowTitle/RenderOSD/RenderControlBar`（自由函数） | `Player.cpp` | **反向依赖 Player** |
| `OSDManager.h/.cpp` | OSD 文字 + 字幕纹理叠加 | `Player.h`(持) | 反向依赖 Player |
| `FontManager.h/.cpp` | 字体纹理生成（SDL_ttf 类） | `Player.h`(持), `OSDManager.cpp`, `Renderer.h` | |
| `Config/ConfigManager.h/.cpp` | 读 `player.json`/`stream.json`，含自研 JSON 解析 | `Player.h`(持), `main.cpp`, `Input/*` | 见 §5 |
| `Config/PlayerConfig.h` / `StreamConfig.h` | 纯数据结构（字段+默认值） | `ConfigManager.h`, `Input/*`, `Demuxer.h` 等 | 无逻辑 |
| `Demuxer.h/.cpp` | 解复用：`av_read_frame`、流选择、Seek、abort | `Player.h`(持), `SeekController.cpp` | |
| `VideoDecoder` / `AudioDecoder` | 编解码封装 | `Player.h`(持) | |
| `AudioResampler` | 重采样 | `Player.h`(持) | |
| `AudioDevice.h/.cpp` | SDL 音频设备 + 回调线程 + PCMQueue | `Player.h`(持) | SDL 音频线程 |
| `Hardware/HardwareDecoder` | CUDA/D3D11VA 硬解 | `Player.h`(持) | |
| `Hardware/CUDAContext` | hw device 上下文 | `Player.h`(持), `HardwareDecoder.h`, `Player.cpp` | |
| `Queue/PacketQueue` / `FrameQueue` | 有界阻塞队列（含 `Interrupt`） | `Player.h`(持), `SeekController.cpp` | |
| `Sync/SyncController` + `MasterClock/VideoClock/AudioClock/DropController/FrameScheduler/LiveClock` | 音视频同步、丢帧、直播时钟 | `Player.h`(持)，组件间互引 | |
| `Network/NetworkBuffer` | 直播包缓冲（丢旧、时长上限） | `Player.h`(持) | |
| `Network/BufferController` / `StreamMonitor` / `NetworkStatistics` | 缓冲水位 / 流健康巡检 / 网络统计 | `Player.h`(持) | |
| `Network/RTMPPublisher` | RTMP 推流 | `Player.h`(持) | |
| `Encoder/VideoEncoder` / `AudioEncoder` | 输出侧编码 | `Player.h`(持) | |
| `Muxer/FLVMuxer` / `HLSMuxer` / `Muxer` | 录制/HLS 封装 | `Player.h`(持), `RTMPPublisher.h` | |
| `Subtitle/SubtitleManager` | 字幕加载/渲染 | `Player.h`(持) | |
| `Playlist/PlaylistManager` | 播放列表 | `Player.h`(持) | |
| `Seek/SeekController` | Seek 状态机 | `Player.h`(持), `Demuxer/PacketQueue/FrameQueue` 反引 | |
| `Screenshot/ScreenshotManager` | 截图（PNG/JPG） | `Player.h`(持) | |
| `Statistics/PlayerStatistics` | 播放统计 | `Player.h`(持), `OSDManager.cpp` | |
| `Input/InputSource`(+FileInput/NetworkInput/CameraInput/RTSPClient) | 输入源抽象 | `Demuxer.h`, `InputSource.cpp` | `RTSPClient` 未被引用，见 §9 |
| `Filter/FilterGraph`(+Audio/VideoFilter) | 滤镜图 | 仅内部互引 | **未被 Player 引用**，见 §9 |
| `Utils/Logger` / `ErrorHandler` / `FFmpegPtr` / `DecodeResult` | 日志 / 错误标签 / FFmpeg 智能指针 / 解码结果 | 全项目广泛 | 基础设施 |

---

## 4. Player 全量分析（上帝对象证据）

### 4.1 硬指标

| 指标 | 值 | 证据 |
|---|---:|---|
| `Player.h` | 673 行 | 全部成员声明 |
| `Player.cpp` | 4651 行（非空 ~3649） | — |
| `Player.h` 直接 include 的**项目头文件** | **31** 个（SDK 头 9 个，合计 40） | `Player.h:34-78` |
| `Player` 成员函数（定义于 `Player.cpp`） | **103** 个（含构造/析构） | `Player.cpp` 中 `Player::` 定义 |
| `Player.h` 内联成员函数 | 至少 `GetControlBar()`（`Player.h:275`）等 | `Player.h:256/275` |
| `std::unique_ptr` 数据成员 | **26** | `Player.h:449–614` |
| 裸 SDL 指针成员 | **4**（`window/renderer/texture/rgbTexture`） | `Player.h:583-592` |
| `std::thread` 成员 | 3 | `Player.h:577/579/581` |
| 队列成员（直接持有） | 5（`videoPacketQueue/audioPacketQueue/videoNetBuffer/videoFrameQueue`） | `Player.h:557-575` |

### 4.2 职责堆叠（按函数区间归类，全部为 `Player::` 方法）

`Player.cpp` 的 103 个方法明显横跨**至少 10 类职责**——这是"上帝对象"的直接证据：

| 职责域 | 代表方法（行号） | 数量级 |
|---|---|---|
| 生命周期/装配 | `Init`(129)、`OpenMedia`(251)、`Run`(740)、`Close`(1390)、`ReleaseMedia`(4303)、`SwitchMedia`(4262) | 6 |
| 配置桥接 | `LoadConfig`(66)、`GetConfigManager`(114)、`SetLiveBufferOverride`(119) | 3 |
| 播放列表 | `AddToPlaylist`(1439)、`ExpandPlaylistWithSiblings`(1453)、`PlayPrevious`(1560)、`PlayNext`(1582)、`GetPlaylistIndex/Count` | 6 |
| 传输控制 | `TogglePause`(1702)、`Pause`/`Resume`、`SetPlaybackSpeed`(1791)、`SetVolume`(1821)、`RequestSeek`(1659)、`RequestFrameStep`(1776) | ~12 |
| 状态/查询（大量 getter） | `GetState`(1750)、`StateToString`(1755)、`GetCurrentTime`(1936)、`GetProgress`(1946)、`GetTimeString`(1951)、`GetVideoWidth/Height`、`IsFullScreen` … | **~35** |
| 渲染桥接/资源暴露 | `GetSwsForFrame`(2063)、`GetRGBData`(2109)、`GetRGBTexture`(2119)、`GetWindow`(2030)、`GetFontManager`(2124)、`GetOSDManager`(2129) | ~10 |
| 队列搬运（暴露给自身线程） | `PushVideoPacket`(2359)、`PopVideoPacket`(2426)、`IsVideoQueueInterrupted`(2444)… | ~10 |
| **输出链（录制/推流/HLS）** | `EnsureOutEncoders`(2500)、`FeedOutputVideo`(2685)、`FlushOutEncoders`(2875)、`StartRecording`(3023)、`StartPushing`(3166)、`StartHLS`(3309)、`Stop*` / `Toggle*` | **~20** |
| 线程管理 | `StartThreads`(3494)、`StopThreads`(3520) | 2 |
| **各线程主循环（实现在 Player 内）** | `DemuxLoop`(3578)、`VideoDecodeLoop`(3882)、`AudioDecodeLoop`(4116)、`ProcessAudioFrame`(4461)、`AudioSeekCleanup`(4563) | **5** |
| 解码/硬解 | `SendVideoPacket`(3731)、`ReceiveVideoFrame`(3745)、`TryInitHardwareDecoder`(3801)、`FlushVideoDecoder`(3715) | 4 |
| 截图 | `TakeScreenshot`(1848) | 1 |

**结论（事实陈述）**：Player 同时是：对象容器 + 状态机 + 三个线程的**循环实现体** + 渲染桥接 + 队列中间人 + 播出/录制/推流/HLS 编排者 + 大量 getter 提供者。其 `Run()` 单函数从 `Player.cpp:740` 延伸至 `~1389`（约 650 行），内含事件处理、切歌、主渲染循环、同步等待、丢帧分支。

### 4.3 Player 持有的一切（装配证据）

`Player.h:449–614` 一次性 new 出：`Demuxer`、`VideoDecoder`、`HardwareDecoder`、`AudioDecoder`、`AudioResampler`、`SpeedController`、`AudioDevice`、`SyncController`、`SeekController`、`ScreenshotManager`、`PlayerStatistics`、`NetworkStatistics`、`BufferController`、`StreamMonitor`、`CUDAContext`、`ConfigManager`、`SubtitleManager`、`PlaylistManager`、`FontManager`、`OSDManager`、`VideoEncoder`、`AudioEncoder`、`FLVMuxer`、`RTMPPublisher`、`HLSMuxer`，以及 3 个 `std::thread` 与 4 个裸 SDL 指针。

---

## 5. ConfigManager 专项分析

### 5.1 规模

- `ConfigManager.h` 93 行；`ConfigManager.cpp` **995 行**（非空 752）；`PlayerConfig.h` 48 行；`StreamConfig.h` 104 行。

### 5.2 职责分解（.cpp 按块）

| 块 | 行范围 | 非空行 | 定位 |
|---|---|---:|---|
| includes | 1–13 | 10 | `Logger.h` + 标准库 + `windows.h` |
| **A. 内置 JSON 解析器**（匿名 namespace） | 25–664 | 479 | **≈全文件非空行的 64%** |
| B. 加载入口 | 670–731 | ~60 | `Load` / 访问器 |
| C. URL 工具（static） | 733–784 | 43 | `IsNetworkUrl`/`ProtocolOf`/`StripFileScheme` |
| D. 字段映射 | 786–930 | 105 | `LoadPlayer`(8 键) / `LoadStream`(21 键) |
| E. 路径/文件搜索 | 932–994 | 51 | `FindConfigDir`(Win32 API) / `FileExists` |

### 5.3 关键事实

- **JSON 解析器是自研的**（匿名 namespace，不依赖三方库）：`JsonValue`(28–52，用 `std::map` 存对象、`std::vector` 存数组)、`AppendUtf8`(54–86)、`ParseHex4`(89–120)、递归下降 `JsonParser`(122–565)、取值工具 `Find/GetString/GetNumber/GetInt/GetBool`(568–640)、`ReadFile`(642–662)。支持 Object/Array/String(含 `\uXXXX` 与代理对)/Number/Bool/null。
- **"默认值"不在 .cpp**：默认值位于 `PlayerConfig.h` / `StreamConfig.h` 的字段初始化器；.cpp 采用"读不到就用结构体当前值"的模式（如 `ConfigManager.cpp:809`）。
- **类成员仅 11 个方法 + 2 个数据成员**；越出"配置"职责的是：3 个 `static` URL 工具（C 块）、`FindConfigDir`(Win32 API 目录搜索)、`FileExists`（文件 IO），加匿名 namespace 的解析器/工具共 8 函数 + 1 struct + 1 class。
- **未越界到播放逻辑**：`ConfigManager`（及两个配置结构）**不 include** `Player/Decoder/Renderer`，不触碰播放状态。

**事实性小结**：ConfigManager 偏胖，胖在**内嵌了一个 JSON 解析库 + 通用 URL/路径工具**；其"配置"本体的字段映射逻辑（D 块）只占约 105 非空行。是否拆分属阶段 2 设计问题，本阶段只列事实。

---

## 6. 线程模型

### 6.1 线程清单（实际在跑）

| 线程 | 创建点 | 入口 | 归属对象 |
|---|---|---|---|
| Demux 线程 | `Player.cpp:3498` | `Player::DemuxLoop`(3578) | `Player` |
| Video 线程 | `Player.cpp:3503` | `Player::VideoDecodeLoop`(3882) | `Player` |
| Audio 线程 | `Player.cpp:3508` | `Player::AudioDecodeLoop`(4116) | `Player` |
| SDL 音频回调线程 | `AudioDevice.cpp:51` `SDL_OpenAudioDevice` | SDL 回调（`AudioDevice` 内） | `AudioDevice` |
| （渲染循环） | 无独立线程 | `Player::Run`(740) 主线程内 | `Player` |

> 另有 **Legacy `Decoder` 的两个线程** `demuxThread/decodeThread`（`Decoder.cpp:218/236/249/256`，`Decoder.h:153/155`）——该文件**未编译**，不构成运行时线程（见 §9）。

### 6.2 启停与同步

- `StartThreads()`（`Player.cpp:3494`）：`quit.store(false)` → 依次构造 3 个 `std::thread(&Player::XxxLoop, this)`。
- `StopThreads()`（`Player.cpp:3520`）：先判 `joinable`；`quit.store(true)`；随后 `Interrupt()` 打断 4 个阻塞点（`videoPacketQueue`/`audioPacketQueue`/`videoFrameQueue`/`videoNetBuffer`）；`demuxer->SetAbort(true)` 用于打断网络 `av_read_frame`；最后逐个 `join()`。
- 线程间标志：`quit / demuxEof / videoEof / audioEof / audioAbort`（`std::atomic<bool>`，`Player.h:~640-655`）、`reconnectRequested / reconnectAttempts`（atomic，`Player.h:~535-540`）。
- 同步原语统计：`mutex` 90 处、`atomic` 59 处、`condition_variable` 4 处、`thread` 10 处、`chrono` 36 处。

### 6.3 已知并发注意点（事实）

- `StopThreads` 依赖"`Interrupt()` 唤醒 + `SetAbort(true)` 打断网络读"这一整套约定才能保证 `join` 不卡；任一处缺失即"join 卡死"风险（对应计划中必须"停止并报告"的情形）。
- 存在**多份线程间共享状态且靠原子变量手工同步**的模式（Eof/Abort/Reconnect 等），构成潜在竞态面。

---

## 7. 资源与所有权生命周期

### 7.1 FFmpeg / C++ 资源

| 类别 | 机制 | 证据 |
|---|---|---|
| FFmpeg C 资源 | 自研 `FFmpegPtr<T, Deleter>`（`Utils/FFmpegPtr.h`） | `PacketPtr`/`FramePtr`/`AVFramePtr`/`AVCodecContextPtr`/`AVFormatContextPtr`/`SwsContextPtr`/`SwrContextPtr`/`AVBufferRefPtr`/`AVFilterGraphPtr`；被 16 个头文件引用 |
| C++ 对象 | `std::unique_ptr`（`make_unique`，全项目 `shared_ptr`=0 处） | `Player.h:449–614`（26 个） |
| 队列元素 | `PacketPtr`/`FramePtr` 移动语义（`std::move` 23 处） | 入队 `PacketQueue.cpp:42,66`、`FrameQueue.cpp:41`、`NetworkBuffer.cpp:94`；出队对应 `:106/:81/:141` |

### 7.2 SDL 资源 —— ⚠️ 所有权"创建/销毁分离"

实测发现一处**跨模块所有权切分**（事实，非评价）：

- **创建**在 `Renderer`：`InitSDL(...)`（`Renderer.cpp:186–261`）用 out 参数创建 `SDL_Window*/SDL_Renderer*/SDL_Texture*`，`SDL_Init` 在 `Renderer.cpp:178`；被 `Player.cpp:561` 调用。
- **销毁**在 `Player`：`ReleaseMedia()`（`Player.cpp:4338/4345/4352/4359`）手工 `SDL_DestroyTexture/DestroyRenderer/DestroyWindow` 并置 `nullptr`；`SDL_Quit()` 在 `Player::Close()`（`Player.cpp:1428`）。
- `Player` 侧这 4 个 SDL 资源是**裸指针**（非 RAII，`Player.h:583–592`），销毁靠手写 `if (p){ ...; p=nullptr; }`。
- SDL 音频设备为 RAII-ish：`AudioDevice` 内 `SDL_OpenAudioDevice`(51) / `SDL_CloseAudioDevice`(158)。

### 7.3 其它

- 字体/OSD 纹理：`FontManager.cpp:99/260` 建、`OSDManager.cpp:192/382/423/431` 销。
- CUDA 上下文：`CUDAContext.cpp:40` 建；`AVBufferRef` 走 `FFmpegPtr`（`FFmpegPtr.h:168`）/`av_buffer_unref`（`HardwareDecoder.cpp:150/248`）。
- 输出链编码器/封装器（`outVideoEncoder/outAudioEncoder/recordMuxer/rtmpPublisher/hlsMuxer`）由 `Player::EnsureOutEncoders`(2500)/`ReleaseOutEncoders`(2992) 管理，配 `std::mutex outMutex`（`Player.h:~460`）。

---

## 8. 依赖关系（含反向依赖）

### 8.1 反向 include 图要点（"谁包含了它"）

- **`Player.h` 被 5 个文件包含**：`Player.cpp`、`main.cpp`、`Event.cpp`、`OSDManager.cpp`、`Renderer.cpp`。
  → 其中 **4 个非核心模块反向依赖 Player**（`Event/OSDManager/Renderer` 之外还有 `main`，但 main 属入口合规）。

### 8.2 ⚠️ 反向依赖清单（重点：`Renderer.cpp #include "Player.h"`）

| 文件 | 是否 `#include "Player.h"` | 对 Player 的引用 |
|---|---|---|
| `Renderer.cpp` | **是**（`Renderer.cpp:4`） | 约 20 处 `player->...`：`GetSwsForFrame(37)`、`GetRGBData(40)`、`GetRGBLinesize(43)`、`GetRGBTexture(46)`、`GetVideoWidth/Height(95/97)`、`GetState(275)`、`GetPlaybackSpeed(299)`、`GetProgress(306)`、`FullScreenToString(312)`、`GetVolume(317)`、`GetTimeString(319)`、`GetDurationString(321)`、`GetOSDManager(346)`、`GetWindow(410)`、`GetControlBar(432)`、`GetDuration/GetProgress(510/513)`… |
| `Renderer.h` | 否（仅前置声明 `class Player;`，`Renderer.h:13`） | 仅 `Player*` 形参 |
| `OSDManager.cpp` | **是**（`OSDManager.cpp:5`） | `GetStatistics(68)`、`StateToString(75)`、`GetTimeString(78)`、`GetDurationString(80)`、`IsHardwareDecode(93)`、`GetNetworkStatistics(139/143)`、`GetPlaybackSpeed(151)`、`GetVolume(155)` |
| `OSDManager.h` | 否（前置声明 `class Player;`） | 仅 `Player*` 形参 |
| `Event.cpp` | **是**（`Event.cpp:3`） | 约 25 处：`TogglePause(40)`、`ToggleFullScreen(50/64)`、`GetCurrentTime+RequestSeek(72/81)`、`Get/SetPlaybackSpeed(91/110)`、`TakeScreenshot(118/126)`、`RequestFrameStep(134)`、`Get/SetVolume(143/152)`、`PlayPrevious/Next(169/177)`、`ToggleSubtitle(185)`、`ToggleRecording/Pushing/HLS(193/201/209)`、`GetControlBar(227/281/345)`、`RequestSeek(353)`… |

**次要耦合**：`Renderer/Event` 通过 **`Player::ControlBarState& GetControlBar()`**（`Player.h:256` struct / `Player.h:275` 内联函数）直接读写 Player 的 UI 状态字段（`ui.prevBtn/playBtn/nextBtn/track/seekDragging/seekPreview/hoverButton`）。

**其它值得注意的引用**：`SeekController.cpp` 反向依赖 `Demuxer.h/PacketQueue.h/FrameQueue.h`；`RTMPPublisher.h` 依赖 `FLVMuxer.h`。

### 8.3 目标依赖方向 vs 现状（供阶段 2 用）

计划目标：`App → PlayerFacade → PlaybackSession → Pipeline → Sync/Output`，且**禁止 Renderer/Sync/Pipeline/Infrastructure → Player 反向依赖**。
现状事实：`Renderer.cpp`、`OSDManager.cpp`、`Event.cpp` **均 `#include "Player.h"` 并调用 Player 方法**——与目标直接冲突（`Sync/`、`Queue/` 目前通过 Player 持有/传参，尚未见直接反向 include Player.h）。

---

## 9. Legacy / 死代码清单

> 判据：①是否登记在 `FFmpeg_text_claw.vcxproj`（是否被编译）；②是否存在任何**外部**文件 `#include` 它（反向 include 图）。

| # | 文件（行数） | 编译？ | 被引用？ | 证据 / 说明 |
|---|---|---|---|---|
| 1 | `Decoder.h/.cpp`（183 / 816） | ❌ 未登记 vcxproj | ❌ 仅自包含 | 独立 `Decoder` 类，自带 `demuxThread/decodeThread` 与 `DemuxLoop/DecodeLoop`（`Decoder.h:153/155`）——已被 Player 自身的 3 线程 + `VideoDecoder/AudioDecoder` 取代 |
| 2 | `Input.h/.cpp`（11 / 142） | ❌ 未登记 | ❌ 仅自包含 | 自由函数 `OpenInput(...)`——已被 `Input/InputSource` 取代 |
| 3 | `Screenshot.h/.cpp`（13 / 189） | ❌ 未登记 | ❌ 仅自包含 | 自由函数 `SaveScreenshotBMP(...)`——已被 `Screenshot/ScreenshotManager` 取代 |
| 4 | `Audio/AudioMixer.h/.cpp` | ❌ 未登记 | ❌ 仅自包含 | 未进入工程 |
| 5 | `Sync/Clock.h/.cpp` | ✅ 已编译 | ❌ 无外部引用 | 反向 include 仅 `Clock.cpp` 自身——已被 `VideoClock/AudioClock/MasterClock` 取代 |
| 6 | `Filter/FilterGraph.cpp` + `AudioFilter` + `VideoFilter` | ✅ 已编译 | ❌ 无外部引用 | 三个头仅互相包含，**未被 Player/Demuxer 接入** |
| 7 | `Input/RTSPClient.h/.cpp` | ✅ 已编译 | ❌ 无外部引用 | 仅自包含；未接入 `InputSource` 链路 |

**共性**：第 1–4 项属"历史实现被新实现取代且未清理"；第 5–7 项属"已编译但从未接线"。二者都属计划要求先登记的 **Legacy 清单**，**本阶段不删除**（遵守"不删除无法确认的代码"）。

> 注：反向 include 图中"无任何文件包含的头"为空集——因为上述 Legacy 头都被各自的 .cpp 包含；真正的判据是"**有无外部**引用"（上表已按此判定）。

---

## 10. 风险点与"停止并报告"触发预判

以下为后续阶段**最可能触发计划中"停止并报告"条款**的位置（事实+关联，非结论）：

| 触发条款 | 相关现状 | 依据 |
|---|---|---|
| 编译错误无法定位 | 构建为 MSBuild（非 CMake），计划若按 CMake 走会直接失败 | §1.3 |
| join 卡死 | `StopThreads` 依赖 `Interrupt()×4 + SetAbort(true)` 全套约定（`Player.cpp:3520`） | §6.2 |
| 线程/生命周期改动导致 double-free 或悬垂 | 队列元素靠 `move` 单向流动（23 处）；SDL 资源**创建在 Renderer、销毁在 Player**、且为裸指针 | §7.2 |
| Legacy 不确定性 | 7 项 Legacy/未接线代码（§9），其中 3 项仍被编译 | §9 |
| 反向依赖难以拆除 | `Renderer/OSDManager/Event` 直接 `#include "Player.h"` 且调用大量 Player 方法（§8.2） | §8.2 |
| 行数目标不达 | `Player.cpp` 4651 行 vs 目标 <500 | §4.1 |

---

## 11. 结论与待确认事项

### 11.1 结论（纯事实）

1. 项目是 **MSBuild/VS2022** 工程（非 CMake），共 118 源文件 / 25,429 行。
2. `Player` 是典型**上帝对象**：4651 行 / 103 方法 / 聚合 26 个 `unique_ptr` + 5 队列 + 3 线程 + 4 裸 SDL 指针，且**三个线程主循环就写在 Player.cpp 内**。
3. `ConfigManager` 内嵌**自研 JSON 解析器**（占其非空行 ~64%），并夹带 URL/路径工具；未越界到播放逻辑。
4. 资源所有权整体已 RAII 化（`FFmpegPtr` + `unique_ptr`，零 `shared_ptr`），**唯一例外是 4 个裸 SDL 指针**，且其**创建在 Renderer、销毁在 Player**。
5. 存在 **3 处反向依赖 Player**（`Renderer.cpp`/`OSDManager.cpp`/`Event.cpp`），与目标架构的"禁止反向依赖"直接冲突。
6. 存在 **7 项 Legacy/未接线代码**（3 项未编译、4 项已编译未引用），**尚未清理**。

### 11.2 待确认事项（进入阶段 2 前需要你拍板）

- **确认审计基线**：本文以 `feature/live-buffer @ 2af4d01` 为基线。是否先 **commit / 打 tag** 锁定该状态，再进入阶段 2？
- **构建验证口径**：因实为 MSBuild，计划的"每步编译验证"将改用 `MSBuild.exe` 命令（见 §1.3）。确认可用？
- **Legacy 处置**：§9 的 7 项，阶段 2 是否纳入"待清理"清单（仍**不删**，仅登记与隔离）？
- **行数目标**：`Player.cpp < 500` 与现状 4651 差距极大，阶段 2 如何分步逼近（计划要求"达不到须解释而非硬压"）。

> 阶段 1 到此停止，等待确认后再进入阶段 2（模块拆分设计）。**本阶段未修改任何源码**。
