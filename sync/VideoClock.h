#pragma once

// ============================================================
// VideoClock - 视频时钟（8.1）
//
// 记录最近一次渲染/解码帧的 PTS，并按墙钟流逝推进：
//
//   video_clock = lastPts + (now - lastSetTime)
//
// 用途：
//   1. 无音频流时作为主时钟（视频主时钟模式）
//   2. 有音频时记录视频帧时刻，供同步调试 / 漂移分析
//
// 线程安全：渲染线程写（SetPts），其他线程读（Get）。
// ============================================================

#include <chrono>
#include <mutex>

class VideoClock
{
public:

    VideoClock();

    // 记录当前帧的 PTS（渲染线程每帧调用）
    // pts: 帧显示时间戳（秒）
    void SetPts(
        double pts);

    // 获取当前视频时钟（秒）
    // = 最后帧 PTS + 自 SetPts 以来流逝的墙钟时间
    double Get() const;

    // 重置时钟（Seek / 切换媒体时调用）
    void Reset();

private:

    mutable std::mutex mutex;   // 保护基准

    double pts = 0.0;           // 最后帧 PTS（秒）

    std::chrono::steady_clock::time_point lastSet;   // 最后 SetPts 时刻

    bool initialized = false;   // 是否已设置过 PTS
};
