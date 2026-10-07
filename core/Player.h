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

#include "core/PlayerState.h"
#include "core/MediaContext.h"
#include "core/PlaybackSession.h"
#include "pipeline/demux/Demuxer.h"
#include "pipeline/video/VideoDecoder.h"
#include "pipeline/audio/AudioDecoder.h"
#include "pipeline/audio/AudioResampler.h"
#include "output/audio/AudioDevice.h"
#include "pipeline/audio/SpeedController.h"
#include "pipeline/queue/PacketQueue.h"
#include "pipeline/queue/FrameQueue.h"
#include "streaming/NetworkBuffer.h"
#include "sync/SyncController.h"
#include "features/seek/SeekController.h"
#include "features/subtitle/SubtitleManager.h"
#include "features/playlist/PlaylistManager.h"
#include "features/screenshot/ScreenshotManager.h"
#include "features/statistics/PlayerStatistics.h"
#include "config/ConfigManager.h"
#include "streaming/NetworkStatistics.h"
#include "streaming/BufferController.h"
#include "streaming/StreamMonitor.h"
#include "hardware/CUDAContext.h"
#include "hardware/HardwareDecoder.h"
#include "recording/VideoEncoder.h"
#include "recording/AudioEncoder.h"
#include "recording/FLVMuxer.h"
#include "recording/HLSMuxer.h"
#include "recording/RTMPPublisher.h"
#include "output/osd/FontManager.h"
#include "output/osd/OSDManager.h"
#include "infra/DecodeResult.h"

#include "infra/FFmpegPtr.h"

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

    friend class PlaybackSession;

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

    // v2: CLI override --live-buffer <ms> (0 = low_latency, >0 = stable target)
    void SetLiveBufferOverride(int ms);

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

    // 8.14 loop: expand single-file playlist with folder siblings
    void ExpandPlaylistWithSiblings();

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

    // ---------- Control bar UI (8.14) ----------
    struct ControlBarState
    {
        bool visible = true;          // always shown

        bool seekDragging = false;    // progress bar dragging

        double seekPreview = -1.0;    // drag preview time (s)

        int hoverButton = 0;          // 0=none 1=prev 2=play/pause 3=next

        SDL_Rect prevBtn{};           // prev button hit rect

        SDL_Rect playBtn{};           // play/pause button hit rect

        SDL_Rect nextBtn{};           // next button hit rect

        SDL_Rect track{};             // progress track hit rect
    };

    ControlBarState& GetControlBar()
    {
        return uiBar;
    }

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

    // 8.4：是否硬件解码（评审六：解码路径可观测）
    bool IsHardwareDecode() const;

    // 8.4：网络流统计（直播：FPS / 码率 / 丢包率 / 缓冲水位）
    NetworkStatistics* GetNetworkStatistics() const;


private:

    // ---------- 工具 ----------

    // 取帧时间戳（秒）
    double GetFramePts(
        AVFrame* frame) const;

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

    // 媒体会话管线对象集合（随 OpenMedia/ReleaseMedia 重建）
    std::unique_ptr<MediaContext> media;

    // Playback session (phase 4.4: threads / lifecycle / decode loops)
    std::unique_ptr<PlaybackSession> session;

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

    // v2: --live-buffer <ms> CLI override (-1 = not set)
    int liveBufferOverrideMs = -1;

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

    bool autoQuitOnEof = false;            // EOF 后自动退出（CLI 输出模式）

    int64_t eofWaitStartMs = -1;           // EOF 等待起始（自动退出计时）

    // ---------- 成员：OSD ----------

    std::unique_ptr<FontManager> fontManager;

    std::unique_ptr<OSDManager> osdManager;

    // control bar UI state (8.14)
    ControlBarState uiBar;

    // ---------- 成员：播放状态 ----------

    PlayerState state = PlayerState::Stopped;

    double playbackSpeed = 1.0;

    int volume = 100;

    double currentTime = 0.0;

    double progress = 0.0;

    bool fullscreen = false;

    bool frameStepRequest = false;

};
