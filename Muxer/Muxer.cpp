#include "Muxer/Muxer.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

// ============================================================
// Muxer - 封装器基类
// ============================================================

Muxer::Muxer()
{
}

Muxer::~Muxer()
{
    Close();
}

AVStream* Muxer::AddVideoStream(
    AVCodecParameters* codecpar,
    AVRational streamTimeBase)
{
    if (!fmt || !codecpar)
    {
        return nullptr;
    }

    AVStream* stream =
        avformat_new_stream(
            fmt,
            nullptr);

    if (!stream)
    {
        ErrorHandler::Log(
            ErrorTag::Muxer,
            "avformat_new_stream (video) failed");

        return nullptr;
    }

    stream->id =
        static_cast<int>(fmt->nb_streams - 1);

    int ret =
        avcodec_parameters_copy(
            stream->codecpar,
            codecpar);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Muxer,
            "avcodec_parameters_copy (video)",
            ret);

        return nullptr;
    }

    stream->time_base =
        streamTimeBase;

    return stream;
}

AVStream* Muxer::AddAudioStream(
    AVCodecParameters* codecpar)
{
    if (!fmt || !codecpar)
    {
        return nullptr;
    }

    AVStream* stream =
        avformat_new_stream(
            fmt,
            nullptr);

    if (!stream)
    {
        ErrorHandler::Log(
            ErrorTag::Muxer,
            "avformat_new_stream (audio) failed");

        return nullptr;
    }

    stream->id =
        static_cast<int>(fmt->nb_streams - 1);

    int ret =
        avcodec_parameters_copy(
            stream->codecpar,
            codecpar);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Muxer,
            "avcodec_parameters_copy (audio)",
            ret);

        return nullptr;
    }

    return stream;
}

bool Muxer::WriteHeader()
{
    if (!fmt || headerWritten)
    {
        return false;
    }

    int ret =
        avformat_write_header(
            fmt,
            nullptr);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Muxer,
            "avformat_write_header",
            ret);

        return false;
    }

    headerWritten = true;

    return true;
}

bool Muxer::WritePacket(
    AVPacket* pkt)
{
    if (!fmt || !pkt)
    {
        return false;
    }

    // ---------- 时间基转换 ----------
    // 编码器输出包的时间基在 pkt->time_base，
    // 需要转换到输出流的时间基

    if (pkt->stream_index < 0 ||
        pkt->stream_index >=
            static_cast<int>(fmt->nb_streams))
    {
        ErrorHandler::Log(
            ErrorTag::Muxer,
            "WritePacket : bad stream_index");

        return false;
    }

    AVStream* stream =
        fmt->streams[pkt->stream_index];

    if (pkt->pts != AV_NOPTS_VALUE)
    {
        pkt->pts =
            av_rescale_q(
                pkt->pts,
                pkt->time_base,
                stream->time_base);
    }

    if (pkt->dts != AV_NOPTS_VALUE)
    {
        pkt->dts =
            av_rescale_q(
                pkt->dts,
                pkt->time_base,
                stream->time_base);
    }

    if (pkt->duration > 0)
    {
        pkt->duration =
            av_rescale_q(
                pkt->duration,
                pkt->time_base,
                stream->time_base);
    }

    pkt->time_base =
        stream->time_base;

    pkt->pos = -1;

    int ret =
        av_interleaved_write_frame(
            fmt,
            pkt);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Muxer,
            "av_interleaved_write_frame",
            ret);

        return false;
    }

    return true;
}

void Muxer::WriteTrailer()
{
    if (!fmt || trailerWritten)
    {
        return;
    }

    int ret =
        av_write_trailer(fmt);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Muxer,
            "av_write_trailer",
            ret);
    }

    trailerWritten = true;
}

void Muxer::Close()
{
    if (fmt)
    {
        // 未写尾时补写（异常中断也保证文件完整）
        if (headerWritten &&
            !trailerWritten)
        {
            av_write_trailer(fmt);
        }

        avio_closep(&fmt->pb);

        avformat_free_context(fmt);

        fmt = nullptr;
    }

    headerWritten = false;

    trailerWritten = false;

    url.clear();
}

bool Muxer::IsOpen() const
{
    return fmt != nullptr;
}

AVFormatContext* Muxer::GetFormatContext() const
{
    return fmt;
}

const std::string& Muxer::GetUrl() const
{
    return url;
}

const char* Muxer::GetFormatName() const
{
    // 默认：按 URL 后缀自动探测
    return nullptr;
}
