#include "Muxer/HLSMuxer.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

extern "C" {
#include <libavutil/opt.h>
}

// ============================================================
// HLSMuxer - HLS 封装器
// ============================================================

HLSMuxer::HLSMuxer()
{
}

bool HLSMuxer::OpenOutput(
    const std::string& url)
{
    Close();

    this->url = url;

    int ret =
        avformat_alloc_output_context2(
            &fmt,
            nullptr,
            GetFormatName(),
            url.c_str());

    if (ret < 0 || !fmt)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Muxer,
            "avformat_alloc_output_context2 (hls)",
            ret);

        return false;
    }

    // ---------- HLS 参数 ----------

    // 每段时长（秒）
    av_opt_set_double(
        fmt->priv_data,
        "hls_time",
        segmentDuration,
        0);

    // 播放列表保留段数（0 = 全部保留）
    av_opt_set_int(
        fmt->priv_data,
        "hls_list_size",
        listSize,
        0);

    // 分段文件名模式（相对 m3u8 所在目录）
    av_opt_set(
        fmt->priv_data,
        "hls_segment_filename",
        segmentPattern.c_str(),
        0);

    // 自动删除过期段（直播场景防止磁盘占满）
    if (deleteSegments)
    {
        av_opt_set(
            fmt->priv_data,
            "hls_flags",
            "delete_segments",
            0);
    }

    Logger::Info()
        << "[HLSMuxer] Open : "
        << url
        << " (segment "
        << segmentDuration
        << "s, list "
        << listSize
        << ", delete "
        << (deleteSegments ? "on" : "off")
        << ")"
        << std::endl;

    // 注意：hls muxer 自己管理全部 IO（playlist .tmp 文件 + 各 segment
    // 文件），**不要** avio_open fmt->pb——否则会多出一个空的
    // index.m3u8 文件（hls 从不写 fmt->pb），且干扰内部的
    // .tmp → 正式文件 rename 逻辑。fmt->pb 保持 NULL 即可。

    return true;
}

void HLSMuxer::Close()
{
    Muxer::Close();
}

void HLSMuxer::SetSegmentDuration(
    double seconds)
{
    if (seconds > 0.1)
    {
        segmentDuration = seconds;
    }
}

void HLSMuxer::SetListSize(
    int size)
{
    listSize = size >= 0 ? size : 0;
}

void HLSMuxer::SetDeleteSegments(
    bool enabled)
{
    deleteSegments = enabled;
}

void HLSMuxer::SetSegmentFilenamePattern(
    const std::string& pattern)
{
    if (!pattern.empty())
    {
        segmentPattern = pattern;
    }
}

const char* HLSMuxer::GetFormatName() const
{
    return "hls";
}
