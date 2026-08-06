#pragma once

// ============================================================
// SyncController - 音视频同步控制器（5.1）
//
// 同步策略：音频作为主时钟（Audio Clock Master）
//
//   videoPts ----+
//                +----> GetVideoDelay() ------> 视频渲染延迟
//   audioPts ----+
//
//   delay > 0 : 视频超前，需要等待 delay 秒再渲染
//   delay < 0 : 视频落后，落后太多则丢帧追赶
//
// 内部维护两个时钟：
//   audioClock : 音频主时钟
//   videoClock : 视频时钟
// ============================================================

#include "Sync/Clock.h"

class SyncController
{
public:

    SyncController();

    // 计算视频渲染延迟
    // videoPts: 当前视频帧的时间戳（秒）
    // audioPts: 当前音频时钟（秒）
    //
    // 返回：
    //   > 0 : 视频帧超前，延迟这么多秒再显示
    //   < 0 : 视频帧落后，落后超过阈值时应丢帧
    double GetVideoDelay(
        double videoPts,
        double audioPts);

    // 丢帧阈值
    // 视频落后超过该值（秒）就丢帧
    double GetDropThreshold() const;

    // 重置同步状态（Seek 时调用）
    void Reset();

private:

    Clock audioClock;    // 音频主时钟

    Clock videoClock;    // 视频时钟

    double dropThreshold = 0.05;   // 落后超过 50ms 丢帧
};
