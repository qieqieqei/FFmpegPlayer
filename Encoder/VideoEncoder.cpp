#include "Encoder/VideoEncoder.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <cstring>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

// ============================================================
// VideoEncoder - 视频编码器
// ============================================================

VideoEncoder::VideoEncoder()
{
}

VideoEncoder::~VideoEncoder()
{
    Close();
}

bool VideoEncoder::Init(
    int width,
    int height,
    AVRational timeBase,
    const std::string& codecName,
    int bitrateKbps,
    bool live)
{
    Close();

    this->codecName = codecName;

    // ---------- 查找编码器 ----------

    const AVCodec* codec =
        avcodec_find_encoder_by_name(
            codecName.c_str());

    if (!codec)
    {
        ErrorHandler::Log(
            ErrorTag::Encoder,
            "Video encoder not found : " +
            codecName);

        return false;
    }

    Logger::Info()
        << "[VideoEncoder] Init : "
        << codecName
        << " "
        << width
        << "x"
        << height
        << " "
        << bitrateKbps
        << "kbps"
        << (live ? " [Live]" : " [VOD]")
        << std::endl;

    // ---------- 分配上下文 ----------

    ctx =
        avcodec_alloc_context3(codec);

    if (!ctx)
    {
        ErrorHandler::Log(
            ErrorTag::Encoder,
            "avcodec_alloc_context3 failed");

        return false;
    }

    ctx->width = width;

    ctx->height = height;

    ctx->time_base = timeBase;

    ctx->framerate = av_inv_q(timeBase);

    ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    ctx->bit_rate =
        static_cast<int64_t>(bitrateKbps) * 1000;

    // GOP：关键帧间隔 2 秒
    ctx->gop_size =
        static_cast<int>(
            av_q2d(av_inv_q(timeBase)) * 2);

    ctx->max_b_frames = 3;

    // ---------- 直播低延迟设置 ----------

    if (live)
    {
        if (codecName == "libx264")
        {
            av_opt_set(
                ctx->priv_data,
                "preset",
                "veryfast",
                0);

            av_opt_set(
                ctx->priv_data,
                "tune",
                "zerolatency",
                0);
        }
        else if (codecName == "h264_nvenc")
        {
            av_opt_set(
                ctx->priv_data,
                "preset",
                "ll",
                0);

            av_opt_set(
                ctx->priv_data,
                "zerolatency",
                "1",
                0);

            // 低延迟禁止 B 帧
            ctx->max_b_frames = 0;
        }
    }

    // ---------- 打开编码器 ----------

    int ret =
        avcodec_open2(
            ctx,
            codec,
            nullptr);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Encoder,
            "avcodec_open2 (" + codecName + ")",
            ret);

        avcodec_free_context(&ctx);

        return false;
    }

    ready = true;

    Logger::Info()
        << "[VideoEncoder] Init success : "
        << codecName
        << " (pix_fmt "
        << av_get_pix_fmt_name(ctx->pix_fmt)
        << ")"
        << std::endl;

    return true;
}

bool VideoEncoder::Encode(
    AVFrame* frame)
{
    if (!ready || !ctx)
    {
        return false;
    }

    int ret =
        avcodec_send_frame(
            ctx,
            frame);

    if (ret < 0 &&
        ret != AVERROR(EAGAIN))
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Encoder,
            "avcodec_send_frame",
            ret);

        return false;
    }

    return true;
}

AVPacket* VideoEncoder::GetPacket()
{
    if (!ready || !ctx)
    {
        return nullptr;
    }

    AVPacket* pkt =
        av_packet_alloc();

    if (!pkt)
    {
        return nullptr;
    }

    int ret =
        avcodec_receive_packet(
            ctx,
            pkt);

    if (ret < 0)
    {
        // EAGAIN = 暂无输出（等下一帧）；EOF = 冲刷结束
        av_packet_free(&pkt);

        return nullptr;
    }

    return pkt;
}

void VideoEncoder::Flush()
{
    if (!ready || !ctx)
    {
        return;
    }

    // 送空帧触发冲刷
    avcodec_send_frame(
        ctx,
        nullptr);
}

void VideoEncoder::Close()
{
    if (ctx)
    {
        avcodec_free_context(&ctx);

        ctx = nullptr;
    }

    ready = false;
}

bool VideoEncoder::IsReady() const
{
    return ready;
}

AVCodecContext* VideoEncoder::GetContext() const
{
    return ctx;
}

const std::string& VideoEncoder::GetCodecName() const
{
    return codecName;
}
