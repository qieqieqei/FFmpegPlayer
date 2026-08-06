
# FFmpeg_Test

基于 **C++17 + FFmpeg + SDL2** 开发的跨平台多线程音视频播放器。

项目目标是深入学习音视频播放器底层架构，实现从 **媒体读取、解封装、音视频解码、同步控制到音视频输出** 的完整播放流程。

项目采用模块化设计，将播放器拆分为多个独立模块，通过线程安全队列连接，实现解码、渲染和控制逻辑解耦。

---

# 项目特点

- 基于 FFmpeg 实现完整媒体处理流程
- C++17 模块化播放器架构设计
- 多线程音视频解码
- SDL2 视频渲染与音频播放
- PacketQueue / FrameQueue 数据缓冲
- Audio Clock 音视频同步
- 支持 Seek 跳转
- 支持暂停、恢复、停止等播放控制
- CMake 工程管理
- Windows/Linux 跨平台设计


---

# 技术栈

| 技术 | 用途 |
| ---- | ---- |
| C++17 | 核心开发语言 |
| FFmpeg | 媒体解析、解码、音频处理 |
| SDL2 | 视频渲染、音频输出、事件处理 |
| CMake | 工程构建 |
| OpenGL | SDL2 底层纹理渲染基础 |
| Git | 版本管理 |


---

# 播放器架构

整体采用分层模块化设计：

             +----------------+
             |   Player       |
             |  Controller    |
             +-------+--------+
                     |
                     |
             +-------v--------+
             |    Demux       |
             |  (FFmpeg)      |
             +-------+--------+
                     |
          +----------+----------+
          |                     |
          v                     v

   +-------------+       +-------------+
   | PacketQueue |       | PacketQueue |
   |   Video     |       |   Audio     |
   +------+------+       +------+------+
          |                     |
          v                     v

   +-------------+       +-------------+
   | Video       |       | Audio       |
   | Decoder     |       | Decoder     |
   +------+------+       +------+------+

          |                     |
          v                     v

   +-------------+       +-------------+
   | FrameQueue  |       | SwrContext  |
   | Video Frame |       | Resample    |
   +------+------+       +------+------+

          |
          v

   +-------------+
   | SDL Renderer|
   | YUV Output  |
   +-------------+

                          |
                          v

                   +-------------+
                   | SDL Audio   |
                   | Callback    |
                   +-------------+

---

# 核心模块

## 1. Demux 模块

负责媒体文件解析和数据读取。

主要流程：


Input File

|
v

avformat_open_input()

|
v

avformat_find_stream_info()

|
v

av_read_frame()

|
v

AVPacket


负责：

- 打开媒体文件
- 查找音视频流
- 读取 AVPacket
- 分发音视频数据


---

# 2. Decoder 模块

基于 FFmpeg libavcodec 实现音视频解码。

数据流程：


AVPacket

|

avcodec_send_packet()

|

avcodec_receive_frame()

|

AVFrame


处理：

- H.264/H.265 视频解码
- 音频解码
- EAGAIN 状态
- EOF 状态
- Decoder Flush


核心对象：

- AVCodecContext
- AVPacket
- AVFrame


---

# 3. PacketQueue

用于缓存 Demux 线程读取的数据。

作用：

- 解耦读取速度和解码速度
- 防止线程阻塞
- 实现生产者消费者模型


结构：


Demux Thread

  |
  v

PacketQueue

  |
  v

Decoder Thread



线程同步：

- mutex
- condition_variable


当：

### 队列为空

消费者等待：


condition_variable.wait()



### 队列有数据

消费者继续读取。


---

# 4. FrameQueue

用于缓存解码后的 Frame。

作用：

- 解耦 Decoder 和 Renderer
- 防止渲染速度影响解码


流程：


Decoder

|

AVFrame

|

FrameQueue

|

Renderer



---

# 5. 视频渲染模块

基于 SDL2 实现。


流程：


AVFrame(YUV420P)

    |

SDL_UpdateYUVTexture()

    |

SDL_RenderCopy()

    |

Display



支持：

- YUV420P 渲染
- 窗口缩放
- 全屏切换
- 视频刷新控制


---

# 6. 音频播放模块


音频处理流程：


AVFrame

|

SwrContext

|

PCM

|

SDL Audio Callback

|

Speaker



SwrContext负责：

- 采样率转换
- 声道布局转换
- Sample Format转换


例如：


AAC

↓

FLTP

↓

S16 PCM

↓

SDL Output



---

# 多线程模型


播放器采用四线程结构：


## Demux Thread

负责：

- 文件读取
- Packet生成
- 分发音视频数据


## Video Decode Thread

负责：

- 视频Packet读取
- FFmpeg解码
- Frame生成


## Audio Decode Thread

负责：

- 音频Packet读取
- 音频解码
- PCM准备


## Render Thread

负责：

- 视频刷新
- SDL渲染


整体模型：

             Demux Thread

                   |
                   |

          +--------+--------+

          |                 |

          v                 v

   Video Packet       Audio Packet

          |                 |

          v                 v

   Video Decoder      Audio Decoder

          |                 |

          v                 |

    FrameQueue          SDL Audio

          |

          v

    SDL Renderer


---

# 音视频同步


播放器采用：

## Audio Clock 主时钟模型


原因：

音频播放通常更加稳定，因此以音频时间作为基准。


同步流程：


Audio Clock

  |

  v

video_pts - audio_clock

  |

  v

计算偏差

  |

  v

调整视频显示时间



策略：

### 视频领先

等待显示：


delay += diff



### 视频落后

减少等待或丢帧：


drop frame



---

# Seek 实现


Seek流程：


User Seek

|

av_seek_frame()

|

Clear PacketQueue

|

Clear FrameQueue

|

avcodec_flush_buffers()

|

Reset Clock

|

Resume Decode



解决：

- 旧Frame残留
- 时间戳错误
- Decoder缓存问题


---

# 播放状态管理


播放器内部维护状态：

```cpp
enum PlayerState
{
    PLAYING,
    PAUSED,
    STOPPED,
    SEEKING
};

状态控制：

PLAYING

    |
    v

PAUSED

    |
    v

PLAYING

暂停时：

Decoder线程停止工作
condition_variable等待
避免CPU空转
已实现功能
功能	状态
MP4播放	✅
FFmpeg解封装	✅
H264/H265解码	✅
SDL2视频渲染	✅
SDL音频播放	✅
音频重采样	✅
多线程解码	✅
PacketQueue	✅
FrameQueue	✅
Pause/Resume	✅
Seek跳转	✅
音视频同步	✅
CMake构建	✅
项目目录结构
FFmpeg_Test

├── Decoder
│   ├── VideoDecoder
│   └── AudioDecoder
│
├── Demux
│
├── Queue
│   ├── PacketQueue
│   └── FrameQueue
│
├── Renderer
│   └── VideoRenderer
│
├── Audio
│   └── AudioOutput
│
├── Player
│   └── PlayerController
│
├── CMakeLists.txt
│
└── README.md
编译环境
Windows

环境：

Windows 10/11
Visual Studio 2022
FFmpeg
SDL2
CMake

编译：

mkdir build

cd build

cmake ..

cmake --build .
Linux

支持：

GCC
CMake
FFmpeg
SDL2

编译方式：

mkdir build

cd build

cmake ..

make
后续计划
 完善播放器UI
 增加字幕渲染
 增加倍速播放
 增加RTSP/RTMP网络流播放
 增加硬件解码支持(NVDEC)
 增加OpenGL渲染优化
 增加Linux完整测试
Learning Notes

开发过程中主要学习：

FFmpeg API调用流程
AVPacket / AVFrame生命周期管理
多线程播放器架构
音视频同步算法
SDL2音视频输出机制
C++模块化工程设计
