#pragma once

// ============================================================
// AudioMixer - 音量控制（5.5）
//
// 数据流：
//
//   PCM Queue
//      |
//      v
//   AudioMixer
//      |
//      v
//   SDL callback
//
// 原理：
//   S16 采样值 * 音量系数
//
//   sample *= volume / 100.0
//
// 注意：
//   乘法可能溢出，需要 clamp 到 [-32768, 32767]
// ============================================================

#include <cstdint>
#include <atomic>

class AudioMixer
{
public:

    AudioMixer();

    // 设置音量百分比 0 ~ 100
    void SetVolume(
        int percent);

    // 获取音量百分比
    int GetVolume() const;

    // 获取音量系数（0.0 ~ 1.0）
    float GetFactor() const;

    // 对 S16 PCM 数据应用音量
    // data: PCM 数据
    // size: 字节数
    static void Apply(
        uint8_t* data,
        int size,
        float factor);

private:

    std::atomic<int> volumePercent;   // 0 ~ 100
};
