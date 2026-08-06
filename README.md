# FFmpeg_text_claw

基于 **C++17 + FFmpeg 8.x + SDL2** 开发的 Windows 多线程音视频播放器（MSVC / Visual Studio 工程）。

项目目标是深入学习音视频播放器底层架构，实现从 **媒体读取、解封装、音视频解码、同步控制到音视频输出** 的完整播放流程。模块化设计：播放器拆分为多个独立模块，通过线程安全队列连接，实现解码、渲染和控制逻辑解耦。

---

## 功能清单

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

- 增加 RTSP / RTMP 网络流播放
- 增加硬件解码（NVDEC / DXVA2）
- 播放器 UI 完善
- 真实字幕文件端到端验证（.srt 渲染已实现，尚未用真实文件回归）
