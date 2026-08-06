#include "AudioResampler.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <iostream>

AudioResampler::AudioResampler()
{
}

AudioResampler::~AudioResampler()
{
    Close();
}

bool AudioResampler::Init(
    AVFrame* frame)
{
    if (!frame)
    {
        return false;
    }

    // 已初始化且参数匹配则直接复用
    if (swrCtx &&
        frame->sample_rate == outSampleRate &&
        static_cast<AVSampleFormat>(frame->format) == outFormat &&
        frame->ch_layout.nb_channels == outChannels)
    {
        return true;
    }

    Close();

    AVChannelLayout outLayout;       // 输出声道布局

    av_channel_layout_default(
        &outLayout,
        outChannels);                // 设置输出双声道

    int ret =
        swr_alloc_set_opts2(
            &swrCtx,                 // 返回 SwrContext

            &outLayout,              // 输出声道布局

            outFormat,               // 输出采样格式 S16

            outSampleRate,           // 输出采样率

            &frame->ch_layout,       // 输入声道布局

            (AVSampleFormat)
            frame->format,            // 输入格式

            frame->sample_rate,       // 输入采样率

            0,
            nullptr);

    av_channel_layout_uninit(
        &outLayout);                // 释放临时布局

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Audio,
            "swr_alloc_set_opts2",
            ret);

        swrCtx = nullptr;

        return false;
    }

    if (
        swr_init(swrCtx)
        < 0)
    {
        ErrorHandler::Log(
            ErrorTag::Audio,
            "swr_init failed");

        swr_free(&swrCtx);

        swrCtx = nullptr;

        return false;
    }

    Logger::Info()
        << "[Audio] SwrContext Init Success ("
        << outSampleRate
        << " Hz, "
        << outChannels
        << " ch)"
        << std::endl;

    return true;
}

int AudioResampler::Convert(
    AVFrame* frame,
    uint8_t* outputBuffer,
    int outputSize)
{
    if (!swrCtx ||          // SwrContext必须存在
        !frame ||           // 输入Frame不能为空
        !outputBuffer)      // 输出Buffer不能为空
    {
        return -1;
    }

    uint8_t* outBuffer[1];

    outBuffer[0] =
        outputBuffer;       // S16输出数据地址

    int maxSamples =
        outputSize /
        (
            outChannels *
            av_get_bytes_per_sample(
                outFormat)
            );                  // 根据Buffer大小计算最大采样数量

    int samples =
        swr_convert(
            swrCtx,                     // 重采样上下文

            outBuffer,                  // 输出PCM

            maxSamples,                 // 输出最大采样数

            (const uint8_t**)frame->data, // 输入FLTP数据

            frame->nb_samples);         // 输入采样数量

    if (samples < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Audio,
            "swr_convert",
            samples);

        return -1;
    }

    return samples;        // 返回转换后的采样数量
}

int AudioResampler::GetOutputChannels() const
{
    return outChannels;
}

int AudioResampler::GetOutputSampleRate() const
{
    return outSampleRate;
}

bool AudioResampler::IsReady() const
{
    return swrCtx != nullptr;
}

void AudioResampler::Reset()
{
    if (swrCtx)
    {
        // 清空 swr 内部缓冲（Seek 后调用，保留配置）
        swr_close(swrCtx);

        swr_init(swrCtx);
    }
}

void AudioResampler::Close()
{
    if (swrCtx)
    {
        swr_free(
            &swrCtx);

        swrCtx = nullptr;
    }
}
