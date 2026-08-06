#pragma once

// ============================================================
// SpeedController - 播放速度控制（6.6）
//
// 统一管理播放速度，同时作用于：
//
//   视频：GetFrameDelay() 计算帧间隔（无音频时按帧率匀速播放）
//   音频：内部持有 AudioSpeedController（SOLA 变速不变调）
//
// 接口：
//   SetSpeed(0.5 / 1.0 / 1.5 / 2.0)
//   GetFrameDelay(frameDuration) = frameDuration / speed
// ============================================================

#include <atomic>

#include "Audio/AudioSpeedController.h"

class SpeedController
{
public:

    SpeedController();

    // 初始化内部音频变速器
    // sampleRate / channels 与重采样器输出一致（48000 / 2）
    bool Init(
        int sampleRate,
        int channels);

    // 设置播放速度（建议 0.25 ~ 4.0）
    void SetSpeed(
        double speed);

    double GetSpeed() const;

    // 计算视频帧间隔（秒）：frameDuration / speed
    double GetFrameDelay(
        double frameDuration) const;

    // ---------- 音频变速转发（AudioSpeedController） ----------

    int Process(
        const uint8_t* in,
        int inBytes,
        uint8_t* out,
        int outCap);

    int Flush(
        uint8_t* out,
        int outCap);

    int PendingBytes() const;

    // 重置内部状态（Seek 时调用）
    void Reset();

private:

    // 播放速度（渲染线程 SetSpeed / 音频线程 Process 跨线程访问）
    std::atomic<double> speed{ 1.0 };

    // 内部音频变速器（SOLA）
    AudioSpeedController audioSpeed;
};
