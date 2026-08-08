#include "Encoder/AudioEncoder.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

// ============================================================
// AudioEncoder - 音频编码器
// ============================================================

AudioEncoder::AudioEncoder()
{
}

AudioEncoder::~AudioEncoder()
{
    // RAII：ctx / swr / convFrame 自动释放
}

bool AudioEncoder::Init(
    int sampleRate,
    int channels,
    const std::string& codecName,
    int bitrateKbps)
{
    Close();

    this->codecName = codecName;

    inSampleRate = sampleRate;

    inChannels = channels;

    inFmt = AV_SAMPLE_FMT_S16;

    // ---------- 查找编码器 ----------

    const AVCodec* codec =
        avcodec_find_encoder_by_name(
            codecName.c_str());

    if (!codec)
    {
        ErrorHandler::Log(
            ErrorTag::Encoder,
            "Audio encoder not found : " +
            codecName);

        return false;
    }

    Logger::Info()
        << "[AudioEncoder] Init : "
        << codecName
        << " "
        << sampleRate
        << "Hz "
        << channels
        << "ch"
        << std::endl;

    // ---------- 分配上下文 ----------

    ctx.reset(
        avcodec_alloc_context3(codec));

    if (!ctx)
    {
        ErrorHandler::Log(
            ErrorTag::Encoder,
            "avcodec_alloc_context3 failed");

        return false;
    }

    ctx->sample_rate = sampleRate;

    ctx->ch_layout.nb_channels = channels;

    av_channel_layout_default(
        &ctx->ch_layout,
        channels);

    // 编码器要求的采样格式（AAC -> FLTP；Opus -> FLT）
    // FFmpeg 7.1+ 推荐 avcodec_get_supported_config
    // （AVCodec::sample_fmts 已废弃）
    const enum AVSampleFormat* fmts = nullptr;

    int nbFormats = 0;

    avcodec_get_supported_config(
        nullptr,                  // ctx（用 codec 查询即可）
        codec,
        AV_CODEC_CONFIG_SAMPLE_FORMAT,
        0,
        reinterpret_cast<const void**>(&fmts),
        &nbFormats);

    ctx->sample_fmt =
        (nbFormats > 0 && fmts) ?
        fmts[0] :
        AV_SAMPLE_FMT_FLTP;

    ctx->bit_rate =
        static_cast<int64_t>(
            (bitrateKbps > 0 ?
            bitrateKbps :
            (codecName == "opus" ? 96 : 128))) *
        1000;

    ctx->time_base =
        AVRational{ 1, sampleRate };

    // ---------- 打开编码器 ----------

    int ret =
        avcodec_open2(
            ctx.get(),
            codec,
            nullptr);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Encoder,
            "avcodec_open2 (" + codecName + ")",
            ret);

        ctx.reset();

        return false;
    }

    // ---------- 重采样器（S16 -> 编码器格式） ----------

    SwrContext* newSwr = nullptr;

    ret =
        swr_alloc_set_opts2(
            &newSwr,
            &ctx->ch_layout,
            ctx->sample_fmt,
            ctx->sample_rate,
            &ctx->ch_layout,
            inFmt,
            inSampleRate,
            0,
            nullptr);

    if (ret < 0 || !newSwr)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Encoder,
            "swr_alloc_set_opts2",
            ret);

        ctx.reset();

        return false;
    }

    swr.reset(newSwr);

    ret =
        swr_init(swr.get());

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Encoder,
            "swr_init",
            ret);

        swr.reset();

        ctx.reset();

        return false;
    }

    // 转换输出帧（内部复用）
    convFrame.reset(
        av_frame_alloc());

    if (!convFrame)
    {
        ErrorHandler::Log(
            ErrorTag::Encoder,
            "av_frame_alloc failed");

        swr.reset();

        ctx.reset();

        return false;
    }

    convFrame->format = ctx->sample_fmt;

    convFrame->sample_rate = ctx->sample_rate;

    av_channel_layout_copy(
        &convFrame->ch_layout,
        &ctx->ch_layout);

    ready = true;

    Logger::Info()
        << "[AudioEncoder] Init success : "
        << codecName
        << " (sample_fmt "
        << av_get_sample_fmt_name(ctx->sample_fmt)
        << ")"
        << std::endl;

    return true;
}

bool AudioEncoder::Encode(
    AVFrame* frame)
{
    if (!ready || !ctx)
    {
        return false;
    }

    AVFrame* in =
        ConvertFrame(frame);

    if (!in)
    {
        return false;
    }

    int ret =
        avcodec_send_frame(
            ctx.get(),
            in);

    if (ret < 0 &&
        ret != AVERROR(EAGAIN))
    {
        // 详细字段日志：定位 EINVAL 根因
        ErrorHandler::LogFFmpeg(
            ErrorTag::Encoder,
            "avcodec_send_frame (audio)",
            ret);

        ErrorHandler::Log(
            ErrorTag::Encoder,
            "audio frame fmt=" +
            std::to_string(in->format) +
            " ctx fmt=" +
            std::to_string(ctx->sample_fmt) +
            " rate=" +
            std::to_string(in->sample_rate) +
            "/" +
            std::to_string(ctx->sample_rate) +
            " ch=" +
            std::to_string(in->ch_layout.nb_channels) +
            "/" +
            std::to_string(ctx->ch_layout.nb_channels) +
            " nb=" +
            std::to_string(in->nb_samples));

        return false;
    }

    return true;
}

AVPacket* AudioEncoder::GetPacket()
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
            ctx.get(),
            pkt);

    if (ret < 0)
    {
        av_packet_free(&pkt);

        return nullptr;
    }

    return pkt;
}

void AudioEncoder::Flush()
{
    if (!ready || !ctx)
    {
        return;
    }

    avcodec_send_frame(
        ctx.get(),
        nullptr);
}

void AudioEncoder::Close()
{
    // RAII：reset(nullptr) 立即释放全部资源
    convFrame.reset();

    swr.reset();

    ctx.reset();

    ready = false;
}

bool AudioEncoder::IsReady() const
{
    return ready;
}

AVCodecContext* AudioEncoder::GetContext() const
{
    return ctx.get();
}

const std::string& AudioEncoder::GetCodecName() const
{
    return codecName;
}

// ============================================================
// 重采样：输入帧 -> 编码器所需格式
// ============================================================

AVFrame* AudioEncoder::ConvertFrame(
    AVFrame* frame)
{
    if (!frame ||
        !frame->data[0] ||
        !swr ||
        !ctx ||
        !convFrame)
    {
        return nullptr;
    }

    int outSamples =
        swr_get_out_samples(
            swr.get(),
            frame->nb_samples);

    // 释放上一帧的缓冲（av_samples_alloc 每次都会新分配）
    // 注意：av_frame_unref 会把 format/sample_rate 重置为默认，
    // 必须恢复，否则 avcodec_send_frame 报 EINVAL
    av_frame_unref(
        convFrame.get());

    convFrame->format =
        ctx->sample_fmt;

    convFrame->sample_rate =
        ctx->sample_rate;

    av_channel_layout_copy(
        &convFrame->ch_layout,
        &ctx->ch_layout);

    int ret =
        av_samples_alloc(
            convFrame->data,
            convFrame->linesize,
            ctx->ch_layout.nb_channels,
            outSamples,
            ctx->sample_fmt,
            0);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Encoder,
            "av_samples_alloc",
            ret);

        return nullptr;
    }

    convFrame->nb_samples =
        outSamples;

    // 输入音频数据指针（S16 平面 / 交错统一处理）
    const uint8_t* inData[8] = { nullptr };

    if (av_sample_fmt_is_planar(inFmt))
    {
        for (int c = 0; c < inChannels; c++)
        {
            inData[c] =
                frame->data[c];
        }
    }
    else
    {
        // 交错格式：当作单平面（声道数=1 的假象，
        // 由 swr 的 channel layout 负责解交错）
        inData[0] =
            frame->data[0];
    }

    int converted =
        swr_convert(
            swr.get(),
            convFrame->data,
            outSamples,
            inData,
            frame->nb_samples);

    if (converted < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Encoder,
            "swr_convert",
            converted);

        return nullptr;
    }

    convFrame->nb_samples =
        converted;

    // swr_convert 不复制 pts；av_frame_unref 已重置它，必须恢复
    convFrame->pts =
        frame->pts;

    return convFrame.get();
}
