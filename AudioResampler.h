#pragma once

extern "C"
{

#include <libswresample/swresample.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>

}

// ============================================================
// AudioResampler - 音频重采样器
//
// 职责：
//   把解码器输出的各种格式
//   （FLTP / S16 / 44100Hz / 单声道 ...）
//   统一转换成 S16 / 48000Hz / 双声道
//
// Seek 后需要 Reset() 清空 swr 内部缓冲，
// 否则会残留 Seek 前的采样数据
// ============================================================

class AudioResampler
{

public:

    AudioResampler();

    ~AudioResampler();

    bool Init(
        AVFrame* frame);

    int Convert(
        AVFrame* frame,
        uint8_t* outputBuffer,
        int outputSize);

    int GetOutputChannels() const;        // 获取输出声道数

    int GetOutputSampleRate() const;      // 获取输出采样率

    // 重采样器是否已初始化
    bool IsReady() const;

    // 清空 swr 内部缓冲（Seek 后调用，保留配置）
    void Reset();

    void Close();

private:

    SwrContext* swrCtx = nullptr;          // 重采样上下文

    int outSampleRate = 48000;             // 输出采样率

    AVSampleFormat outFormat =
        AV_SAMPLE_FMT_S16;                 // 输出格式

    int outChannels = 2;                   // 输出声道

};
