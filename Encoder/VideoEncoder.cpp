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
    // RAII：ctx 自动释放
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

    ctx.reset(
        avcodec_alloc_context3(codec));

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
        static_cast<int64_t>(
            bitrateKbpsOverride > 0 ?
            bitrateKbpsOverride :
            bitrateKbps) * 1000;

    // GOP：关键帧间隔（默认 2 秒，可调）
    ctx->gop_size =
        gopSize > 0 ?
        gopSize :
        static_cast<int>(
            av_q2d(av_inv_q(timeBase)) * 2);

    // B 帧数（默认 3；直播低延迟由下方 live 块覆盖）
    ctx->max_b_frames =
        bFrames >= 0 ?
        bFrames :
        3;

    // 编码线程数（0 = 编码器自动）
    if (threads > 0)
    {
        ctx->thread_count = threads;
    }

    // ---------- 9.0：VBV（码率平滑 / 低延迟约束） ----------

    if (maxBitrateKbps > 0)
    {
        ctx->rc_max_rate =
            static_cast<int64_t>(maxBitrateKbps) * 1000;
    }

    if (vbvBufferKbps > 0)
    {
        ctx->rc_buffer_size =
            static_cast<int>(vbvBufferKbps) * 1000;

        if (ctx->rc_max_rate <= 0)
        {
            ctx->rc_max_rate = ctx->bit_rate;
        }
    }

    // 编码档位 / 级别（通用选项，编码器不支持时静默忽略）
    if (!profileOverride.empty())
    {
        av_opt_set(
            ctx.get(),
            "profile",
            profileOverride.c_str(),
            AV_OPT_SEARCH_CHILDREN);
    }

    if (!levelOverride.empty())
    {
        av_opt_set(
            ctx.get(),
            "level",
            levelOverride.c_str(),
            AV_OPT_SEARCH_CHILDREN);
    }

    // ---------- 直播低延迟设置 ----------

    bool wantLowLatency =
        live || lowLatencySet;

    if (wantLowLatency)
    {
        if (codecName == "libx264")
        {
            av_opt_set(
                ctx->priv_data,
                "preset",
                presetOverride.empty() ?
                    "veryfast" :
                    presetOverride.c_str(),
                0);

            av_opt_set(
                ctx->priv_data,
                "tune",
                tuneOverride.empty() ?
                    "zerolatency" :
                    tuneOverride.c_str(),
                0);
        }
        else if (codecName == "h264_nvenc")
        {
            av_opt_set(
                ctx->priv_data,
                "preset",
                presetOverride.empty() ?
                    "ll" :
                    presetOverride.c_str(),
                0);

            av_opt_set(
                ctx->priv_data,
                "zerolatency",
                "1",
                0);

            // 低延迟禁止 B 帧（未显式设置时）
            if (bFrames < 0)
            {
                ctx->max_b_frames = 0;
            }
        }
    }
    else if (!presetOverride.empty() ||
        !tuneOverride.empty())
    {
        // 非直播：preset / tune 覆盖仍生效
        if (!presetOverride.empty())
        {
            av_opt_set(
                ctx->priv_data,
                "preset",
                presetOverride.c_str(),
                0);
        }

        if (!tuneOverride.empty())
        {
            av_opt_set(
                ctx->priv_data,
                "tune",
                tuneOverride.c_str(),
                0);
        }
    }

    // ---------- 9.0：恒定码率（CBR） ----------

    if (cbrOverride)
    {
        if (codecName == "libx264")
        {
            av_opt_set(
                ctx->priv_data,
                "nal-hrd",
                "cbr",
                0);
        }
        else if (codecName == "h264_nvenc")
        {
            av_opt_set(
                ctx->priv_data,
                "rc",
                "cbr",
                0);
        }
    }

    if (!profileName.empty())
    {
        Logger::Info()
            << "[VideoEncoder] Profile : "
            << profileName
            << std::endl;
    }

    // ---------- 打开编码器 ----------

    // GLOBAL_HEADER：SPS/PPS 在 avcodec_open2 时即生成到 extradata，
    // 供 muxer 写 avcC（RTMP/FLV/HLS 均需）。否则 libx264/nvenc
    // 只在关键帧内嵌 SPS/PPS（Annex-B），ctx->extradata 始终为空，
    // 推流时 WriteHeader 写出的 avcC 为空，接收端（如 mediamtx）
    // 直接拒绝：unable to parse H264 config: EOF。
    ctx->flags |=
        AV_CODEC_FLAG_GLOBAL_HEADER;

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
            ctx.get(),
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
            ctx.get(),
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
        ctx.get(),
        nullptr);
}

void VideoEncoder::Close()
{
    // RAII：reset(nullptr) 立即释放
    ctx.reset();

    ready = false;
}

bool VideoEncoder::IsReady() const
{
    return ready;
}

AVCodecContext* VideoEncoder::GetContext() const
{
    return ctx.get();
}

const std::string& VideoEncoder::GetCodecName() const
{
    return codecName;
}

// ============================================================
// 9.0：编码参数调优（Init 之前调用）
// ============================================================

void VideoEncoder::SetGopSize(
    int frames)
{
    gopSize =
        frames > 0 ?
        frames :
        0;
}

void VideoEncoder::SetBFrameCount(
    int count)
{
    bFrames = count;
}

void VideoEncoder::SetLowLatency(
    bool enable)
{
    lowLatencySet = enable;
}

void VideoEncoder::SetBitrateKbps(
    int kbps)
{
    bitrateKbpsOverride =
        kbps > 0 ?
        kbps :
        0;
}

void VideoEncoder::SetMaxBitrateKbps(
    int kbps)
{
    maxBitrateKbps =
        kbps > 0 ?
        kbps :
        0;
}

void VideoEncoder::SetVBVBufferSizeKbps(
    int kbps)
{
    vbvBufferKbps =
        kbps > 0 ?
        kbps :
        0;
}

void VideoEncoder::SetThreads(
    int threads)
{
    this->threads =
        threads > 0 ?
        threads :
        0;
}

void VideoEncoder::SetPreset(
    const std::string& preset)
{
    presetOverride = preset;
}

void VideoEncoder::SetTune(
    const std::string& tune)
{
    tuneOverride = tune;
}

void VideoEncoder::SetProfileLevel(
    const std::string& profile,
    const std::string& level)
{
    profileOverride = profile;

    levelOverride = level;
}

void VideoEncoder::SetCbr(
    bool enable)
{
    cbrOverride = enable;
}

void VideoEncoder::SetProfile(
    const EncoderProfile& profile)
{
    profileName = profile.name;

    presetOverride = profile.preset;

    tuneOverride = profile.tune;

    profileOverride = profile.profile;

    levelOverride = profile.level;

    gopSize = profile.gopSize;

    bFrames = profile.bFrames;

    bitrateKbpsOverride = profile.bitrateKbps;

    maxBitrateKbps = profile.maxBitrateKbps;

    vbvBufferKbps = profile.vbvBufferKbps;

    threads = profile.threads;

    lowLatencySet = profile.lowLatency;

    cbrOverride = profile.cbr;
}

void VideoEncoder::SetProfileName(
    const std::string& name)
{
    SetProfile(
        EncoderProfile::FromName(name));
}
