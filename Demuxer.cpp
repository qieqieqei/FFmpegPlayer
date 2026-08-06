#include "Demuxer.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <iostream>
#include <algorithm>

Demuxer::Demuxer()
{
}

Demuxer::~Demuxer()
{
    Close();
}

bool Demuxer::Open(
    const std::string& path)
{
    // ---------- 打开输入文件 ----------

    int ret =
        avformat_open_input(
            &fmt,
            path.c_str(),
            nullptr,
            nullptr);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avformat_open_input",
            ret);

        return false;
    }

    // 读取流信息（时长、码率等）
    ret =
        avformat_find_stream_info(
            fmt,
            nullptr);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avformat_find_stream_info",
            ret);

        return false;
    }

    Logger::Info()
        << "[Demuxer] File : "
        << path
        << std::endl;

    Logger::Info()
        << "[Demuxer] Streams : "
        << fmt->nb_streams
        << std::endl;

    Logger::Info()
        << "[Demuxer] Duration : "
        << fmt->duration / static_cast<double>(AV_TIME_BASE)
        << " s"
        << std::endl;

    // ---------- 寻找视频流 / 音频流 ----------

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
    if (!fmt)
    {
        return AVERROR(EINVAL);
    }

    return av_read_frame(
        fmt,
        pkt);
}

bool Demuxer::Seek(
    double seconds)
{
    if (!fmt)
    {
        return false;
    }

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
    if (fmt)
    {
        avformat_close_input(&fmt);
    }
}

AVFormatContext* Demuxer::GetFormatContext() const
{
    return fmt;
}

AVStream* Demuxer::GetVideoStream() const
{
    if (!fmt || videoIndex < 0)
    {
        return nullptr;
    }

    return fmt->streams[videoIndex];
}

AVStream* Demuxer::GetAudioStream() const
{
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
    if (!fmt)
    {
        return 0.0;
    }

    return fmt->duration / static_cast<double>(AV_TIME_BASE);
}
