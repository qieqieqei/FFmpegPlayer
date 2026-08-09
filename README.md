# FFmpegPlayer

基于 **C++17 + FFmpeg 8.x + SDL2** 开发的 Windows 多线程音视频播放器（MSVC / Visual Studio 工程）。

> GitHub：https://github.com/qieqieqei/FFmpegPlayer

项目目标是深入学习音视频播放器底层架构，实现从 **媒体读取、解封装、音视频解码、同步控制到音视频输出** 的完整播放流程。模块化设计：播放器拆分为多个独立模块，通过线程安全队列连接，实现解码、渲染和控制逻辑解耦。

---

## 功能清单

### 模块状态总表

| 模块 | 状态 | 说明 |
|---|---|---|
| `Input/`（File/Network/RTSPClient） | ✅ 完成 | 协议工厂 + 超时注入 + 中断回调 |
| `Sync/`（Audio/Video/MasterClock + Scheduler + Drop） | ✅ 完成 | 音频主时钟 + ffplay 级目标延迟调整 + 丢帧追赶 + 墙钟漂移校正 |
| `Hardware/`（CUDAContext/HardwareDecoder） | ✅ 解码 / 🧪 渲染未做 | NVDEC/D3D11VA/DXVA2 解码 + 软解回退；**GPU 零拷贝渲染未实现**（回读 CPU） |
| `Queue/` + `Network/NetworkBuffer` | ✅ 完成 | RAII 所有权 + 谓词等待 + GOP 感知丢包 |
| `Decoder`（Video/Audio/Hardware） | ✅ 完成 | DecodeResult 三态（Success/NeedMore/End/Error） |
| `Encoder/`（Video/Audio） | ✅ 完成 | libx264/libx265/nvenc；AAC/Opus |
| `Muxer/`（FLV/HLS） | ✅ 完成 | FLV 文件/RTMP 推流、HLS 自动切片清理 |
| `Filter/`（FilterGraph/Video/Audio） | ✅ 完成 | avfilter 通用封装，滤镜链配置（`video_filter`/`audio_filter`） |
| `Subtitle/` | 🧪 实验性 | .srt 解析 + OSD 渲染已实现，**尚未用真实字幕文件端到端回归** |
| `Screenshot/` | ✅ 完成 | PNG/JPG |
| `Playlist/` | ✅ 完成 | 多文件连播（上一首/下一首/自动连播） |
| `Seek/` | ✅ 完成 | 关键帧定位 + 解码器 flush + 代数防竞态 |
| `Network/`（统计/缓冲/重连/推流/监控） | ✅ 完成 | 丢包率、缓冲水位、自动重连、RTMP 推流、健康巡检 |
| `Config/` | ✅ 完成 | 自研轻量 JSON 解析，player.json + stream.json |

### 网络与配置（7.x）
- ✅ 统一输入层 `Input/`：`InputSource` 抽象基类 + 工厂（按 URL 协议自动创建），`FileInput`（本地文件，支持 `file://`）、`NetworkInput`（rtsp/rtmp/http/https/HLS，按协议注入 `rtsp_transport=tcp`、`stimeout`/`rw_timeout` 超时、`fflags=nobuffer` 低延迟选项）、`RTSPClient`（连接生命周期管理：连接 / 断开 / 自动重连，次数上限 + 间隔）
- ✅ 中断回调：`SetAbort()` 可打断阻塞中的网络读取（`av_read_frame` 立即返回），退出 / 切换媒体不再卡死
- ✅ 直播 / 点播自动识别：RTSP/RTMP/时长未知流按直播处理（禁止 Seek、低延迟缓冲）；点播流可 Seek
- ✅ 网络缓冲 `Network/NetworkBuffer`：满时丢最旧包（直播低延迟策略，区别于本地背压队列），带丢弃计数
- ✅ **直播路径已接入 Player**：`useNetBuffer = demuxer->IsLive()`——直播流（RTSP/RTMP/推流中 HLS）自动走 NetworkBuffer（满丢最旧、低延迟），点播流走原 PacketQueue（满阻塞背压）；音频 NetBuffer cap = max(30, cap/2)、视频 cap = 600，缓冲目标 `bufferTargetMs = 300`（stream.json 可配）
- ✅ HTTP HLS 崩溃修复：`OpenWithOptions` 改收 `AVDictionary**`（原按值传参，`avformat_open_input` 消费选项后 `av_dict_free(&opts)` 双重释放崩溃 0xC0000005）；`analyzeduration=1500000`、`probesize=300000` 修复 HLS/TS AAC 采样率未解析问题
- ✅ 网络统计 `Network/NetworkStatistics`：1s 滑动窗口统计输入/输出 FPS、码率、丢包率、缓冲水位、延迟估算
- ✅ 缓冲控制 `Network/BufferController`：低/高水位模型（NeedBuffer / IsEnough），直播目标 300ms、点播 2000ms
- ✅ 配置系统 `Config/`：自研轻量 JSON 解析器，`player.json`（窗口/音量/速度/默认 URL/日志）+ `stream.json`（RTSP/RTMP/编码/缓冲/HLS/滤镜参数，为后续阶段预留），缺失时用默认值不报错

### 断网自动恢复（8.3）
- ✅ **断网自动重连**：直播流（RTSP/RTMP/HLS）断流时，Demux 线程检测到 `av_read_frame` 错误/EOF → 请求重连 → 主循环 `SwitchMedia` 循环重试（`reconnect_max_attempts` / `reconnect_delay_ms` 可配，≤0 表示无限重试，适合 24h 无人值守）→ 重连成功后自动恢复渲染
- ✅ 重连目标 = `OpenMedia` 记录的 `currentMediaPath`；0x0 分辨率（SPS 未解析）提前失败走重试，等下一个关键帧；`max_analyze_duration` 5s→12s 覆盖 8s GOP
- ✅ 实测：mediamtx + ffmpeg 合成推流（GOP=1s 每秒关键帧）模拟断网/恢复——断流检测 → **1.6s 重连成功 → 渲染恢复**；24h 长测 48 轮断网/恢复，截至 2026-08-09 16/48 轮全 PASS（脚本 `rtsp_reconnect_test.ps1` 快速验证 + `rtsp_24h_test.ps1` 长测，均已入仓）

### 队列所有权与等待优化（8.4）
- ✅ **队列 RAII 所有权改造**：`PacketQueue` / `FrameQueue` / `NetworkBuffer` 元素从裸指针改为 `PacketPtr` / `FramePtr`（`Utils/FFmpegPtr.h` 别名），Push 移动语义交接所有权、Pop 返回所有权，Demux → 队列 → 解码器全链路无共享裸指针，杜绝 double-free / use-after-free
- ✅ **谓词等待替代轮询**：`cv.wait_for(10ms)` 轮询改为谓词等待（`cv.wait(lock, pred)` / `wait_for(lock, timeout, pred)`），条件满足（有数据 / 不满 / 打断）立即唤醒，不再空转
- ✅ **GOP 感知丢包**（直播 NetworkBuffer 满）：不再盲目丢最旧包（会撕裂 GOP 导致花屏），改为丢到关键帧边界——队头非关键帧时丢到第一个关键帧之前（保留完整 GOP 起点）；队头即关键帧时整段丢弃等下一个关键帧重建
- ✅ **丢包统计打通**：NetworkBuffer 丢弃计数增量同步 `NetworkStatistics`（OSD 新增 Net 行显示 `Loss %`），丢包可观测
- ✅ **可配置指数退避**：`reconnect_backoff_factor`（stream.json，默认 1.0 = 固定间隔，行为不变）；>1.0 开启 `delay * factor^(n-1)` 封顶 30s，长时间断网避免高频重试打服务器
- ✅ **解码结果三态可区分**（评审七）：`ReceiveFrame()` 从裸指针升级为 `DecodeResult` 枚举（`Success / NeedMorePacket / End / Error`）——`avcodec_receive_frame` 的 EAGAIN（继续送包）、EOF（解码结束）、错误不再被吞成同一个 nullptr；VideoDecoder / AudioDecoder / HardwareDecoder 统一，调用方明确处理每种结果
- ✅ **ffplay 级目标延迟调整**（评审五）：同步不再只是 `videoPts - masterTime` 误差计算——按视频时钟与主时钟偏差微调：视频领先时 `delay` 加长（轻微领先翻倍、大领先直接加偏差，等音频）；视频落后时 `delay` 缩短（最快立即显示），落后超过阈值仍由 DropController 丢帧追赶（连续判定 + 冷却防抖）
- ✅ **音频墙钟漂移校正**（评审五）：AudioClock 跟踪媒体时间与真实时间累积偏差，渲染循环每秒渐进拉回（单次 ≤5ms 无感知，偏差 <10ms 不动），长期播放进度不再漂移
- ✅ **解码路径诚实标注**（评审六）：OSD 显示 `(HW decode)` / `(SW decode)`；硬件状态如实声明——NVDEC/D3D11VA/DXVA2 解码已完成，渲染为 `av_hwframe_transfer_data` 回读 CPU → SDL 纹理，**GPU 零拷贝渲染未实现**（SDL2 无 CUDA/D3D11 互操作 API；OpenGL interop 在 Windows 不稳定），不宣称"GPU 渲染"

### 编码 / 封装 / 推流 / 滤镜（7.x 第二阶段）
- ✅ 视频编码 `Encoder/VideoEncoder`：libx264 / libx265 / h264_nvenc；直播低延迟（libx264 `tune=zerolatency`，nvenc `preset=ll` + `bf=0`），GOP=2s，输入 YUV420P
- ✅ 音频编码 `Encoder/AudioEncoder`：AAC / Opus；内部 swr 自动重采样为编码器所需格式（AAC→FLTP）
- ✅ 封装 `Muxer/`：`Muxer` 薄基类（AddStream / WritePacket 自动时间基转换 / 补写尾），`FLVMuxer`（.flv 文件或 rtmp://，支持关键帧起播），`HLSMuxer`（原生 hls muxer：hls_time / hls_list_size / delete_segments 自动切片与清理）
- ✅ RTMP 推流 `Network/RTMPPublisher`：组合 FLVMuxer（RTMP 推送 = FLV 封装 + rtmp 协议），连接 / 断开 / 失败计数 / 断线重连（次数上限 + 间隔），从关键帧开始推（接收端立即起播）
- ✅ 滤镜 `Filter/`：`FilterGraph`（avfilter 图封装：buffer/abuffer → 滤镜链 → buffersink），`VideoFilter`（scale / hflip / drawtext 等）、`AudioFilter`（volume / highpass / atempo 等）
- ✅ 硬件解码 `Hardware/`：`CUDAContext`（CUDA → D3D11VA → DXVA2 自动探测降级，已在本机验证 cuda 可用）、`HardwareDecoder`（硬解 + 软解自动回退 + GPU 帧零拷贝，可共享 hw_frames_ctx 给 h264_nvenc）
- ✅ 流监控 `Network/StreamMonitor`：每秒巡检网络流（延迟 / 丢包率 / 码率 / 缓冲 / FPS），阈值告警（延迟 ≥500ms、丢包 ≥1%、无数据 5s 判定断流），已接入 Player 渲染循环（仅网络流触发）
- ✅ Player 集成：录制 / 推流 / HLS 开关（编码链接入播放主流程），CLI `--record/--push/--hls` 启动即输出，EOF 后 3 秒自动退出（批处理友好）
- ✅ 直播低延迟实测：UDP mpegts（`udp://127.0.0.1:12345`）与 HTTP HLS 均验证通过——live 自动识别（`Network stream : http (live)`）、NetworkBuffer cap=600 target=300ms、源停后干净 EOF → 3s 自动退出、FLV 完整 20.000s；仅推流中的 m3u8 走 live 路径（静态 m3u8 按点播处理）
- ✅ 硬解接入解码主链路：`stream.json` 的 `"hardware_decode": true` 开启（默认开）；Player 优先走 `HardwareDecoder`（NVDEC/D3D11VA/DXVA2），GPU 帧 transfer 回系统内存（NV12）后走原有渲染/输出链路（渲染转换器按实际帧格式惰性创建，自动适配 NV12/YUV420P）；失败自动回退软解。已验证：RTX4060 上 `h264 : cuda 640x360` 激活，录制 20s FLV 正常
- ⚠ FLV/RTMP 格式限制：仅支持 H.264 + AAC（Opus 不能走 FLV/RTMP 路径）

### 播放核心
- ✅ MP4 / 常见封装格式播放（FFmpeg 解封装）
- ✅ H.264 / H.265 视频解码（libavcodec）
- ✅ AAC 等音频解码 + 重采样（SwrContext，统一为 S16 / 48000Hz / 立体声）
- ✅ 三线程架构：Demux 线程 / Video Decode 线程 / Audio Decode 线程（主线程负责渲染）
- ✅ PacketQueue / FrameQueue 线程安全缓冲（带 max-size 背压）
- ✅ 音频主时钟音视频同步（视频提前 ≤100ms 分片等待，落后 >50ms 丢帧；无音频时按帧率播放）
- ✅ Seek 跳转（`av_seek_frame` + flush + 清队列 + 时钟重置，带中断保护防死锁）
- ✅ 暂停 / 恢复（背压自停，不空转）
- ✅ 变速播放（0.5x / 1x / 1.5x / 2x，音频重采样变速）
- ✅ 音量控制（S16 采样缩放 + clamp）

### 进阶功能
- ✅ 播放列表：多文件播放、上一首 / 下一首、**EOF 自动播下一首**
- ✅ 字幕：自动加载同名 .srt / .ass，OSD 底部渲染，可开关
- ✅ OSD 屏幕显示：进度 / 时间 / 状态 / 提示，中文字体（Font/simhei.ttf）
- ✅ 截图：PNG / JPG 一键保存（S / J 键）
- ✅ 帧步进（暂停时逐帧查看，N 键）
- ✅ 全屏切换（F 键）
- ✅ 轻量日志系统：级别过滤（DEBUG/INFO/WARN/ERROR）、时间戳、`-v` 开 DEBUG、`--log-file` 写文件

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

---

## 命令行用法

```bat
FFmpeg_text_claw.exe [文件1] [文件2] ...        # 多文件加入播放列表
FFmpeg_text_claw.exe -v file.mp4                # DEBUG 级别日志
FFmpeg_text_claw.exe --log-file player.log file.mp4   # 同时写日志文件
FFmpeg_text_claw.exe --record file.mp4          # 播放同时录制 FLV（record_*.flv）
FFmpeg_text_claw.exe --hls file.mp4             # 播放同时输出 HLS（hls_out/）
FFmpeg_text_claw.exe --push rtmp://host/live/stream file.mp4   # 播放同时 RTMP 推流
```

不带参数时播放默认测试视频 `D:\FFmpeg\ffmpeg\test_audio.mp4`（代码内 kDefaultVideo 常量，可自行修改）。

---

## 播放器架构

Player 作为控制中心，持有全部模块，三线程 + 主渲染循环：

```
                +---------------------------+
                |         Player            |  控制中心（状态机 + 线程管理）
                +-------------+-------------+
                              |
        +---------------------+---------------------+
        |                     |                     |
        v                     v                     v
+---------------+     +---------------+     +---------------+
|  Demux Thread |     | Video Decode  |     | Audio Decode  |
|  (Demuxer)    |     | Thread        |     | Thread        |
|  av_read_frame|     | (VideoDecoder)|     | (AudioDecoder)|
+-------+-------+     +-------+-------+     +-------+-------+
        |                     |                     |
        v                     v                     v
+---------------+     +---------------+     +---------------+
| PacketQueue   |     | PacketQueue   |     | AudioResampler|
|  Video(120)   |     |  Audio(60)    |     |  → SwrContext |
+---------------+     +---------------+     +-------+-------+
                                              | PCMQueue
                                              v
                                     +---------------+
                                     |  AudioDevice  |  SDL Audio 回调
                                     | AudioClock    |  （音频主时钟）
                                     | VolumeControl |
                                     +---------------+

+---------------+     +---------------+
| FrameQueue    |     | SyncController|  音视频同步
|  Video(12)    |     +---------------+
+-------+-------+     +---------------+
        |             | SeekController|  打断三队列→Seek→清队列→重置时钟
        |             +---------------+
        v             +---------------+
+---------------+     | SpeedController| 变速
| Renderer(SDL) |     +---------------+
|  + OSDManager |     +---------------+
|  + Subtitle   |     | SubtitleManager| .srt/.ass 解析
+---------------+     +---------------+
                      +---------------+
                      | PlaylistManager| 列表 + 自动连播
                      +---------------+
```

### 线程模型

| 线程 | 职责 |
| ---- | ---- |
| Demux 线程 | `av_read_frame` → 按流类型分发到 Video/Audio PacketQueue |
| Video Decode 线程 | 取视频包 → 解码 → clone 入 FrameQueue（最多 12 帧） |
| Audio Decode 线程 | 取音频包 → 解码 → 重采样 → 变速 → Push 到 PCMQueue |
| 主线程（Render） | 取视频帧 → 同步等待/丢帧 → SDL 渲染 + OSD + 事件处理 |

### 同步策略（音频主时钟）

- 音频播放更稳定，作为时间基准（AudioClock = base + played × speedFactor）
- 视频提前：按剩余时间分片等待（≤100ms），避免阻塞过久
- 视频落后 >50ms：直接丢帧追赶
- 无音频流：按 `frameDuration / speed` 均匀播放（降级为 Video only mode）

### Seek 流程

```
用户按 ←/→ → SeekController::Request → Run 循环消费
  → 打断三队列（Interrupt）→ av_seek_frame → 清空队列
  → ResetInterrupt → 解码器 flush → 时钟重置 → seekGeneration++
```

### 队列背压

- `MAX_VIDEO_PACKETS = 120`、`MAX_AUDIO_PACKETS = 60`、`MAX_VIDEO_FRAMES = 12`（Player.h 常量）
- 队列满时生产者等待，暂停即背压自停（不空转 CPU）

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
2. **子目录 include**：`#include "Audio/PCMQueue.h"` 这类子目录引用，依赖 `$(ProjectDir);` 已加入 AdditionalIncludeDirectories。
3. **新增 .cpp 文件**：必须手动注册进 `.vcxproj` 和 `.vcxproj.filters`，否则不会被编译。
4. **FFmpeg 8.x**：`qscale` 字段已移除，mjpeg 编码质量用 `av_opt_set_int(ctx, "q", 8, 0)`。
5. **SDL_MAIN_HANDLED**：main.cpp 顶部已定义，避免 SDL 改写 Win32 入口。

---

## 项目目录结构

```
FFmpeg_text_claw
├── main.cpp                 # 入口：命令行解析（-v / --log-file）+ 播放列表
├── Player.h / Player.cpp    # 控制中心：三线程 + 状态机 + 媒体切换
├── Demuxer.h / .cpp         # 解封装：avformat_open_input / ReadPacket / Seek
├── VideoDecoder.h / .cpp    # 视频解码（SendPacket / ReceiveFrame / Flush）
├── AudioDecoder.h / .cpp    # 音频解码
├── AudioDevice.h / .cpp     # SDL 音频输出（PCMQueue + AudioClock + 音量）
├── AudioResampler.h / .cpp  # 重采样（SwrContext）
├── Renderer.h / .cpp        # SDL 渲染（YUV 纹理）
├── OSDManager.h / .cpp      # OSD：进度 / 状态 / 字幕 / 提示
├── Event.h / .cpp           # 键盘事件 → 播放器控制
├── FontManager.h / .cpp     # SDL_ttf 中文字体
├── Screenshot.h / .cpp      # 截图（废弃，由 ScreenshotManager 取代）
├── PlayerState.h            # 播放状态枚举
├── Audio\                  # PCMQueue / SpeedController / VolumeController / AudioSpeedController
├── Playlist\               # PlaylistManager（多文件 + 自动连播）
├── Queue\                  # PacketQueue / FrameQueue
├── Screenshot\             # ScreenshotManager（PNG/JPG）
├── Seek\                   # SeekController
├── Statistics\             # PlayerStatistics（缓冲统计）
├── Subtitle\               # SubtitleManager（.srt / .ass 解析）
├── Sync\                   # AudioClock / Clock / SyncController
├── Utils\                  # Logger（日志系统）/ ErrorHandler / FFmpegPtr
├── Font\simhei.ttf         # 中文字体
├── rtsp_reconnect_test.ps1 # RTSP 断网重连快速验证（mediamtx + lavfi 推流，GOP=1s）
├── rtsp_24h_test.ps1       # RTSP 24h 断网长测（48 轮断网/恢复，summary + 日志裁剪）
├── FFmpeg_text_claw.sln / .vcxproj
└── README.md
```

> `Decoder.*`、`Input.*`、`Audio\AudioMixer.*` 已从工程移除（磁盘保留，不参与编译）。

---

## 日志系统

轻量流式日志（`Utils\Logger.h/.cpp`），与 `std::cout` 同风格：

```cpp
Logger::Info()  << "[Main] Open : " << path << std::endl;
Logger::Warn()  << "[Player] Video only mode" << std::endl;
Logger::Error() << "[Main] Init failed" << std::endl;
```

- 输出格式：`[HH:MM:SS.mmm] [INFO ] 消息`
- 级别：DEBUG / INFO / WARN / ERROR，默认 INFO（`-v` 开启 DEBUG）
- 线程安全（原子级别 + 互斥锁）；被过滤的高频日志零开销
- `--log-file xxx.log` 同时写文件（追加模式）

---

## 后续计划

- ✅（2026-08-08）编码 / 封装 / 推流 / 滤镜 / 硬件解码 / 流监控模块完成（7.4–7.9）
- ✅（2026-08-08）编码链接入 Player（录制 / 推流 / HLS 开关）+ 硬解接入解码主链路（hardware_decode 配置，NVDEC 验证通过）
- ✅（2026-08-08）直播播放路径切 NetworkBuffer（丢最旧，真低延迟）——已完成，UDP/HTTP HLS 实测通过（见功能清单）
- ✅（2026-08-09）RTSP 断网自动重连（8.3）——断流检测 + 自动重连 + 渲染恢复；24h 断网长测进行中（16/48 轮全 PASS），模拟验证全覆盖
- ✅（2026-08-09）队列 RAII 所有权 + 谓词等待 + GOP 感知丢包（8.4）——评审意见（裸指针隐患 / 10ms 轮询 / 丢包策略 / 丢包可观测）落实；24h 长测结束后部署正式编译 + 回归
- ✅（2026-08-09）评审五/六/七落实（8.4）：解码三态 DecodeResult、ffplay 级目标延迟调整、音频墙钟漂移校正、解码路径诚实标注 + 模块状态总表
- 播放器 UI 完善
- 真实字幕文件端到端验证（.srt 渲染已实现，尚未用真实文件回归）
- RTSP 真机验证（模拟流已全覆盖；摄像头地址待提供）
