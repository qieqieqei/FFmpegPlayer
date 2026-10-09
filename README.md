# FFmpegPlayer

基于 **C++17 + FFmpeg 8.x + SDL2** 开发的 Windows 多线程音视频播放器（MSVC / Visual Studio 工程）。

> GitHub：https://github.com/qieqieqei/FFmpegPlayer

项目目标是深入了解音视频播放器底层架构，实现从 **媒体读取、解封装、音视频解码、同步控制到音视频输出** 的完整播放流程。模块化设计：播放器拆分为多个独立模块，通过线程安全队列连接，实现解码、渲染和控制逻辑解耦。

---

## 功能清单

### 模块状态总表

| 模块 | 状态 | 说明 |
|---|---|---|
| `core/`（Player 门面 / PlaybackSession / MediaContext） | ✅ 完成 | 对外 API 门面 + 会话装配 / 主循环；内部实现已下沉，`Player.cpp` 收敛到 341 行 |
| `pipeline/input/`（InputSource / File / Network / Camera） | ✅ 完成 | 协议工厂 + 超时注入 + 中断回调 |
| `Sync/`（Master/Video/Audio/LiveClock + Scheduler + Drop） | ✅ 完成 | 音频主时钟 + ffplay 级目标延迟调整 + 丢帧追赶 + 墙钟漂移校正 |
| `Hardware/`（CUDAContext / HardwareDecoder） | ✅ 解码 / 🧪 渲染未做 | NVDEC/D3D11VA/DXVA2 解码 + 软解回退；**GPU 零拷贝渲染未实现**（回读 CPU） |
| `pipeline/queue/` + `streaming/NetworkBuffer` | ✅ 完成 | RAII 所有权 + 谓词等待 + GOP 感知丢包 |
| `pipeline/video` + `pipeline/audio`（解码） | ✅ 完成 | DecodeResult 三态（Success/NeedMore/End/Error） |
| `recording/`（VideoEncoder / AudioEncoder） | ✅ 完成 | libx264/libx265/nvenc；AAC/Opus |
| `recording/`（Muxer / FLVMuxer / HLSMuxer） | ✅ 完成 | FLV 文件 / RTMP 推流、HLS 自动切片清理 |
| `legacy/`（FilterGraph / VideoFilter / AudioFilter） | ⚠️ 已退役 | avfilter 封装曾实现但**从未接线**（LEGACY-005），已隔离到 `legacy/`，不参与编译 |
| `features/subtitle/` | 🧪 实验性 | .srt 解析 + OSD 渲染已实现，**尚未用真实字幕文件端到端回归** |
| `features/screenshot/` | ✅ 完成 | PNG/JPG |
| `features/playlist/` | ✅ 完成 | 多文件连播（上一首/下一首/自动连播） |
| `features/seek/` | ✅ 完成 | 关键帧定位 + 解码器 flush + 代数防竞态 |
| `streaming/` + `recording/RTMPPublisher`（统计/缓冲/重连/推流/监控） | ✅ 完成 | 丢包率、缓冲水位、自动重连、RTMP 推流、健康巡检 |
| `Config/` | ✅ 完成 | 自研轻量 JSON 解析，player.json + stream.json |

> 表中为当前实际路径；旧路径对照见「项目目录结构」。

### 播放核心
- ✅ 本地文件 / 网络流播放：MP4 等常见封装（FFmpeg 解封装）；rtsp / rtmp / http / https / HLS
- ✅ H.264 / H.265 视频解码（libavcodec）；AAC 等音频解码 + 重采样（SwrContext，统一 S16 / 48000Hz / 立体声）
- ✅ 三线程架构：Demux 线程 / Video Decode 线程 / Audio Decode 线程（主线程渲染）
- ✅ PacketQueue / FrameQueue 线程安全缓冲：RAII 所有权（`PacketPtr` / `FramePtr`）+ 谓词等待背压（满则阻塞，暂停即自停）
- ✅ 音频主时钟音视频同步（视频提前 ≤100ms 分片等待、落后 >50ms 丢帧；无音频按帧率播放）
- ✅ Seek 跳转（`av_seek_frame` + flush + 清队列 + 时钟重置 + 中断保护）
- ✅ 暂停 / 恢复、变速播放（0.5x / 1x / 1.5x / 2x）、音量控制

### 网络与直播
- ✅ 统一输入层：`InputSource` 抽象基类 + 工厂 —— `FileInput`（本地文件 / `file://`）、`NetworkInput`（rtsp / rtmp / http / https / HLS）、`CameraInput`（RTSP 摄像头）；按协议注入 `rtsp_transport=tcp`、超时、`fflags=nobuffer` 等选项
- ✅ 直播 / 点播自动识别：时长未知的网络流按直播处理（禁 Seek、低延迟缓冲），点播流可 Seek
- ✅ 中断回调 `SetAbort()`：可打断阻塞中的网络读取，退出 / 切换媒体不卡死
- ✅ 断网自动重连：直播流断流 → 重连 → 自动恢复渲染；`reconnect_max_attempts` / `reconnect_delay_ms` / `reconnect_backoff_factor` 可配（≤0 = 无限重试，适合 24h 无人值守）
- ✅ 低延迟直播：
  - 打开参数 `fflags=nobuffer` + `flags=low_delay`
  - 解码级 `SetLowDelay()`（硬件 + 软解两处）
  - 直播队列 `LiveMode`：满不阻塞、按队列时长丢旧包追最新画面（GOP 感知，避免花屏）
  - `Sync/LiveClock`：超前 >100ms / 落后 >50ms 直接丢帧追实时，`NextWaitMs` 恒为 0
  - `CameraInput` 目标延迟 `camera_latency_ms`（0 = 极限低延迟）
  - 实测：局域网 RTSP 摄像头端到端约 300ms
- ✅ 直播缓冲：`live_buffer_mode` = `stable`（默认，滞回缓冲抗抖动）/ `low_latency`（追最新，约 300–600ms）；CLI `--live-buffer <ms>`
- ✅ 丢包可观测 + 网络统计：`NetworkBuffer` 丢弃计数同步 `NetworkStatistics`（OSD `Net` 行显示 `Loss %`）；1s 滑动窗口统计输入/输出 FPS、码率、丢包率、缓冲水位、延迟估算
- ✅ 缓冲控制 `BufferController`（低/高水位模型）+ 流监控 `StreamMonitor`（每秒巡检，阈值告警：延迟 ≥500ms、丢包 ≥1%、无数据 5s 判断流）
- ✅ 配置系统 `Config/`：`player.json`（窗口 / 音量 / 速度 / 默认 URL / 日志）+ `stream.json`（RTSP/RTMP/编码/缓冲/HLS/滤镜参数），缺失用默认值

### 编码 / 封装 / 推流
- ✅ 视频编码 `VideoEncoder`：libx264 / libx265 / h264_nvenc（直播低延迟：libx264 `tune=zerolatency`、nvenc `preset=ll` + `bf=0`），GOP=2s，输入 YUV420P
- ✅ 音频编码 `AudioEncoder`：AAC / Opus（内部 swr 自动重采样为编码器所需格式）
- ✅ 封装 `Muxer`：`FLVMuxer`（.flv 文件或 rtmp://，支持关键帧起播）、`HLSMuxer`（原生 hls muxer，`hls_time` / `hls_list_size` / `delete_segments` 自动切片与清理）
- ✅ RTMP 推流 `RTMPPublisher`：FLV 封装 + rtmp 协议，断线重连，从关键帧开始推
- ✅ Player 集成：录制 / 推流 / HLS 开关接入播放主流程，CLI `--record` / `--push` / `--hls` 启动即输出，EOF 后 3s 自动退出（批处理友好）
- ⚠️ FLV / RTMP 格式限制：仅支持 H.264 + AAC（Opus 不能走 FLV/RTMP 路径）

### 硬件解码
- ✅ `CUDAContext`：CUDA → D3D11VA → DXVA2 自动探测降级
- ✅ `HardwareDecoder`：硬解 + 软解自动回退；`stream.json` 的 `"hardware_decode"`（默认开）优先走硬解，失败自动回退软解；OSD 如实标注 `(HW decode)` / `(SW decode)`
- ⚠️ 渲染仍为 `av_hwframe_transfer_data` 回读 CPU → SDL 纹理，**GPU 零拷贝渲染未实现**（SDL2 无 CUDA/D3D11 互操作 API）

### 进阶功能
- ✅ 播放列表：多文件播放、上一首 / 下一首、EOF 自动播下一首
- ✅ 字幕：自动加载同名 .srt / .ass，OSD 底部渲染，可开关（实验性，尚未用真实字幕文件端到端回归）
- ✅ OSD：进度 / 时间 / 状态 / 提示，中文字体（`Font/simhei.ttf`）
- ✅ 截图 PNG / JPG（`S` / `J` 键）；帧步进（`N`，暂停时）；全屏切换（`F`）
- ✅ 自动截图（回归核对用）：CLI `--screenshot-at <秒>` 把合成画面（视频 + OSD + 控制栏）读回存 24 位 BMP，`--screenshot-file` 指定输出名
- ✅ 日志系统：级别过滤（DEBUG/INFO/WARN/ERROR）、时间戳、`-v` 开 DEBUG、`--log-file` 写文件

---

## 快捷键

| 按键 | 功能 |
| ---- | ---- |
| `Space` | 暂停 / 恢复 |
| `R` | 循环变速（0.5x → 1x → 1.5x → 2x） |
| `←` / `→` | 后退 / 前进 5 秒 |
| `[` / `]` | 上一首 / 下一首（播放列表） |
| `T` | 字幕开关 |
| `S` / `J` | 截图 PNG / JPG |
| `N` | 帧步进（暂停时） |
| `+` / `-` | 音量 +10 / -10 |
| `F` | 全屏 |
| `ESC` / `Q` | 退出 |

**控制栏（鼠标）**：底部半透明控制条 —— ⏮ 上一首 / ⏯ 暂停恢复 / ⏭ 下一首；点击进度条或拖动滑块 seek。

---

## 命令行用法

```bat
FFmpeg_text_claw.exe [文件1] [文件2] ...        # 多文件加入播放列表
FFmpeg_text_claw.exe -v file.mp4                # DEBUG 级别日志
FFmpeg_text_claw.exe --log-file player.log file.mp4   # 同时写日志文件
FFmpeg_text_claw.exe --record file.mp4          # 播放同时录制 FLV（record_*.flv）
FFmpeg_text_claw.exe --hls file.mp4             # 播放同时输出 HLS（hls_out/）
FFmpeg_text_claw.exe --push rtmp://host/live/stream file.mp4   # 播放同时 RTMP 推流
FFmpeg_text_claw.exe --screenshot-at 10 file.mp4               # 播到第 10 秒把合成画面（视频+OSD+控制栏）存成 BMP
FFmpeg_text_claw.exe --screenshot-at 10 --screenshot-file out.bmp file.mp4   # 指定输出文件名
```

不带参数时播放默认视频 `D:\application\visual studio\product\FFmpeg_text_claw\a4c277.mp4`（代码内 `kDefaultVideo` 常量，硬编码绝对路径，可自行修改）。

---

## 播放器架构

重构后为**分层单向依赖**架构：`app`（入口 / 输入适配）→ `core`（门面 + 会话）→ 功能层 → `pipeline` / `infra`。
`core/Player` 只做对外门面，装配、主循环与模块协调在 `core/PlaybackSession`，子系统实例由 `core/MediaContext` 持有。

```text
main() → Player(门面) → PlaybackSession(装配 / 生命周期 / 主循环) → MediaContext{ demux · decoder · queue · clock · renderer · encoder … }

L6  app/            main.cpp · EventController                     进程入口 + SDL 事件适配
L5  core/           Player(门面) → PlaybackSession → MediaContext  对外 API / 会话生命周期 / 协调
L4  features/ · streaming/ · recording/                            可选功能 / 直播缓冲 / 编码封装推流
L3  output/         video(Renderer,VideoPresenter) · audio(AudioDevice) · osd(OSDManager,FontManager)
L2  Sync/           Master/Video/Audio/LiveClock · SyncController · FrameScheduler · DropController
L1  pipeline/       input → demux → video|audio decode → queue     纯数据流
L0  infra/ · config/ · Hardware/                                   日志/错误/FFmpegPtr · 配置 · 硬解
```

依赖规则：只允许**上层依赖下层**（`L(n) → L(m), m < n`）；`pipeline` / `Sync` / `output` / `infra` 不得反向依赖 `core/Player`。
依赖实测：全仓 include 扫描 **129 文件 / 247 条边 / 0 违规**，头文件级 **69 个 / 0 循环**；`core/Player.h` 的引用方仅 `core/` 自身 + `app/`。

### 线程模型

| 线程 | 职责 |
| ---- | ---- |
| Demux 线程 | `av_read_frame` → 按流类型分发到 Video/Audio PacketQueue |
| Video Decode 线程 | 取视频包 → 解码 → clone 入 FrameQueue（最多 12 帧） |
| Audio Decode 线程 | 取音频包 → 解码 → 重采样 → 变速 → Push 到 PCMQueue |
| 主线程（Render，`PlaybackSession::RunLoop`） | 取视频帧 → 同步等待/丢帧 → SDL 渲染 + OSD + 事件处理 |

### 同步策略（音频主时钟）

- 音频播放更稳定，作为时间基准（AudioClock = base + played × speedFactor）
- 视频提前：按剩余时间分片等待（≤100ms），避免阻塞过久
- 视频落后 >50ms：直接丢帧追赶
- 无音频流：按 `frameDuration / speed` 均匀播放（降级为 Video only mode）
- 直播：`Sync/LiveClock` 策略——超前 >100ms / 落后 >50ms 直接丢帧追实时，且 `NextWaitMs` 恒为 0（见「网络与直播」）

### Seek 流程

```
用户按 ←/→ → SeekController::Request → Run 循环消费
  → 打断三队列（Interrupt）→ av_seek_frame → 清空队列
  → ResetInterrupt → 解码器 flush → 时钟重置 → seekGeneration++
```

### 队列背压

- `MAX_VIDEO_PACKETS = 120`、`MAX_AUDIO_PACKETS = 60`、`MAX_VIDEO_FRAMES = 12`（`core/Player.h` 文件级常量，使用方为 `PlaybackSession`）
- 队列满时生产者**谓词等待**（非轮询），暂停即背压自停（不空转 CPU）
- 直播路径改走 `streaming/NetworkBuffer`（满丢最旧 + GOP 感知）；点播仍走 `pipeline/queue` 背压队列

---

## 编译说明（Windows / MSVC）

### 环境要求

- Windows 10/11 + Visual Studio 2022（含 C++ 桌面开发）
- FFmpeg 8.x（本项目使用 avcodec-62 / avutil-60 等 8.x 库，8.x 已移除 `AVCodecContext::qscale`，mjpeg 画质改用私有选项 `q`）
- SDL2 2.32.8
- SDL2_ttf 2.24.0（OSD 中文字体渲染）

### 依赖目录（vcxproj 中配置）

| 依赖 | 路径 |
| ---- | ---- |
| FFmpeg | `D:\FFmpeg`（含 include / lib，dll 与 exe 同目录） |
| SDL2 | `D:\SDL2`（2.32.8） |
| SDL2_ttf | `D:\library\SDL2_tff\SDL2_ttf-2.24.0` |
| 中文字体 | `Font\simhei.ttf`（项目内，随 exe 输出目录一起拷贝） |

> 换机器编译时需同步修改 vcxproj 中的 Include/库路径和 DLL 拷贝路径，或改用环境变量。

### 编译步骤

```bat
:: 命令行 MSBuild（也可直接用 Visual Studio 打开 sln）
"D:\application\visual studio\IDE\MSBuild\Current\Bin\MSBuild.exe" ^
  FFmpeg_text_claw.vcxproj /p:Configuration=Release /p:Platform=x64 /m
```

或打开 `FFmpeg_text_claw.sln` → 生成 → 重新生成解决方案（Debug/Release + x64）。

输出：`x64\Release\FFmpeg_text_claw.exe`（构建后需把 FFmpeg/SDL2 DLL 与 Font 目录放到 exe 旁）。

### 编译注意事项（踩坑记录）

1. **源码编码**：源文件为 UTF-8 无 BOM 且含中文注释，所有 `.cpp` 必须加编译选项 `/utf-8`，否则 MSVC 按 GBK 解析产生乱码/警告（C4828）。
2. **工程根相对 include**：`#include "pipeline/audio/PCMQueue.h"`、`#include "core/Player.h"` 这类**从工程根起的相对路径**引用，依赖 `$(ProjectDir);` 已加入 AdditionalIncludeDirectories。
3. **新增 .cpp 文件**：必须手动注册进 `.vcxproj` 和 `.vcxproj.filters`，否则不会被编译。
4. **FFmpeg 8.x**：`qscale` 字段已移除，mjpeg 编码质量用 `av_opt_set_int(ctx, "q", 8, 0)`。
5. **SDL_MAIN_HANDLED**：main.cpp 顶部已定义，避免 SDL 改写 Win32 入口。

---

## 项目目录结构

```
FFmpeg_text_claw
├── app/                     # 入口 + 输入适配（唯一允许"知道全部"的壳）
│   ├── main.cpp             # 命令行解析（-v / --log-file / --record / --push / --hls / --screenshot-at / --live-buffer）+ 播放列表
│   └── EventController.{h,cpp}   # SDL 事件 / 快捷键 / 鼠标 → Player 门面命令（实现 core/IInputHandler）
├── core/                    # 会话与门面
│   ├── Player.{h,cpp}       # 对外 API 门面（薄；≈ 目标架构里的 PlayerFacade）
│   ├── PlaybackSession.{h,cpp}   # 会话生命周期 / 子系统装配与释放 / 主循环 / 切媒体
│   ├── MediaContext.h       # 子系统实例宿主（demux / decoder / queue / clock …）
│   ├── IInputHandler.h      # 输入处理接口（由 app 注入）
│   └── PlayerState.h        # 高层播放状态枚举
├── pipeline/                # 数据流水线（input → demux → decode → queue）
│   ├── input/               # InputSource 工厂：FileInput / NetworkInput / CameraInput
│   ├── demux/               # Demuxer（avformat_open_input / ReadPacket / Seek）
│   ├── video/               # VideoDecoder
│   ├── audio/               # AudioDecoder / AudioResampler / PCMQueue / SpeedController / AudioSpeedController / VolumeController
│   └── queue/               # PacketQueue / FrameQueue
├── Sync/                    # MasterClock / VideoClock / AudioClock / LiveClock / SyncController / FrameScheduler / DropController
├── output/                  # 输出侧
│   ├── video/               # Renderer / VideoPresenter / RenderContext.h / ControlBarState.h
│   ├── audio/               # AudioDevice（SDL 音频回调 + 音量）
│   └── osd/                 # OSDManager / FontManager / StatsSnapshot.h
├── streaming/               # NetworkBuffer / BufferController / NetworkStatistics / StreamMonitor
├── recording/               # VideoEncoder / AudioEncoder / Muxer / FLVMuxer / HLSMuxer / RTMPPublisher / OutputPipeline
├── features/                # 可选功能（经接口 / 上下文与 core 交互，不反向依赖 Player）
│   ├── seek/                # SeekController
│   ├── playlist/            # PlaylistManager
│   ├── subtitle/            # SubtitleManager（.srt / .ass）
│   ├── screenshot/          # ScreenshotManager（PNG / JPG）
│   └── statistics/          # PlayerStatistics（缓冲统计）
├── Hardware/                # CUDAContext / HardwareDecoder（NVDEC / D3D11VA / DXVA2 + 软解回退）
├── Config/                  # ConfigManager + PlayerConfig.h / StreamConfig.h（player.json / stream.json）
├── infra/                   # Logger / ErrorHandler / FFmpegPtr / DecodeResult
├── legacy/                  # ⚠️ 隔离区（不在 vcxproj，不参与编译）：Decoder / Input / RTSPClient / Screenshot / Clock / Filter* / AudioMixer
├── docs/architecture/       # 重构设计与阶段报告（target-architecture / dependency / migration-map / phase3~8 report）
├── Font/simhei.ttf          # 中文字体
├── vidio101/                # 测试样片（桌面「视频点播」junction 指向）
├── a4c277.mp4               # 默认播放视频（代码内 kDefaultVideo）
├── player.json / stream.json    # 运行配置
├── rtsp_reconnect_test.ps1  # RTSP 断网重连快速验证（mediamtx + lavfi 推流，GOP=1s）
├── rtsp_24h_test.ps1        # RTSP 24h 断网长测（48 轮断网/恢复，summary + 日志裁剪）
├── FFmpeg_text_claw.sln / .vcxproj
└── README.md
```

> `legacy/` 为**隔离区**：`Decoder.*`、`Input.*`、`Screenshot.*`、`AudioMixer.*`、`Clock.*`、`Filter*`、`RTSPClient.*` 已从工程移除（仅磁盘保留，**不在 vcxproj 中、不参与编译**），登记见 `docs/architecture/legacy-register.md`。

> 重构前旧路径 → 现路径：`Input/` → `pipeline/input/`；`Queue/` → `pipeline/queue/`；`Audio/` → `pipeline/audio/`；`Network/` → `streaming/`（其中 `RTMPPublisher` 归 `recording/`）；`Encoder/` + `Muxer/` → `recording/`；`Playlist/`、`Screenshot/`、`Seek/`、`Statistics/`、`Subtitle/` → `features/*/`；`Utils/` → `infra/`；`Filter/` → `legacy/`；`Clock.*` → `Sync/MasterClock.*`（旧文件留在 `legacy/`）。

---

## 日志系统

轻量流式日志（`infra/Logger.h/.cpp`），与 `std::cout` 同风格：

```cpp
Logger::Info()  << "[Main] Open : " << path << std::endl;
Logger::Warn()  << "[Player] Video only mode" << std::endl;
Logger::Error() << "[Main] Init failed" << std::endl;
```

- 输出格式：`[HH:MM:SS.mmm] [INFO ] 消息`
- 级别：DEBUG / INFO / WARN / ERROR，默认 INFO（`-v` 开启 DEBUG）
- 线程安全（原子级别 + 互斥锁）；被过滤的高频日志零开销
- `--log-file xxx.log` 同时写文件（追加模式）
