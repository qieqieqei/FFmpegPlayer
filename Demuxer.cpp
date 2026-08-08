#include "Demuxer.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <iostream>
#include <algorithm>

// ============================================================
// Demuxer - 解复用器（7.1：接入 InputSource）
// ============================================================

Demuxer::Demuxer()
{
}

Demuxer::~Demuxer()
{
    Close();
}

bool Demuxer::Open(
    const std::string& url)
{
    // ---------- 按 URL 协议创建输入源（7.1） ----------

    source.reset(
        InputSource::Create(
            url,
            networkConfigApplied ?
            &networkConfig :
            nullptr));

    if (!source)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "Create input source failed : " +
            url);

        return false;
    }

    if (!source->Open(url))
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "Open input source failed : " +
            url);

        source.reset();

        return false;
    }

    AVFormatContext* fmt =
        source->GetFormatContext();

    if (!fmt)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "No format context : " +
            url);

        source.reset();

        return false;
    }

    Logger::Info()
        << "[Demuxer] Url : "
        << url
        << std::endl;

    Logger::Info()
        << "[Demuxer] Protocol : "
        << source->GetProtocol()
        << (source->IsLive() ?
            " [Live]" :
            " [VOD]")
        << std::endl;

    Logger::Info()
        << "[Demuxer] Streams : "
        << fmt->nb_streams
        << std::endl;

    if (fmt->duration > 0)
    {
        Logger::Info()
            << "[Demuxer] Duration : "
            << fmt->duration / static_cast<double>(AV_TIME_BASE)
            << " s"
            << std::endl;
    }
    else
    {
        Logger::Info()
            << "[Demuxer] Duration : unknown (live)"
            << std::endl;
    }

    // ---------- 寻找视频流 / 音频流 ----------

    videoIndex = -1;

    audioIndex = -1;

    for (unsigned int i = 0; i < fmt->nb_streams; i++)
    {
        // 逐个检查每个流

        AVStream* stream = fmt->streams[i];

        // 流类型：视频 / 音频 / 字幕 ...

        AVMediaType type =
            stream->codecpar->codec_type;

        if (type == AVMEDIA_TYPE_VIDEO &&
            videoIndex < 0)
        {
            videoIndex = static_cast<int>(i);
        }
        else if (type == AVMEDIA_TYPE_AUDIO &&
            audioIndex < 0)
        {
            audioIndex = static_cast<int>(i);
        }
    }

    if (videoIndex < 0)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "No video stream found");

        return false;
    }

    Logger::Info()
        << "[Demuxer] Video Stream Index : "
        << videoIndex
        << std::endl;

    if (audioIndex >= 0)
    {
        Logger::Info()
            << "[Demuxer] Audio Stream Index : "
            << audioIndex
            << std::endl;
    }
    else
    {
        Logger::Info()
            << "[Demuxer] No audio stream (video only)"
            << std::endl;
    }

    return true;
}

int Demuxer::ReadPacket(
    AVPacket* pkt)
{
    if (!source)
    {
        return AVERROR(EINVAL);
    }

    return av_read_frame(
        source->GetFormatContext(),
        pkt);
}

bool Demuxer::Seek(
    double seconds)
{
    if (!source)
    {
        return false;
    }

    // 直播流不可 Seek（RTSP/RTMP/直播 HLS）
    if (!source->IsSeekable())
    {
        Logger::Warn()
            << "[Demuxer] Seek ignored (live stream)"
            << std::endl;

        return false;
    }

    AVFormatContext* fmt =
        source->GetFormatContext();

    // 目标时间夹在 [0, 时长] 内
    seconds =
        std::max(
            0.0,
            std::min(
                GetDuration(),
                seconds));

    // ---------- 按全局时间基（AV_TIME_BASE）定位 ----------

    int64_t targetTs =
        static_cast<int64_t>(
            seconds * AV_TIME_BASE);

    int ret =
        avformat_seek_file(
            fmt,
            -1,                 // -1：按全局时间基定位
            INT64_MIN,          // 允许向后找任意位置
            targetTs,           // 目标时间戳
            targetTs,           // 最小目标（向后找关键帧）
            0);                 // 无特殊标志

    if (ret < 0)
    {
        // 备用方案：按视频流时间基定位
        AVStream* vStream =
            GetVideoStream();

        if (vStream)
        {
            int64_t streamTs =
                av_rescale_q(
                    targetTs,
                    AV_TIME_BASE_Q,
                    vStream->time_base);

            ret =
                av_seek_frame(
                    fmt,
                    videoIndex,
                    streamTs,
                    AVSEEK_FLAG_BACKWARD);
        }
    }

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "Seek failed",
            ret);

        return false;
    }

    return true;
}

void Demuxer::Close()
{
    // RAII：unique_ptr 自动释放 InputSource
    source.reset();
}

void Demuxer::SetAbort(
    bool abort)
{
    if (source)
    {
        source->SetAbort(abort);
    }
}

void Demuxer::SetNetworkConfig(
    const StreamConfig& config)
{
    networkConfig = config;

    networkConfigApplied = true;
}

// ============================================================
// 流信息查询
// ============================================================

AVFormatContext* Demuxer::GetFormatContext() const
{
    return source ?
        source->GetFormatContext() :
        nullptr;
}

AVStream* Demuxer::GetVideoStream() const
{
    AVFormatContext* fmt =
        GetFormatContext();

    if (!fmt || videoIndex < 0)
    {
        return nullptr;
    }

    return fmt->streams[videoIndex];
}

AVStream* Demuxer::GetAudioStream() const
{
    AVFormatContext* fmt =
        GetFormatContext();

    if (!fmt || audioIndex < 0)
    {
        return nullptr;
    }

    return fmt->streams[audioIndex];
}

int Demuxer::GetVideoIndex() const
{
    return videoIndex;
}

int Demuxer::GetAudioIndex() const
{
    return audioIndex;
}

bool Demuxer::HasAudio() const
{
    return audioIndex >= 0;
}

double Demuxer::GetDuration() const
{
    AVFormatContext* fmt =
        GetFormatContext();

    if (!fmt)
    {
        return 0.0;
    }

    return fmt->duration / static_cast<double>(AV_TIME_BASE);
}

bool Demuxer::IsNetwork() const
{
    return source ?
        source->IsNetwork() :
        false;
}

bool Demuxer::IsLive() const
{
    return source ?
        source->IsLive() :
        false;
}

bool Demuxer::IsSeekable() const
{
    return source ?
        source->IsSeekable() :
        false;
}

const std::string& Demuxer::GetProtocol() const
{
    static const std::string empty;

    return source ?
        source->GetProtocol() :
        empty;
}

const std::string& Demuxer::GetUrl() const
{
    static const std::string empty;

    return source ?
        source->GetUrl() :
        empty;
}
