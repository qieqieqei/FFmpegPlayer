#pragma once

// ============================================================
// MasterClock - 主时钟选择器（8.1）
//
// 统一"当前以谁为准"：
//
//   MasterClock
//      |
//      +-- Audio 模式：以 AudioClock 为准（有音频时）
//      |
//      +-- Video 模式：以 VideoClock 为准（无音频 / 音频不可用）
//
// 模式自动切换规则（Auto）：
//   - 音频可用（已绑定 AudioClock）-> Audio 模式
//   - 音频不可用 -> Video 模式
//
// 渲染线程每帧查询 GetTime() 作为同步基准，
// 视频帧延迟 = videoPts - masterTime。
// ============================================================

#include <atomic>

class AudioClock;

class VideoClock;

class MasterClock
{
public:

    MasterClock();

    enum class Mode
    {
        Auto,      // 自动：音频可用则音频，否则视频（默认）
        Audio,     // 强制音频主时钟
        Video      // 强制视频主时钟
    };

    // 绑定/解绑时钟源（音频设备创建/销毁时调用）
    void SetAudioClock(
        AudioClock* clock);

    void SetVideoClock(
        VideoClock* clock);

    void SetMode(
        Mode mode);

    Mode GetMode() const;

    // 是否处于音频主时钟模式（实际生效的模式）
    bool IsAudioMaster() const;

    // 主时钟当前时间（秒）
    double GetTime() const;

    // 重置（Seek / 切换媒体时调用）
    void Reset();

private:

    std::atomic<Mode> mode{ Mode::Auto };

    AudioClock* audioClock = nullptr;   // 借用，不拥有

    VideoClock* videoClock = nullptr;   // 借用，不拥有
};
