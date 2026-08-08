#include "Filter/FilterGraph.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <cstdio>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/avstring.h>
}

// ============================================================
// FilterGraph - 滤镜图
// ============================================================

FilterGraph::FilterGraph()
{
}

FilterGraph::~FilterGraph()
{
    // RAII：graph / outFrame 自动释放
}

bool FilterGraph::InitVideo(
    const std::string& filterDesc,
    int width,
    int height,
    AVPixelFormat pixFmt,
    AVRational timeBase,
    AVRational frameRate)
{
    Close();

    isVideo = true;

    outWidth = width;

    outHeight = height;

    // ---------- 分配滤镜图 ----------

    graph.reset(
        avfilter_graph_alloc());

    if (!graph)
    {
        ErrorHandler::Log(
            ErrorTag::Filter,
            "avfilter_graph_alloc failed");

        return false;
    }

    // ---------- buffer 源 ----------

    // 参数：视频尺寸 / 像素格式 / 时间基 / 帧率 / 像素宽高比
    std::string args =
        "video_size=" +
        std::to_string(width) +
        "x" +
        std::to_string(height) +
        ":pix_fmt=" +
        std::to_string(pixFmt) +
        ":time_base=" +
        std::to_string(timeBase.num) +
        "/" +
        std::to_string(timeBase.den) +
        ":frame_rate=" +
        std::to_string(frameRate.num) +
        "/" +
        std::to_string(frameRate.den) +
        ":pixel_aspect=1/1";

    int ret =
        avfilter_graph_create_filter(
            &srcCtx,
            avfilter_get_by_name("buffer"),
            "in",
            args.c_str(),
            nullptr,
            graph.get());

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Filter,
            "avfilter_graph_create_filter (buffer)",
            ret);

        Close();

        return false;
    }

    // ---------- buffersink 汇 ----------

    ret =
        avfilter_graph_create_filter(
            &sinkCtx,
            avfilter_get_by_name("buffersink"),
            "out",
            nullptr,
            nullptr,
            graph.get());

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Filter,
            "avfilter_graph_create_filter (buffersink)",
            ret);

        Close();

        return false;
    }

    // ---------- 解析滤镜链并连接 ----------

    // 用 AVFilterInOut 描述图端点：
    //   inputs  -> 链的自由输入端（连到 buffer 源）
    //   outputs -> 链的自由输出端（连到 buffersink 汇）
    AVFilterInOut* inputs =
        avfilter_inout_alloc();

    AVFilterInOut* outputs =
        avfilter_inout_alloc();

    if (!inputs || !outputs)
    {
        ErrorHandler::Log(
            ErrorTag::Filter,
            "avfilter_inout_alloc failed");

        avfilter_inout_free(&inputs);

        avfilter_inout_free(&outputs);

        Close();

        return false;
    }

    outputs->name = av_strdup("in");

    outputs->filter_ctx = srcCtx;

    outputs->pad_idx = 0;

    outputs->next = nullptr;

    inputs->name = av_strdup("out");

    inputs->filter_ctx = sinkCtx;

    inputs->pad_idx = 0;

    inputs->next = nullptr;

    ret =
        avfilter_graph_parse_ptr(
            graph.get(),
            filterDesc.c_str(),
            &inputs,
            &outputs,
            nullptr);

    avfilter_inout_free(&inputs);

    avfilter_inout_free(&outputs);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Filter,
            "avfilter_graph_parse_ptr : " +
            filterDesc,
            ret);

        Close();

        return false;
    }

    // ---------- 配置图 ----------

    ret =
        avfilter_graph_config(
            graph.get(),
            nullptr);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Filter,
            "avfilter_graph_config",
            ret);

        Close();

        return false;
    }

    // 输出尺寸可能被滤镜改变（如 scale）
    AVFilterLink* outlink =
        sinkCtx->inputs[0];

    outWidth =
        outlink->w;

    outHeight =
        outlink->h;

    // 内部输出帧
    outFrame.reset(
        av_frame_alloc());

    if (!outFrame)
    {
        ErrorHandler::Log(
            ErrorTag::Filter,
            "av_frame_alloc failed");

        Close();

        return false;
    }

    ready = true;

    Logger::Info()
        << "[FilterGraph] Video filter : "
        << filterDesc
        << " -> "
        << outWidth
        << "x"
        << outHeight
        << std::endl;

    return true;
}

bool FilterGraph::InitAudio(
    const std::string& filterDesc,
    AVSampleFormat sampleFmt,
    int sampleRate,
    uint64_t channelLayout)
{
    Close();

    isVideo = false;

    // ---------- 分配滤镜图 ----------

    graph.reset(
        avfilter_graph_alloc());

    if (!graph)
    {
        ErrorHandler::Log(
            ErrorTag::Filter,
            "avfilter_graph_alloc failed");

        return false;
    }

    // ---------- abuffer 源 ----------

    std::string args =
        "sample_rate=" +
        std::to_string(sampleRate) +
        ":sample_fmt=" +
        av_get_sample_fmt_name(sampleFmt) +
        ":channel_layout=0x" +
        ToHex(channelLayout);

    int ret =
        avfilter_graph_create_filter(
            &srcCtx,
            avfilter_get_by_name("abuffer"),
            "in",
            args.c_str(),
            nullptr,
            graph.get());

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Filter,
            "avfilter_graph_create_filter (abuffer)",
            ret);

        Close();

        return false;
    }

    // ---------- abuffersink 汇 ----------

    ret =
        avfilter_graph_create_filter(
            &sinkCtx,
            avfilter_get_by_name("abuffersink"),
            "out",
            nullptr,
            nullptr,
            graph.get());

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Filter,
            "avfilter_graph_create_filter (abuffersink)",
            ret);

        Close();

        return false;
    }

    // ---------- 解析滤镜链并连接 ----------

    AVFilterInOut* inputs =
        avfilter_inout_alloc();

    AVFilterInOut* outputs =
        avfilter_inout_alloc();

    if (!inputs || !outputs)
    {
        ErrorHandler::Log(
            ErrorTag::Filter,
            "avfilter_inout_alloc failed");

        avfilter_inout_free(&inputs);

        avfilter_inout_free(&outputs);

        Close();

        return false;
    }

    outputs->name = av_strdup("in");

    outputs->filter_ctx = srcCtx;

    outputs->pad_idx = 0;

    outputs->next = nullptr;

    inputs->name = av_strdup("out");

    inputs->filter_ctx = sinkCtx;

    inputs->pad_idx = 0;

    inputs->next = nullptr;

    ret =
        avfilter_graph_parse_ptr(
            graph.get(),
            filterDesc.c_str(),
            &inputs,
            &outputs,
            nullptr);

    avfilter_inout_free(&inputs);

    avfilter_inout_free(&outputs);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Filter,
            "avfilter_graph_parse_ptr : " +
            filterDesc,
            ret);

        Close();

        return false;
    }

    ret =
        avfilter_graph_config(
            graph.get(),
            nullptr);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Filter,
            "avfilter_graph_config",
            ret);

        Close();

        return false;
    }

    outFrame.reset(
        av_frame_alloc());

    if (!outFrame)
    {
        ErrorHandler::Log(
            ErrorTag::Filter,
            "av_frame_alloc failed");

        Close();

        return false;
    }

    ready = true;

    Logger::Info()
        << "[FilterGraph] Audio filter : "
        << filterDesc
        << std::endl;

    return true;
}

bool FilterGraph::ProcessFrame(
    AVFrame* in,
    AVFrame** out)
{
    if (!ready)
    {
        return false;
    }

    if (out)
    {
        *out = nullptr;
    }

    // ---------- 送输入 ----------

    if (in)
    {
        if (!SendInput(in))
        {
            return false;
        }
    }

    // ---------- 取输出 ----------

    av_frame_unref(outFrame.get());

    int ret =
        av_buffersink_get_frame(
            sinkCtx,
            outFrame.get());

    if (ret < 0)
    {
        // EAGAIN = 滤镜缓冲中，暂无输出；EOF = 冲刷结束
        return false;
    }

    if (out)
    {
        *out = outFrame.get();
    }

    return true;
}

void FilterGraph::Flush()
{
    if (!ready)
    {
        return;
    }

    // 送空帧触发冲刷
    SendInput(nullptr);
}

void FilterGraph::Close()
{
    // RAII：outFrame / graph 自动释放（graph 连带释放 srcCtx/sinkCtx）
    outFrame.reset();

    graph.reset();

    srcCtx = nullptr;

    sinkCtx = nullptr;

    ready = false;
}

bool FilterGraph::IsReady() const
{
    return ready;
}

int FilterGraph::GetOutputWidth() const
{
    return outWidth;
}

int FilterGraph::GetOutputHeight() const
{
    return outHeight;
}

// ============================================================
// 内部
// ============================================================

bool FilterGraph::SendInput(
    AVFrame* in)
{
    if (!srcCtx)
    {
        return false;
    }

    // KEEP_REF：不转移帧所有权，滤镜只引用
    // （调用方仍持有 in，可继续复用）
    int ret =
        av_buffersrc_add_frame_flags(
            srcCtx,
            in,
            AV_BUFFERSRC_FLAG_KEEP_REF);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Filter,
            "av_buffersrc_add_frame_flags",
            ret);

        return false;
    }

    return true;
}

// 把 uint64 转成十六进制字符串（channel_layout 参数用）
std::string FilterGraph::ToHex(uint64_t v)
{
    char buf[32] = { 0 };

    snprintf(buf, sizeof(buf), "%llx",
        static_cast<unsigned long long>(v));

    return std::string(buf);
}
