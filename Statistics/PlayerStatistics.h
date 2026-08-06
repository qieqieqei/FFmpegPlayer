#pragma once

// ============================================================
// PlayerStatistics - 播放信息统计（5.7）
//
// 保存并计算：
//   - 分辨率 Resolution
//   - 编码格式 Codec（视频/音频）
//   - FPS（标称 + 实测）
//   - 码率 Bitrate
//   - 缓冲 Buffer（视频帧队列 / 音频缓冲）
//
// 线程安全：内部用 mutex 保护，任何线程都可以读写
//
// OSD 显示示例：
//   Resolution : 1280x720
//   Codec      : H264 / AAC
//   FPS        : 30.0 (Measured 29.8)
//   Bitrate    : 116 kbps
//   Buffer     : Video 3 frames / Audio 120 ms
// ============================================================

#include <string>
#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
}

class PlayerStatistics
{
public:

    PlayerStatistics();

    // 初始化静态信息（分辨率、编码、标称 FPS）
    void Init(
        AVFormatContext* fmt,
        int videoIndex,
        int audioIndex);

    // 渲染了一帧（用于实测 FPS）
    void OnFrameRendered();

    // 丢了一帧（用于统计）
    void OnFrameDropped();

    // 更新缓冲状态（每帧调用一次即可）
    void UpdateBuffers(
        int videoPackets,
        int audioPackets,
        int videoFrames,
        int audioBufferBytes,
        int audioSampleRate,
        int audioChannels);

    // 设置实测码率（bps），Demux 线程测量
    void SetBitrate(
        double bps);

    // ---------- 读取 ----------

    double GetFPS() const;

    double GetNominalFPS() const;

    double GetBitrate() const;          // bps

    int GetVideoPackets() const;

    int GetAudioPackets() const;

    int GetVideoFrames() const;

    int GetAudioBufferMs() const;

    std::string GetResolution() const;

    std::string GetVideoCodec() const;

    std::string GetAudioCodec() const;

    // 丢帧数量（累计）
    int GetDroppedFrames() const;

    // 码率格式化为字符串，例如 "116 kbps"
    static std::string FormatBitrate(
        double bps);

private:

    void UpdateFPS();

    mutable std::mutex mutex;

    // 静态信息
    std::string resolution;

    std::string videoCodec;

    std::string audioCodec;

    double nominalFps = 0.0;

    // 动态信息
    double fps = 0.0;

    double bitrate = 0.0;

    int videoPackets = 0;

    int audioPackets = 0;

    int videoFrames = 0;

    int audioBufferMs = 0;

    int droppedFrames = 0;

    // FPS 测量状态
    int frameCount = 0;

    double lastFpsTime = 0.0;
};
