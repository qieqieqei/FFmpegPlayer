#pragma once

// ============================================================
// Player - 播放器总控（第六阶段重构）
//
// 线程架构（6.0）：
//
//   Demux Thread                 Video Thread               Audio Thread
//   av_read_frame()              videoPacketQueue.Pop()     audioPacketQueue.Pop()
//        |                            |                         |
//        +--视频包--> videoPacketQueue +--> VideoDecoder         +--> AudioDecoder
//        |                            |                         |
//        +--音频包--> audioPacketQueue +--> FrameQueue           +--> Resampler
//        |                            |                         +--> SpeedController
//        +--Seek: SeekController      |                         +--> PCMQueue
//                                      v                         v
//                                 Render Thread            SDL AudioCallback
//                                 (本类 Run)
//
// 本类职责：
//   - 控制中心：持有全部子系统对象（Demuxer / VideoDecoder /
//     AudioDecoder / 三个队列 / AudioDevice / SyncController /
//     SeekController / SpeedController / SubtitleManager /
//     PlaylistManager / ...）
//   - Run：渲染循环（取帧、音视频同步、渲染、OSD、统计）
//   - 控制：Seek / 暂停 / 帧步进 / 倍速 / 音量 / 截图 / 全屏
//   - SwitchMedia：停线程 -> 释放媒体 -> 重新初始化（播放列表切换）
//
// 音频主时钟（5.1，沿用）：
//   AudioClock = 基准 + 已播时长 * 播放速度
//   视频帧同步：delay = videoPts - audioClock
// ============================================================

#include <SDL.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "PlayerState.h"
#include "Demuxer.h"
#include "VideoDecoder.h"
#include "AudioDecoder.h"
#include "AudioResampler.h"
#include "AudioDevice.h"
#include "Audio/SpeedController.h"
#include "Queue/PacketQueue.h"
#include "Queue/FrameQueue.h"
#include "Network/NetworkBuffer.h"
#include "Sync/SyncController.h"
#include "Seek/SeekController.h"
#include "Subtitle/SubtitleManager.h"
#include "Playlist/PlaylistManager.h"
#include "Screenshot/ScreenshotManager.h"
#include "Statistics/PlayerStatistics.h"
#include "Config/ConfigManager.h"
#include "Network/NetworkStatistics.h"
#include "Network/BufferController.h"
#include "Network/StreamMonitor.h"
#include "Hardware/CUDAContext.h"
#include "Hardware/HardwareDecoder.h"
#include "Encoder/VideoEncoder.h"
#include "Encoder/AudioEncoder.h"
#include "Muxer/FLVMuxer.h"
#include "Muxer/HLSMuxer.h"
#include "Network/RTMPPublisher.h"
#include "FontManager.h"
#include "OSDManager.h"
#include "Utils/FFmpegPtr.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

// 队列容量上限（背压阈值）
static constexpr int MAX_VIDEO_PACKETS = 120;   // 视频包队列上限

static constexpr int MAX_AUDIO_PACKETS = 60;    // 音频包队列上限

static constexpr int MAX_VIDEO_FRAMES  = 12;    // 视频帧队列上限

class Player
{

public:

    Player();

    ~Player();

    // ---------- 生命周期 ----------

    // 初始化（打开文件、SDL、音频、OSD、字幕）
    bool Init(
        const char* filename);

    // 加载并应用配置（player.json / stream.json）
    // 应在 Init 之前调用；不调用则使用默认值
    bool LoadConfig();

    // 配置管理器访问（main 读取 default_url 等）
    ConfigManager* GetConfigManager() const;

    // 渲染主循环（启动三个线程，播放直到退出）
    bool Run();

    // 释放所有资源
    void Close();

    // ---------- 播放列表（6.8） ----------

    // 添加媒体到播放列表（main 对每个命令行参数调用）
    void AddToPlaylist(
        const std::string& path);

    // 切到上一首 / 下一首（立即切换）
    bool PlayPrevious();

    bool PlayNext();

    size_t GetPlaylistIndex() const;

    size_t GetPlaylistCount() const;

    // 当前播放列表路径（Init / SwitchMedia 用）
    const std::string& GetCurrentPath() const;

    // ---------- 字幕（6.9） ----------

    // 开关字幕显示
    void ToggleSubtitle();

    bool IsSubtitleEnabled() const;

    // ---------- 播放控制（5.2 / 5.4 / 5.5） ----------

    // 请求 Seek（由 Demux 线程执行）
    void RequestSeek(
        double seconds);

    void TogglePause();

    void Pause();

    void Resume();

    PlayerState GetState() const;

    const char* StateToString() const;

    // 帧步进（暂停时按 N 逐帧播放）
    void RequestFrameStep();

    bool HasFrameStepRequest() const;

    void ClearFrameStepRequest();

    // 播放速度（0.5 / 1.0 / 1.5 / 2.0）
    void SetPlaybackSpeed(
        double speed);

    double GetPlaybackSpeed() const;

    // 音量 0~100
    void SetVolume(
        int percent);

    int GetVolume() const;

    // 截图（5.6）：format = "png" / "jpg"
    void TakeScreenshot(
        const std::string& format);

    // ---------- 输出：录制 / 推流 / HLS（7.4–7.6 集成） ----------

    // 录制到 FLV 文件（与推流 / HLS 可同时启用，共享编码器）
    bool StartRecording(
        const std::string& path);

    void StopRecording();

    // 无参切换：开始录制到 record_<时间戳>.flv，再按一次停止
    void ToggleRecording();

    // RTMP 推流（url 为空则用 stream.json 的 rtmp_url）
    bool StartPushing(
        const std::string& url);

    void StopPushing();

    // 无参切换：推流 / 停止（用配置的 rtmp_url）
    void TogglePushing();

    // HLS 切片输出（dir 下生成 index.m3u8 + segment*.ts）
    bool StartHLS(
        const std::string& dir);

    void StopHLS();

    // 无参切换：HLS 输出到 hls_out/，再按一次停止
    void ToggleHLS();

    // 输出状态查询
    bool IsRecording() const;

    bool IsPushing() const;

    bool IsHLSActive() const;

    void ToggleFullScreen();

    bool IsFullScreen() const;

    const char* FullScreenToString() const;

    // ---------- Seek 状态查询 ----------

    bool HasSeekRequest() const;

    double GetSeekPosition() const;

    // Seek 是否已执行完毕（渲染线程据此丢弃旧帧）
    bool IsSeekHandled() const;

    void ClearSeekHandled();

    // ---------- 时间 / 进度 ----------

    double GetCurrentTime() const;

    double GetDuration() const;

    double GetProgress() const;

    std::string GetTimeString() const;

    std::string GetDurationString() const;

    // ---------- 渲染 / OSD 访问 ----------

    SDL_Window* GetWindow() const;

    int GetVideoWidth() const;

    int GetVideoHeight() const;

    // EOF 后自动退出（CLI 输出模式用：--record/--hls/--push）
    void SetAutoQuitOnEof(
        bool enable);

    // 按帧实际格式取（或惰性创建）YUV->RGB 转换器；
    // 帧格式变化（软解 YUV420P / 硬解 NV12）时自动重建
    SwsContext* GetSwsForFrame(
        AVFrame* frame);

    uint8_t* GetRGBData() const;

    int GetRGBLinesize() const;

    SDL_Texture* GetRGBTexture() const;

    FontManager* GetFontManager() const;

    OSDManager* GetOSDManager() const;

    PlayerStatistics* GetStatistics() const;

    // 更新统计信息（渲染循环每帧调用）
    void UpdateStatistics();

    // ---------- 直播/点播双路径包队列（7.3） ----------
    // 点播/本地：PacketQueue（满阻塞背压）；直播：NetworkBuffer（满丢最旧）

    bool PushVideoPacket(
        AVPacket* pkt);

    bool PushAudioPacket(
        AVPacket* pkt);

    AVPacket* PopVideoPacket(
        int timeoutMs);

    AVPacket* PopAudioPacket(
        int timeoutMs);

    bool IsVideoQueueInterrupted() const;

    bool IsAudioQueueInterrupted() const;

    int GetVideoQueueSize() const;

    int GetAudioQueueSize() const;

    int GetVideoQueueCapacity() const;

private:

    // ---------- 线程 ----------

    // 启动 Demux / Video / Audio 三个线程
    bool StartThreads();

    // 停止三个线程并等待结束（退出 / 切换媒体时调用）
    void StopThreads();

    // Demux 线程：读包分派 + Seek 执行
    void DemuxLoop();

    // Video 线程：视频包解码 -> 帧队列
    void VideoDecodeLoop();

    // Audio 线程：音频包解码 -> 重采样 -> 变速 -> PCM 队列
    void AudioDecodeLoop();

    // 切换到新媒体（停线程 -> 释放媒体 -> 重新初始化 -> 重启线程由调用方负责）
    bool SwitchMedia(
        const std::string& path);

    // 打开媒体（首次初始化 / 播放列表切换共用）
    bool OpenMedia(
        const std::string& path);

    // 释放媒体相关资源（保留 SDL 会话与字体/OSD/列表/字幕对象）
    void ReleaseMedia();

    // ---------- 音频（Audio 线程） ----------

    // 处理一个音频帧（解码/重采样/变速/推送）
    void ProcessAudioFrame(
        AVFrame* frame);

    // Seek 完成后的音频清理：清解码器/重采样器/变速器/时钟
    void AudioSeekCleanup(
        double target);

    // ---------- 工具 ----------

    // 取帧时间戳（秒）
    double GetFramePts(
        AVFrame* frame) const;

    // ---------- 硬件解码（7.7） ----------

    // 刷新当前激活的视频解码器（硬解优先）
    void FlushVideoDecoder();

    // 送包给当前激活的视频解码器
    bool SendVideoPacket(
        AVPacket* pkt);

    // 从当前激活的解码器取帧（硬解时已拷回系统内存，可直接用）
    AVFrame* ReceiveVideoFrame();

    // 尝试创建硬件解码器（配置开启 + CUDA 可用 + h264/hevc 时）
    void TryInitHardwareDecoder(
        AVCodecParameters* codecpar);

    // ---------- 输出链（7.4–7.6） ----------

    // 确保视频/音频编码器存在（首路输出时按 stream.json 创建）
    bool EnsureOutEncoders();

    // 视频帧送入输出链（Video 线程调用，锁内）
    void FeedOutputVideo(
        AVFrame* frame);

    // 音频帧送入输出链（Audio 线程调用，锁内）
    void FeedOutputAudio(
        AVFrame* frame);

    // 视频帧转 YUV420P（编码器输入格式，惰性创建 sws）
    AVFrame* ToYuv420p(
        AVFrame* frame);

    // 分发视频编码包到所有活跃输出
    void DispatchVideoPacket(
        AVPacket* pkt);

    // 分发音频编码包到所有活跃输出
    void DispatchAudioPacket(
        AVPacket* pkt);

    // 冲刷编码器尾帧（停止某路输出时，尾帧写给剩余活跃输出）
    void FlushOutEncoders();

    // 停止所有输出（录制 + 推流 + HLS）
    void StopAllOutputs();

    // 释放输出链资源（编码器 / 转换器）
    void ReleaseOutEncoders();

    // 更新当前播放时间 / 进度
    void SetCurrentTime(
        double time);

    // 音频是否可用（有音频流且设备打开成功）
    bool HasAudio() const;

    // ---------- 成员：核心对象（Player 拥有） ----------

    // 解复用器（Demux 线程）
    std::unique_ptr<Demuxer> demuxer;

    // 视频解码器（Video 线程）
    std::unique_ptr<VideoDecoder> videoDecoder;

    // 硬件视频解码器（Video 线程；激活时优先于 videoDecoder）
    std::unique_ptr<HardwareDecoder> hwDecoder;

    // 硬件帧 -> 系统内存的拷贝目标（Video 线程，复用）
    AVFramePtr hwTransferFrame;

    // 音频解码器（Audio 线程）
    std::unique_ptr<AudioDecoder> audioDecoder;

    // 音频重采样器（Audio 线程）
    std::unique_ptr<AudioResampler> audioResampler;

    // 变速不变调（Audio 线程使用，SetSpeed 跨线程）
    std::unique_ptr<SpeedController> speedController;

    // SDL 音频设备（回调线程 + Audio 线程）
    std::unique_ptr<AudioDevice> audioDevice;

    // 音视频同步控制器（渲染线程）
    std::unique_ptr<SyncController> syncController;

    // Seek 控制器（Demux 线程执行 / 各线程检测代数）
    std::unique_ptr<SeekController> seekController;

    // 截图管理器（渲染线程）
    std::unique_ptr<ScreenshotManager> screenshotManager;

    // 播放信息统计
    std::unique_ptr<PlayerStatistics> statistics;

    // 网络流统计（7.2：FPS / 码率 / 丢包 / 延迟）
    std::unique_ptr<NetworkStatistics> networkStatistics;

    // 网络缓冲控制（7.3：缓冲水位）
    std::unique_ptr<BufferController> bufferController;

    // 流媒体监控（7.9：网络流健康巡检）
    std::unique_ptr<StreamMonitor> streamMonitor;

    // 硬件加速上下文（7.7：CUDA/D3D11VA/DXVA2 探测）
    std::unique_ptr<CUDAContext> cudaContext;

    // 硬件加速是否可用（仅探测，解码接入在后续阶段）
    bool hardwareReady = false;

    // 配置管理器（7.11）
    std::unique_ptr<ConfigManager> configManager;

    // ---------- 成员：输出链（7.4–7.6） ----------

    std::mutex outMutex;             // 保护输出链生命周期（主线程 vs 解码线程）

    std::unique_ptr<VideoEncoder> outVideoEncoder;   // 共享视频编码器

    std::unique_ptr<AudioEncoder> outAudioEncoder;   // 共享音频编码器

    std::unique_ptr<FLVMuxer> recordMuxer;           // 录制（.flv 文件）

    std::unique_ptr<RTMPPublisher> rtmpPublisher;    // 推流（rtmp://）

    std::unique_ptr<HLSMuxer> hlsMuxer;              // HLS 切片

    SwsContextPtr outSws;                            // 视频帧 -> YUV420P

    AVFramePtr outYuvFrame;                          // 转换输出帧（内部复用）

    int64_t outVideoPts = 0;                   // 输出视频 pts（自管理）

    int64_t outAudioPts = 0;                   // 输出音频 pts（自管理）

    int64_t outVideoPktIdx = 0;                // 输出视频包序号（重建 pts）

    int64_t outAudioPktIdx = 0;                // 输出音频包序号（重建 pts）

    bool recording = false;                    // 录制中

    bool pushing = false;                      // 推流中

    bool hlsActive = false;                    // HLS 输出中

    // 字幕管理器
    std::unique_ptr<SubtitleManager> subtitleManager;

    // 播放列表管理器
    std::unique_ptr<PlaylistManager> playlistManager;

    // 播放列表切换请求（渲染线程置位，Run 消费）
    bool switchRequested = false;

    std::string switchPath;

    // ---------- 断网重连（8.3） ----------

    std::string currentMediaPath;                    // 当前媒体路径（重连目标）

    std::atomic<bool> reconnectRequested{ false };   // Demux 线程检测到断流后置位

    std::atomic<int> reconnectAttempts{ 0 };         // 重连尝试计数

    // ---------- 成员：队列（Player 直接持有） ----------

    PacketQueue videoPacketQueue;   // 视频包队列（点播：满阻塞背压）

    PacketQueue audioPacketQueue;   // 音频包队列（点播：满阻塞背压）

    NetworkBuffer videoNetBuffer;   // 视频包队列（直播：满丢最旧，低延迟 7.3）

    NetworkBuffer audioNetBuffer;   // 音频包队列（直播：满丢最旧）

    bool useNetBuffer = false;      // 直播流：Demux<->Decode 走 NetworkBuffer

    FrameQueue videoFrameQueue;     // 视频帧队列（Video -> Render）

    // ---------- 成员：线程 ----------

    std::thread demuxThread;        // Demux 线程

    std::thread videoThread;        // Video 线程

    std::thread audioThread;        // Audio 线程

    // ---------- 成员：SDL 资源 ----------

    SDL_Window* window = nullptr;

    SDL_Renderer* renderer = nullptr;

    SDL_Texture* texture = nullptr;        // YUV 视频纹理（预留）

    SDL_Texture* rgbTexture = nullptr;     // RGB24 纹理

    SwsContextPtr swsCtx;                    // YUV -> RGB 转换

    // 转换器当前源格式（格式变化时重建 swsCtx）
    AVPixelFormat swsSrcFmt = AV_PIX_FMT_NONE;

    int swsSrcW = 0;                       // 转换器源宽

    int swsSrcH = 0;                       // 转换器源高

    bool autoQuitOnEof = false;            // EOF 后自动退出（CLI 输出模式）

    int64_t eofWaitStartMs = -1;           // EOF 等待起始（自动退出计时）

    std::unique_ptr<uint8_t[]> rgbData;      // RGB 缓冲

    int rgbLinesize = 0;                   // RGB 每行字节数

    // ---------- 成员：OSD ----------

    std::unique_ptr<FontManager> fontManager;

    std::unique_ptr<OSDManager> osdManager;

    // ---------- 成员：播放状态 ----------

    PlayerState state = PlayerState::Stopped;

    double playbackSpeed = 1.0;

    int volume = 100;

    double currentTime = 0.0;

    double duration = 0.0;

    double progress = 0.0;

    bool fullscreen = false;

    bool frameStepRequest = false;

    // ---------- 成员：线程间标志 ----------

    // 退出标志（StopThreads 置位）
    std::atomic<bool> quit{ false };

    // Demux 是否已读到文件尾
    std::atomic<bool> demuxEof{ false };

    // 视频解码是否全部完成（含解码器冲刷）
    std::atomic<bool> videoEof{ false };

    // 音频解码是否全部完成（含解码器冲刷）
    std::atomic<bool> audioEof{ false };

    // 音频丢弃标志（Seek / 退出期间置位，让 PushPCM 立即返回）
    std::atomic<bool> audioAbort{ false };

    // Seek 状态（渲染线程）
    double seekPosition = 0.0;             // 当前 Seek 目标（丢弃旧帧用）

    bool seekPending = false;              // 是否还有未完成的 Seek

    // 丢弃 Seek 目标之前的音频帧（pts 秒，<0 表示不丢弃；Audio 线程）
    double dropAudioUntil = -1.0;

    // 自动切下一首进行中（防止 EOF 状态反复触发）
    bool autoAdvancing = false;

    // 上一帧副本（供截图 / EOF 显示）
    AVFramePtr lastFrame;

    // 视频帧时长（无音频时按它匀速播放）
    double videoFrameDuration = 1.0 / 25.0;

    bool hasAudioStream = false;

};
