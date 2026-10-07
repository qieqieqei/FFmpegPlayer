#pragma once

// ============================================================
// VolumeController - 音量控制（6.7）
//
// 接口：
//   SetVolume(0 ~ 100)
//   GetFactor() -> 音量系数 0.0 ~ 1.0
//
// 应用方式（SDL 回调里对 PCM 逐采样缩放）：
//
//   sample *= volume / 100.0
//
// 注意：
//   乘法可能溢出，需要 clamp 到 [-32768, 32767]
//   （见 Apply）
//
// 线程安全：volumePercent 用原子变量，任意线程可读写
// ============================================================

#include <cstdint>
#include <atomic>

class VolumeController
{
public:

    VolumeController();

    // 设置音量百分比 0 ~ 100
    void SetVolume(
        int percent);

    // 获取音量百分比
    int GetVolume() const;

    // 获取音量系数（0.0 ~ 1.0）
    float GetFactor() const;

    // 对 S16 PCM 数据应用音量（静态，可在回调里直接调）
    // data: PCM 数据
    // size: 字节数
    static void Apply(
        uint8_t* data,
        int size,
        float factor);

private:

    std::atomic<int> volumePercent{ 100 };   // 0 ~ 100
};
