#include "AudioDecoder.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <iostream>

AudioDecoder::AudioDecoder()
{
}

AudioDecoder::~AudioDecoder()
{
    Close();
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

    codecCtx =
        avcodec_alloc_context3(
            decoder);

    if (!codecCtx)
    {
        ErrorHandler::Log(
            ErrorTag::Audio,
            "avcodec_alloc_context3 failed");

        return false;
    }

    if (avcodec_parameters_to_context(
        codecCtx,
        codecpar)
        < 0)
    {
        ErrorHandler::Log(
            ErrorTag::Audio,
            "avcodec_parameters_to_context failed");

        return false;
    }

    if (avcodec_open2(
        codecCtx,
        decoder,
        nullptr)
        < 0)
    {
        ErrorHandler::Log(
            ErrorTag::Audio,
            "avcodec_open2 failed");

        return false;
    }

    frame =
        av_frame_alloc();

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
            codecCtx,
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

AVFrame* AudioDecoder::ReceiveFrame()
{
    if (!codecCtx)
    {
        return nullptr;
    }

    int ret =
        avcodec_receive_frame(
            codecCtx,
            frame);

    if (ret < 0)
    {
        return nullptr;
    }

    return frame;
}

void AudioDecoder::Flush()
{
    if (codecCtx)
    {
        // 清空解码器内部缓冲（Seek 后调用）
        avcodec_flush_buffers(codecCtx);
    }
}

void AudioDecoder::Close()
{
    if (frame)
    {
        av_frame_free(
            &frame);
    }

    if (codecCtx)
    {
        avcodec_free_context(
            &codecCtx);
    }
}
