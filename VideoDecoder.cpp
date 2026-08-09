#include "VideoDecoder.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <iostream>

VideoDecoder::VideoDecoder()
{
}

VideoDecoder::~VideoDecoder()
{
    // RAII：FFmpegPtr 自动释放 codecCtx / frame
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

    codecCtx.reset(
        avcodec_alloc_context3(codec));

    if (!codecCtx)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "avcodec_alloc_context3 failed");

        return false;
    }

    int ret =
        avcodec_parameters_to_context(
            codecCtx.get(),
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
            codecCtx.get(),
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
        << " extradata="
        << codecCtx->extradata_size
        << std::endl;

    // 解码帧缓冲区
    frame.reset(
        av_frame_alloc());

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
            codecCtx.get(),
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

DecodeResult VideoDecoder::ReceiveFrame(
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
            ErrorTag::Decoder,
            "avcodec_receive_frame (video)",
            ret);

        return DecodeResult::Error;
    }

    // 成功：克隆一帧交给调用方（内部帧复用，
    // 所有权随 out 转移，调用方负责释放）
    out.reset(
        av_frame_clone(frame.get()));

    av_frame_unref(frame.get());

    if (!out)
    {
        return DecodeResult::Error;
    }

    return DecodeResult::Success;
}

void VideoDecoder::Flush()
{
    if (codecCtx)
    {
        avcodec_flush_buffers(
            codecCtx.get());
    }
}

void VideoDecoder::Close()
{
    // RAII：reset(nullptr) 立即释放，等价于旧的 Close()
    codecCtx.reset();

    frame.reset();
}

AVCodecContext* VideoDecoder::GetContext() const
{
    return codecCtx.get();
}

int VideoDecoder::GetWidth() const
{
    return codecCtx ? codecCtx->width : 0;
}

int VideoDecoder::GetHeight() const
{
    return codecCtx ? codecCtx->height : 0;
}
