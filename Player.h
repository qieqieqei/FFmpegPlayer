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
#include "Sync/SyncController.h"
#include "Seek/SeekController.h"
#include "Subtitle/SubtitleManager.h"
#include "Playlist/PlaylistManager.h"
#include "Screenshot/ScreenshotManager.h"
#include "Statistics/PlayerStatistics.h"
#include "FontManager.h"
#include "OSDManager.h"

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

    SwsContext* GetSwsContext() const;

    uint8_t* GetRGBData() const;

    int GetRGBLinesize() const;

    SDL_Texture* GetRGBTexture() const;

    FontManager* GetFontManager() const;

    OSDManager* GetOSDManager() const;

    PlayerStatistics* GetStatistics() const;

    // 更新统计信息（渲染循环每帧调用）
    void UpdateStatistics();

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

    // 更新当前播放时间 / 进度
    void SetCurrentTime(
        double time);

    // 音频是否可用（有音频流且设备打开成功）
    bool HasAudio() const;

    // ---------- 成员：核心对象（Player 拥有） ----------

    // 解复用器（Demux 线程）
    Demuxer* demuxer = nullptr;

    // 视频解码器（Video 线程）
    VideoDecoder* videoDecoder = nullptr;

    // 音频解码器（Audio 线程）
    AudioDecoder* audioDecoder = nullptr;

    // 音频重采样器（Audio 线程）
    AudioResampler* audioResampler = nullptr;

    // 变速不变调（Audio 线程使用，SetSpeed 跨线程）
    SpeedController* speedController = nullptr;

    // SDL 音频设备（回调线程 + Audio 线程）
    AudioDevice* audioDevice = nullptr;

    // 音视频同步控制器（渲染线程）
    SyncController* syncController = nullptr;

    // Seek 控制器（Demux 线程执行 / 各线程检测代数）
    SeekController* seekController = nullptr;

    // 截图管理器（渲染线程）
    ScreenshotManager* screenshotManager = nullptr;

    // 播放信息统计
    PlayerStatistics* statistics = nullptr;

    // 字幕管理器
    SubtitleManager* subtitleManager = nullptr;

    // 播放列表管理器
    PlaylistManager* playlistManager = nullptr;

    // 播放列表切换请求（渲染线程置位，Run 消费）
    bool switchRequested = false;

    std::string switchPath;

    // ---------- 成员：队列（Player 直接持有） ----------

    PacketQueue videoPacketQueue;   // 视频包队列（Demux -> Video）

    PacketQueue audioPacketQueue;   // 音频包队列（Demux -> Audio）

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

    SwsContext* swsCtx = nullptr;          // YUV -> RGB 转换

    uint8_t* rgbData = nullptr;            // RGB 缓冲

    int rgbLinesize = 0;                   // RGB 每行字节数

    // ---------- 成员：OSD ----------

    FontManager* fontManager = nullptr;

    OSDManager* osdManager = nullptr;

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
    AVFrame* lastFrame = nullptr;

    // 视频帧时长（无音频时按它匀速播放）
    double videoFrameDuration = 1.0 / 25.0;

    bool hasAudioStream = false;

};
