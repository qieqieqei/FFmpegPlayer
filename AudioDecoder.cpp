#include "AudioDecoder.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <iostream>

AudioDecoder::AudioDecoder()
{
}

AudioDecoder::~AudioDecoder()
{
    // RAII：AVCodecContextPtr / AVFramePtr 自动释放
}

bool AudioDecoder::Init(
    AVCodecParameters* codecpar)
{
    if (!codecpar)
    {
        return false;
    }

    const AVCodec* decoder =
        avcodec_find_decoder(
            codecpar->codec_id);

    if (!decoder)
    {
        ErrorHandler::Log(
            ErrorTag::Audio,
            "Audio Decoder Not Found : " +
            std::string(avcodec_get_name(codecpar->codec_id)));

        return false;
    }

    codecCtx.reset(
        avcodec_alloc_context3(
            decoder));

    if (!codecCtx)
    {
        ErrorHandler::Log(
            ErrorTag::Audio,
            "avcodec_alloc_context3 failed");

        return false;
    }

    if (avcodec_parameters_to_context(
        codecCtx.get(),
        codecpar)
        < 0)
    {
        ErrorHandler::Log(
            ErrorTag::Audio,
            "avcodec_parameters_to_context failed");

        return false;
    }

    if (avcodec_open2(
        codecCtx.get(),
        decoder,
        nullptr)
        < 0)
    {
        ErrorHandler::Log(
            ErrorTag::Audio,
            "avcodec_open2 failed");

        return false;
    }

    frame.reset(
        av_frame_alloc());

    if (!frame)
    {
        ErrorHandler::Log(
            ErrorTag::Audio,
            "av_frame_alloc failed");

        return false;
    }

    Logger::Info()
        << "[Audio] Decoder Init Success ("
        << avcodec_get_name(codecCtx->codec_id)
        << ", "
        << codecCtx->sample_rate
        << " Hz, "
        << codecCtx->ch_layout.nb_channels
        << " ch)"
        << std::endl;

    return true;
}

bool AudioDecoder::SendPacket(
    AVPacket* packet)
{
    if (!codecCtx)
    {
        return false;
    }

    int ret =
        avcodec_send_packet(
            codecCtx.get(),
            packet);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Audio,
            "avcodec_send_packet",
            ret);
    }

    return ret >= 0;
}

DecodeResult AudioDecoder::ReceiveFrame(
    FramePtr& out)
{
    if (!codecCtx ||
        !frame)
    {
        return DecodeResult::Error;
    }

    int ret =
        avcodec_receive_frame(
            codecCtx.get(),
            frame.get());

    if (ret == AVERROR(EAGAIN))
    {
        // 需要更多包：不是错误
        return DecodeResult::NeedMorePacket;
    }

    if (ret == AVERROR_EOF)
    {
        // 解码真正结束
        return DecodeResult::End;
    }

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Audio,
            "avcodec_receive_frame",
            ret);

        return DecodeResult::Error;
    }

    // 成功：克隆一帧交给调用方（内部帧复用，所有权随 out 转移）
    out.reset(
        av_frame_clone(frame.get()));

    av_frame_unref(frame.get());

    if (!out)
    {
        return DecodeResult::Error;
    }

    return DecodeResult::Success;
}

void AudioDecoder::Flush()
{
    if (codecCtx)
    {
        // 清空解码器内部缓冲（Seek 后调用）
        avcodec_flush_buffers(codecCtx.get());
    }
}

void AudioDecoder::Close()
{
    // RAII：reset(nullptr) 立即释放
    codecCtx.reset();

    frame.reset();
}
