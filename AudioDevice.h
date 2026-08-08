#pragma once

// ============================================================
// AudioDevice - SDL 音频输出设备（6.3 / 6.4 / 6.7）
//
// 数据流：
//
//   AudioDecoder ---> Resampler ---> SpeedController ---> PushPCM()
//                                                          |
//                                                          v
//                                                  PCMQueue（带背压）
//                                                          |
//                                                          v
//                                               SDL AudioCallback
//                                                          |
//                                                          v
//                                           VolumeController（音量）
//                                                          |
//                                                          v
//                                                          声卡
//
// 职责：
//   - 打开 SDL 音频设备
//   - 缓存 PCM 数据（PCMQueue，独立类）
//   - 维护音频主时钟（AudioClock，独立类）
//   - 音量控制（VolumeController，独立类）
//
// 第六阶段重构：内部实现全部委托给独立类，
// 本类只负责 SDL 设备生命周期 + 回调桥接
// ============================================================

#include <SDL.h>

#include <iostream>
#include <atomic>

#include "Audio/PCMQueue.h"
#include "Sync/AudioClock.h"
#include "Audio/VolumeController.h"

class AudioDevice
{
public:

    AudioDevice();

    ~AudioDevice();

    // 初始化 SDL Audio
    // sampleRate: 采样率，例如 48000
    // channels:   声道数，例如 2
    bool Init(
        int sampleRate,
        int channels);

    // 推送 PCM 数据（S16）
    // 队列达到最大缓冲时会阻塞等待（背压）
    // abort: 可选指针，等待期间若 *abort == true 则放弃推送（Seek/退出用）
    void PushPCM(
        const uint8_t* data,
        int size,
        const std::atomic<bool>* abort = nullptr);

    // 获取当前缓冲字节数
    int GetQueuedSize();

    // 获取音频主时钟（媒体时间，秒）
    // = 时钟基准 + 已播放输出时间 * 播放速度
    double GetAudioClock() const;

    // 获取音频时钟对象（借用指针，生命周期随本对象）
    // SyncController 绑定主时钟用（8.1 接入）
    AudioClock* GetClock()
    {
        return &clock;
    }

    // 重置时钟（Seek 时调用）
    // baseSeconds: Seek 目标时间，作为新时钟基准
    void ResetClock(
        double baseSeconds);

    // 清空 PCM 队列
    void ClearQueue();

    // 暂停 / 恢复音频设备
    void SetPaused(
        bool paused);

    // 设置播放速度（影响音频时钟换算）
    void SetSpeedFactor(
        double speed);

    // 设置音量 0~100（转发给 VolumeController）
    void SetVolume(
        int percent);

    int GetVolume() const;

    // 打断阻塞中的 PushPCM（Seek / 退出时调用）
    void Interrupt();

    void ResetInterrupt();

    // 释放音频设备
    void Close();

private:

    // SDL 音频回调（静态，SDL 音频线程调用）
    static void AudioCallback(
        void* userdata,
        Uint8* stream,
        int len);

    // SDL音频设备ID
    SDL_AudioDeviceID device = 0;

    // 音频采样率
    int sampleRate = 48000;

    // 声道数量
    int channels = 2;

    // PCM 数据队列（独立类，带背压）
    PCMQueue pcmQueue;

    // 音频主时钟（独立类）
    AudioClock clock;

    // 音量控制（独立类）
    VolumeController volume;
};
