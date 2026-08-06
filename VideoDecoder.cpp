#include "VideoDecoder.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <iostream>

VideoDecoder::VideoDecoder()
{
}

VideoDecoder::~VideoDecoder()
{
    Close();
}

bool VideoDecoder::Init(
    AVCodecParameters* codecpar)
{
    if (!codecpar)
    {
        return false;
    }

    // ---------- 查找并创建解码器 ----------

    const AVCodec* codec =
        avcodec_find_decoder(
            codecpar->codec_id);

    if (!codec)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "Video decoder not found");

        return false;
    }

    codecCtx =
        avcodec_alloc_context3(codec);

    if (!codecCtx)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "avcodec_alloc_context3 failed");

        return false;
    }

    int ret =
        avcodec_parameters_to_context(
            codecCtx,
            codecpar);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avcodec_parameters_to_context",
            ret);

        return false;
    }

    ret =
        avcodec_open2(
            codecCtx,
            codec,
            nullptr);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avcodec_open2 (video)",
            ret);

        return false;
    }

    Logger::Info()
        << "[VideoDecoder] Codec : "
        << codec->name
        << " ("
        << codecCtx->width
        << "x"
        << codecCtx->height
        << ")"
        << std::endl;

    // 解码帧缓冲区
    frame =
        av_frame_alloc();

    if (!frame)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "av_frame_alloc failed");

        return false;
    }

    return true;
}

bool VideoDecoder::SendPacket(
    AVPacket* pkt)
{
    if (!codecCtx)
    {
        return false;
    }

    int ret =
        avcodec_send_packet(
            codecCtx,
            pkt);

    if (ret < 0 &&
        ret != AVERROR(EAGAIN))
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avcodec_send_packet (video)",
            ret);

        return false;
    }

    return true;
}

AVFrame* VideoDecoder::ReceiveFrame()
{
    if (!codecCtx ||
        !frame)
    {
        return nullptr;
    }

    int ret =
        avcodec_receive_frame(
            codecCtx,
            frame);

    if (ret < 0)
    {
        // EAGAIN（需要更多包）/ EOF（解码结束）
        // 都不是错误，返回 nullptr
        return nullptr;
    }

    return frame;
}

void VideoDecoder::Flush()
{
    if (codecCtx)
    {
        avcodec_flush_buffers(
            codecCtx);
    }
}

void VideoDecoder::Close()
{
    if (frame)
    {
        av_frame_free(&frame);
    }

    if (codecCtx)
    {
        avcodec_free_context(&codecCtx);
    }
}

AVCodecContext* VideoDecoder::GetContext() const
{
    return codecCtx;
}

int VideoDecoder::GetWidth() const
{
    return codecCtx ? codecCtx->width : 0;
}

int VideoDecoder::GetHeight() const
{
    return codecCtx ? codecCtx->height : 0;
}
